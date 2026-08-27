extern "C" {
#include "MC_transport.h"

#include <libusb.h>
}

#include "MC_impl_common.hpp"

#include <algorithm>
#include <expected>
#include <list>
#include <optional>
#include <ranges>
#include <vector>

namespace {

struct [[nodiscard]] LibUSBTransfer {
    // Microcode >= 65KB, so can't use uint16_t despite *data* <= 4KB
    using Result = std::expected<std::size_t, libusb_transfer_status>;

    static constexpr unsigned int DefaultTimeout = 1000 /* ms */;

    LibUSBTransfer() = default;
    LibUSBTransfer(const LibUSBTransfer&) = delete;
    LibUSBTransfer& operator=(LibUSBTransfer&) = delete;

    LibUSBTransfer(
        libusb_device_handle* const device,
        libusb_context* const context,
        const uint8_t endpoint
    ):
        _device(device),
        _context(context), _endpoint(endpoint) {
        _transfer = libusb_alloc_transfer(0);
    }

    ~LibUSBTransfer() {
        if (_transfer) {
            libusb_free_transfer(_transfer);
        }
    }

    LibUSBTransfer(LibUSBTransfer&& other) noexcept {
        moveFrom(std::move(other));
    }

    LibUSBTransfer& operator=(LibUSBTransfer&& other) noexcept {
        moveFrom(std::move(other));
        return *this;
    }

    LibUSBTransfer& fill(
        uint8_t* buffer,
        const std::size_t length,
        unsigned int timeout = DefaultTimeout) {
        SPAMMY(TraceLoggingThreadActivity<gTL> tla);
        SPAMMY(TraceLoggingWriteStart(tla, "LibUSBTransfer::fill()", TraceLoggingValue(length, "length")));

        if (const auto oldState = std::exchange(_state, State::Filled);
            oldState != State::Init && oldState != State::Complete) [[unlikely]] {
            LogError("Invalid state transition: {} -> {}", std::to_underlying(oldState), std::to_underlying(_state));
            abort();
        }

        _libUSBCompletionFlag = 0;

        libusb_fill_bulk_transfer(
            _transfer,
            _device,
            _endpoint,
            buffer,
            static_cast<int>(length),
            &LibUSBTransfer::callback,
            &this->_libUSBCompletionFlag,
            timeout);
        SPAMMY(TraceLoggingWriteStop(tla, "LibUSBTransfer::fill()"));
        return *this;
    }

    LibUSBTransfer& submit() {
        transition<State::Filled, State::Submitted>();

        libusb_submit_transfer(_transfer);
        return *this;
    }

    [[nodiscard]]
    Result wait() noexcept {
        while (!_libUSBCompletionFlag) {
            libusb_handle_events_completed(_context, &_libUSBCompletionFlag);
        }
        if (_state != State::Complete) {
            this->transition<State::Submitted, State::Complete>();
        }
        if (_transfer->status == LIBUSB_TRANSFER_COMPLETED) [[likely]] {
            return static_cast<std::size_t> (_transfer->actual_length);
        }
        TraceLoggingWrite(gTL, "LibUSBTransfer::wait()/failure",
            TraceLoggingValue(std::to_underlying(_transfer->status), "libusb-status"),
            TraceLoggingValue(_transfer->actual_length, "transferred"));
        return std::unexpected { _transfer->status };
    }
private:
    enum class State {
        Init,
        Filled,
        Submitted,
        Complete,
        Moved,
    };

    int _libUSBCompletionFlag {};

    libusb_device_handle* _device { nullptr };
    libusb_context* _context { nullptr };
    uint8_t _endpoint { 0 };

    libusb_transfer* _transfer { nullptr };

    State _state { State::Init };

    static void callback(libusb_transfer* const t) {
        *static_cast<int*>(t->user_data) = 1;
    }

