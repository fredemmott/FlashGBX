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


struct counters_t {
    std::size_t tx_enqueued {};
    std::size_t rx_enqueued {};

    std::atomic<std::size_t> tx_complete {};
    std::atomic<std::size_t> rx_complete {};
};
counters_t& counters() {
    static counters_t instance {};
    return instance;
}

struct callbacks_t {
    void onTxProgress(const std::size_t count, const int error) const {
        onProgress(counters().tx_complete, _callbacks.on_tx_progress, count, error);
    }

    void onRxProgress(const std::size_t count, const int error) const {
        onProgress(counters().rx_complete, _callbacks.on_rx_progress, count, error);
    }

    void set(const mc_transport_callbacks* const callbacks) {
        if (callbacks) {
            _callbacks = *callbacks;
        } else {
            _callbacks = {};
        }
    }
private:
    mc_transport_callbacks _callbacks {};

    void onProgress(
        std::atomic<std::size_t>& counter,
        const decltype(mc_transport_callbacks::on_tx_progress) callback,
        const std::size_t count,
        const int error) const {
        const auto old = counter.fetch_add(count, std::memory_order::relaxed);
        if (!callback) {
            return;
        }
        const mc_transport_progress progress {
            .completed_this_transaction = count,
            .completed_cumulative = old + count,
            .error = error,
        };
        callback(_callbacks.user_data, &progress);
    }
};
callbacks_t& callbacks() {
    static callbacks_t instance {};
    return instance;
}

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
        if (_transfer->status == LIBUSB_TRANSFER_COMPLETED) [[likely]] {
            return static_cast<std::size_t>(_transfer->actual_length);
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
        const auto count = static_cast<std::size_t>(t->actual_length);
        const auto isRX = (t->endpoint & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN;

        if (isRX) {
            callbacks().onRxProgress(count, t->status);
        } else {
            callbacks().onTxProgress(count, t->status);
        }

        auto& self = *static_cast<LibUSBTransfer*>(t->user_data);
        self.transition<State::Submitted, State::Complete>();
        self._libUSBCompletionFlag = 1;

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

struct transfers_t {
    std::list<LibUSBTransfer> tx {};
    std::list<LibUSBTransfer> rx {};
};
transfers_t& transfers() {
    static transfers_t _transactions;
    return _transactions;
}

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

enum class Operation { TX, RX };
template<Operation T>
[[nodiscard]]
std::size_t enqueue(
    std::conditional_t<T == Operation::TX, const uint8_t*, uint8_t*> data,
    const size_t count
) {
    static constexpr bool IsRX = T == Operation::RX;
    auto& counter = IsRX ? counters().rx_enqueued : counters().tx_enqueued;
    auto& ops = IsRX ? transfers().rx : transfers().tx;

    const auto before = counter;
    const auto after = before + count;
    counter = after;

    // We reliably get a partial success above 64KB on Windows, so let's
    // just queue up all the transfers we'll end up doing and get a packed
    // queue instead of needing to resubmit later.
    constexpr std::size_t MaxChunk = 64*1024;

    for (std::size_t i = 0; i < count; i += MaxChunk) {
        const auto chunk = std::min(i + (64*1024), count) - i;
        if constexpr (IsRX) {
            ops.emplace_back(device()->read(data + i, chunk));
        } else {
            ops.emplace_back(device()->write(data + i, chunk));
        }
        ops.back().submit();
    }

    return before;
}

}

extern "C" void mc_transport_set_callbacks(const mc_transport_callbacks* new_callbacks) {
    callbacks().set(new_callbacks);
}

extern "C" size_t mc_transport_enqueue_tx(const uint8_t* const data, const size_t count) {
    return enqueue<Operation::TX>(data, count);
}

extern "C" size_t mc_transport_enqueue_rx(uint8_t* const data, const size_t count) {
    return enqueue<Operation::RX>(data, count);
}

extern "C" void mc_transport_flush() {
    if (!transfers().tx.empty()) {
        std::ignore = transfers().tx.back().wait();
    }
    if (!transfers().rx.empty()) {
        std::ignore = transfers().rx.back().wait();
    }
}

void mc_usb_open(
  const uint16_t vendorID,
  const uint16_t productID,
  const uint8_t interfaceNumber) {
  device().reset();
  device().emplace(vendorID, productID, interfaceNumber);

  mc_init();
}

void mc_usb_close() {
    mc_reset();

    device().reset();
}