    void moveFrom(LibUSBTransfer&& other) {
        if (other._state == State::Submitted) [[unlikely]] {
            dprint("Can't move a LibUSBTransfer which is in progress");
            abort();
        }
        if (_state == State::Submitted) [[unlikely]] {
            LogError("Can't move over a LibUSBTransfer which is in progress");
            abort();
        }

        if (_transfer) {
            libusb_free_transfer(_transfer);
        }

        _device = std::exchange(other._device, nullptr);
        _context = std::exchange(other._context, nullptr);
        _endpoint = std::exchange(other._endpoint, 0);

        _transfer = std::exchange(other._transfer, nullptr);

        _state = std::exchange(other._state, State::Moved);
        _libUSBCompletionFlag = std::exchange(other._libUSBCompletionFlag, 0);

        _transfer->user_data = &_libUSBCompletionFlag;
    }

    template<State T, State U>
    void transition() {
        if (_state != T) [[unlikely]] {
            LogError("Invalid state transition: {} -> {}", std::to_underlying(_state), std::to_underlying(U));
            abort();
        }
        _state = U;
    }
};

struct LibUSBDevice {
    LibUSBDevice() = delete;
    LibUSBDevice(const uint16_t vendorID, const uint16_t productID, const uint8_t interfaceNumber) : _interface(interfaceNumber) {
        std::ignore = libusb_init_context(&_context, nullptr, 0);
        _device = libusb_open_device_with_vid_pid(_context, vendorID, productID);
        libusb_set_auto_detach_kernel_driver(_device, true);

        {
            libusb_config_descriptor* config {};
            libusb_get_active_config_descriptor(libusb_get_device(_device), &config);

            if (interfaceNumber >= config->bNumInterfaces) {
                LogError("Invalid interface number: {} >= count {}", interfaceNumber, config->bNumInterfaces);
                return;
            }

            const auto interface = config->interface[interfaceNumber].altsetting[0];
            for (int i = 0; i < interface.bNumEndpoints; ++i) {
                const auto endpoint = interface.endpoint[i];
                if (endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                    _epIn = endpoint.bEndpointAddress;
                } else {
                    _epOut = endpoint.bEndpointAddress;
                }
            }
            libusb_free_config_descriptor(config);
        }

        if (const auto err = libusb_claim_interface(_device, interfaceNumber); err != LIBUSB_SUCCESS) {
            _interface.reset();
            LogError("Failed to claim interface: \"{}\" ({})", libusb_strerror(err), err);
            // Expected on Win32
            if (err != LIBUSB_ERROR_NOT_SUPPORTED) {
                return;
            }
        }
        std::ignore = libusb_clear_halt(_device, _epIn);
        std::ignore = libusb_clear_halt(_device, _epOut);

        dprint("Opened libusb device {:#06x}/{:#06x} interface {:#04x}: epIn: {:#04x}, epOut: {:#04x}", vendorID, productID, interfaceNumber,_epIn, _epOut);
    }

    ~LibUSBDevice() {
        if (_interface) {
            libusb_release_interface(_device, *_interface);
        }

        libusb_close(_device);
        libusb_exit(_context);
    }

    [[nodiscard]]
    LibUSBTransfer write(const void* data, const std::size_t count, const unsigned int timeout = DefaultTimeout) const {
        return this->transfer(_epOut, const_cast<void*>(data), count, timeout);
    }

    [[nodiscard]]
    LibUSBTransfer read(void* data, const std::size_t count, const unsigned int timeout = DefaultTimeout) const {
        return this->transfer(_epIn, data, count, timeout);
    }

    [[nodiscard]]
    LibUSBTransfer makeWriteTransfer() {
        return { _device, _context, _epOut };
    }

    [[nodiscard]]
    LibUSBTransfer makeReadTransfer() {
        return { _device, _context, _epIn };
    }

private:
    static constexpr auto DefaultTimeout = LibUSBTransfer::DefaultTimeout;

    libusb_context* _context {};
    libusb_device_handle* _device {};
    std::optional<uint8_t> _interface {};
    uint8_t _epIn {};
    uint8_t _epOut {};

    [[nodiscard]]
    LibUSBTransfer transfer(const uint8_t endpoint, void* data, const std::size_t count, const unsigned int timeout = 1000) const {
        auto ret = LibUSBTransfer { _device, _context, endpoint};
        ret.fill(static_cast<uint8_t*>(data), count, timeout);
        return ret;

    }
};

std::optional<LibUSBDevice>& device() {
    static std::optional<LibUSBDevice> ret {};
    return ret;
}

[[nodiscard]]
double SecondsBetween(const LARGE_INTEGER& qpBegin, const LARGE_INTEGER& qpEnd) {
    static const auto multiplier = [] {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return 1.0 / static_cast<double>(freq.QuadPart);
    }();
    return static_cast<double>(qpEnd.QuadPart - qpBegin.QuadPart) * multiplier;
}

}

extern "C" void mc_exec_batch(
  const uint8_t* const txData,
  const size_t txCount,
  uint8_t* const rxData,
  const size_t rxSize) {
#ifdef ENABLE_SPAMMY
    LARGE_INTEGER qpBegin, qpEnd;
    QueryPerformanceCounter(&qpBegin);
#endif

    SPAMMY(TraceLoggingThreadActivity<gTL> tla);
    SPAMMY(TraceLoggingWriteStart(
        tla,
        "mc_flush()",
        TraceLoggingValue(txCount, "txCount"),
        TraceLoggingValue(rxSize, "rxSize"),
        TraceLoggingValue(txCount / 2, "commandCount")
    ));

    LibUSBTransfer::Result bytesWritten, bytesRead;
    {
        // We reliably get a partial success above 64KB on Windows, so let's
        // just queue up all the transfers we'll end up doing and get a packed
        // queue instead of needing to resubmit later.
        static constexpr auto MaxTXChunk = 64*1024;
        std::list<LibUSBTransfer> txOps;
        for (std::size_t i = 0; i < txCount; i += MaxTXChunk) {
            const auto count = std::min(i + (64*1024), txCount) - i;
            txOps.emplace_back(device()->write(txData + i, count));
            txOps.back().submit();
        }

        std::ignore = txOps.back().wait(); // values checked below

        const auto txResults = std::views::transform(txOps, &LibUSBTransfer::wait) | std::ranges::to<std::vector>();
        const auto firstFailure = std::ranges::find_if_not(txResults, &LibUSBTransfer::Result::has_value);
        if (firstFailure != txResults.end()) [[unlikely]] {
            bytesWritten = *firstFailure;
        } else {
            bytesWritten = std::ranges::fold_left(
                std::views::transform(txResults, [](const auto& it) { return it.value(); }),
                0,
                std::plus<std::size_t>{});
        }

        if (rxSize == 0) {
            bytesRead = 0;
        } else {
            auto rxOp = device()->read(rxData, rxSize);
            bytesRead = rxOp.submit().wait();
        }
    };

    bool error = false;

    if (!bytesRead.has_value()) [[unlikely]] {
        error = true;
        LogError("mc_flush()/rx-error: libusb status: {}", std::to_underlying(bytesRead.error()));
    } else if (bytesRead.value() != rxSize) {
        error = true;
        LogError("mc_flush()/rx-count: expected {} actual {}", rxSize, bytesRead.value());
    }

    if (!bytesWritten.has_value()) [[unlikely]] {
        error = true;
        LogError("mc_flush()/tx-error: libusb status: {}", std::to_underlying(bytesWritten.error()));
    } else if (bytesWritten.value() != txCount) [[unlikely]] {
        error = true;
        LogError("mc_flush()/tx-count: expected {} actual {}", txCount, bytesWritten.value());
    }

    if (error) [[unlikely]] {
        abort();
    }

#ifdef ENABLE_SPAMMY
    QueryPerformanceCounter(&qpEnd);
    const auto elapsed = SecondsBetween(qpBegin, qpEnd);

    TraceLoggingWriteStop(tla, "mc_flush()",
        TraceLoggingValue(static_cast<double>(txCount) / elapsed, "usb-tx-EBps"),
        TraceLoggingValue(static_cast<double>(rxSize) / elapsed, "usb-rx-EBps"),
        TraceLoggingValue(static_cast<double>(rxSize + txCount) / elapsed, "usb-trx-EBps"));
#endif
}

void mc_usb_open(
  const uint16_t vendorID,
  const uint16_t productID,
  const uint8_t interfaceNumber) {
  device().reset();
  device().emplace(vendorID, productID, interfaceNumber);
}

void mc_usb_close() {
    device().reset();
}