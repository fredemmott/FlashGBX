#include <future>
extern "C" {
#include "LK_device.h"
#undef dprint
}

#include "PAPI.hpp"

#include <array>
#include <chrono>
#include <expected>
#include <format>
#include <functional>
#include <thread>
#include <optional>
#include <libusb.h>

#if __has_include(<windows.h>)
#include <Windows.h>
#endif

template<std::size_t N>
class RingBuffer final {
public:
    void write(const void* data, std::size_t count) {
        const auto idx = _writeIndex;
        const auto firstCount = std::min(N - idx, count);

        if (firstCount) {
            memcpy(&_buffer[idx], data, firstCount);
        }
        if (firstCount != count) {
            memcpy(&_buffer[0], static_cast<const uint8_t*>(data) + firstCount, count - firstCount);
        }

        _writeIndex= (idx + count) % N;
    }

    void read(void* data, const std::size_t count) {
        std::size_t bytesRead = 0;
        const auto it = static_cast<uint8_t*>(data);

        while (bytesRead < count) {
            while (_writeIndex == _readIndex) {
                _mm_pause();
            }

            std::size_t available = 0;
            if (_writeIndex >= _readIndex) {
                available = _writeIndex - _readIndex;
            } else {
                available = N - _readIndex;
            }

            const auto toRead = std::min(available, count - bytesRead);
            if (toRead > 0) {
                if (data) {
                    memcpy(it + bytesRead, &_buffer[_readIndex], toRead);
                }
                _readIndex = (_readIndex + toRead) % N;
                bytesRead += toRead;
            }
        }
    }
private:
    std::array<uint8_t, N> _buffer;
    std::size_t _readIndex {};
    std::size_t _writeIndex {};
};
RingBuffer<65536> fromFlashGBX;
RingBuffer<65536> toFlashGBX;

enum class Command : uint8_t {
    Ping = 0,
    SetPins = 1,
    SetOutputEnable = 2,
    SetAddress = 3,
    SetData = 4,
    GetData = 5
};

static LK_Chromatic_data_callback PAPI_OnError = nullptr;

template<class... Args>
static void dprint(std::format_string<Args...> fmt, Args&&... args) {
    auto s = std::vformat(fmt.get(), std::make_format_args(args...));
    s += '\n';
    OutputDebugStringA(s.c_str());
}

template<class... Args>
void LogError(std::format_string<Args...> fmt, Args&&... args) {
    const auto s = std::vformat(fmt.get(), std::make_format_args(args...));
    dprint("LK_Chromatic error: {}", s);
    PAPI_OnError((uint8_t*)s.data(), s.length());
}

struct [[nodiscard]] LibUSBTransfer {
    LibUSBTransfer() = delete;
    LibUSBTransfer(const LibUSBTransfer&) = delete;
    LibUSBTransfer& operator=(LibUSBTransfer&) = delete;

    explicit LibUSBTransfer(libusb_context* const context) : _context(context) {
        _state = std::make_unique<State>(State::Pending);

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

    void fill(
        libusb_device_handle* device,
        unsigned char endpoint,
        unsigned char* buffer,
        int length,
        unsigned int timeout = 0) {
        libusb_fill_bulk_transfer(
            _transfer,
            device,
            endpoint,
            buffer,
            length,
            &LibUSBTransfer::callback,
            this->_state.get(),
            timeout);
    }

    void submit() {
        libusb_submit_transfer(_transfer);
    }

    [[nodiscard]]
    std::expected<uint16_t, libusb_transfer_status> wait() noexcept {
        while (*_state == State::Pending) {
            libusb_handle_events(_context);
        }
        if (_transfer->status == LIBUSB_TRANSFER_COMPLETED) [[likely]] {
            return _transfer->actual_length;
        }
        return std::unexpected { _transfer->status };
    }
private:
    enum class State {
        Pending,
        Complete,
    };

    libusb_context* _context { nullptr };
    libusb_transfer* _transfer { nullptr };

    std::unique_ptr<State> _state;

    static void callback(libusb_transfer* const t) {
        *static_cast<State*>(t->user_data) = State::Complete;
    }

    void moveFrom(LibUSBTransfer&& other) {
        _context = std::exchange(other._context, nullptr);
        _transfer = std::exchange(other._transfer, nullptr);
        _state = std::move(other._state);
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

        dprint("LK_Chromatic: Opened libusb device {:#06x}/{:#06x} interface {:#04x}: epIn: {:#04x}, epOut: {:#04x}", vendorID, productID, interfaceNumber,_epIn, _epOut);

        // Doesn't need to be timestamp, just want to make sure that the response isn't hardcoded
        const uint8_t ping_req[] {
            std::bit_cast<uint8_t>(Command::Ping),
            static_cast<uint8_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count() & 0xff),
            0x00
        };
        uint8_t ping_reply[2] {};
        const auto expected = static_cast<uint8_t>(~ping_req[1]);
        dprint("Sending ping: {:#04x} -> {:#04x}", ping_req[1], expected);
        std::ignore = this->write(ping_req, sizeof(ping_req)).wait();
        std::ignore = this->read(ping_reply, sizeof(ping_reply)).wait();
        if ((ping_reply[0] != std::bit_cast<uint8_t>(Command::Ping)) || (ping_reply[1] != expected)) {
            LogError("LK_Chromatic: Ping response command mismatch - received {:#06x}, expected {:#06x}", (static_cast<uint16_t>(ping_reply[0]) << 8) | ping_reply[1], expected);
            return;
        }
        dprint("LK_Chromatic: Initial ping OK, {:#04x} -> {:#04x}", ping_reply[1], expected);
    }

    ~LibUSBDevice() {
        if (_interface) {
            libusb_release_interface(_device, *_interface);
        }

        libusb_close(_device);
        libusb_exit(_context);
    }

    [[nodiscard]]
    LibUSBTransfer write(const void* data, const uint16_t count) const {
        return this->transfer(_epOut, const_cast<void*>(data), count);
    }

    [[nodiscard]]
    LibUSBTransfer read(void* data, const uint16_t count) const {
        return this->transfer(_epIn, data, count);
    }

private:
    static constexpr unsigned int BufferSize = 65536;

    libusb_context* _context {};
    libusb_device_handle* _device {};
    std::optional<uint8_t> _interface {};
    uint8_t _epIn {};
    uint8_t _epOut {};

    [[nodiscard]]
    LibUSBTransfer transfer(const uint8_t endpoint, void* data, const uint16_t count) const {
        auto ret = LibUSBTransfer { _context };
        ret.fill(_device, endpoint, static_cast<uint8_t*>(data), count, 10 /* ms */);
        ret.submit();
        return ret;

    }
};

using Device = LibUSBDevice;

static std::optional<Device> gDevice;

static bool asyncEnabled = false;
static uint8_t asyncBuffer[65536];
static uint8_t* asyncBufferIt;

static bool tailIsSetPins() {
    if (!asyncEnabled) return false;
    if (asyncBufferIt == asyncBuffer) return false;
    const auto p = asyncBufferIt - 3;
    return *p == static_cast<uint8_t>(Command::SetPins);
}
static void mergeSetPins(uint8_t pins, uint8_t values) {
    const auto p = asyncBufferIt - 3;
    p[1] |= pins;
    p[2] = (p[2] & ~pins) | (values & pins);
}

extern "C" void LK_Chromatic_async_start() {
    asyncEnabled = true;
    asyncBufferIt = asyncBuffer;
    memset(asyncBuffer, 0, std::size(asyncBuffer));
}

extern "C" void LK_Chromatic_async_flush(uint8_t* data, uint16_t len) {
    asyncEnabled = false;
    const auto txCount = asyncBufferIt - asyncBuffer;
    const auto commandCount = txCount / 3;
    const auto rxCount = commandCount * 2;
    static uint8_t responseBuffer[65536];

    auto write = gDevice->write(asyncBuffer, txCount);
    
    uint16_t bytesRead = 0;
    while (bytesRead < rxCount) {
        const auto chunk = gDevice->read(&responseBuffer[bytesRead], rxCount - bytesRead).wait();
        if (!chunk.has_value()) [[unlikely]] {
            LogError("Failed RX after {} bytes: {}", bytesRead, static_cast<int>(chunk.error()));
            return;
        }
        if (chunk.value() == 0) {
            LogError("RX 0 bytes, wanted {}", rxCount - bytesRead);
            return;
        }
        bytesRead += chunk.value();
    }

    const auto bytesWritten = write.wait();

    if (!bytesWritten.has_value()) [[unlikely]] {
        LogError("Failed TX: {}", static_cast<int>(bytesWritten.error()));
        return;
    }
    if (bytesWritten.value() != txCount) [[unlikely]] {
        LogError("Incorrect TX length - expected {}, got {}", txCount, bytesWritten.value());
    }

    const auto begin = responseBuffer;
    const auto end = responseBuffer + rxCount;
    uint16_t count = 0;
    for (auto packetIt = begin; packetIt != end; packetIt = &packetIt[2]) {
        if (packetIt[0] == static_cast<uint8_t>(Command::GetData)) {
            ++count;
            if (count > len) {
                LogError("Too many data bytes - expected {}", count);
                return;
            }
            if (data) {
                *data = packetIt[1];
                ++data;
            }
        }
    }
    if (count != len) {
        LogError("Incorrect data length - expected {}, got {}", len, count);
    }
}

static void SendToDevice(const Command cmd, const uint8_t arg8a = 0, const uint8_t arg8b = 0) {
    uint8_t buffer[3];
    uint8_t* p = asyncEnabled ? asyncBufferIt : buffer;

    p[0] = static_cast<std::underlying_type_t<Command>>(cmd);
    p[1] = arg8a;
    p[2] = arg8b;

    if (asyncEnabled) {
        asyncBufferIt += 3;
    } else {
        std::ignore = gDevice->write(buffer, 3).wait();
    }
}

static void SendToDevice16(const Command cmd, const uint16_t arg16) {
    SendToDevice(cmd, static_cast<uint8_t>(arg16 >> 8), static_cast<uint8_t>(arg16 & 0xFF));
}

template<Command T>
[[nodiscard]]
static uint8_t RecvFromDevice() {
    if (asyncEnabled) {
        return 0xFF;
    }

    static constexpr auto Expected = static_cast<uint8_t>(T);
    uint8_t buffer[2];
    std::ignore = gDevice->read(buffer, 2).wait();
    if (buffer[0] != Expected) [[unlikely]] {
        LogError("Response did not match command: expected '{}', got '{}' ({:#06x})",
            Expected,
            buffer[0],
            (buffer[0] << 8) | buffer[1]
        );
        return 0;
    }
    return buffer[1];
}

template<Command T>
static void WaitForDevice() {
    if (asyncEnabled) {
        return;
    }

    const auto result = RecvFromDevice<T>();
    if (result != 1) [[unlikely]] {
        LogError("Expected ack (1), got {}", result);
    }
}

extern "C" uint32_t LK_Chromatic_TIMESTAMP_NOW() {
    using namespace std::chrono;
    using clock = std::conditional_t<high_resolution_clock::is_steady, high_resolution_clock, steady_clock>;

    static_assert(
        std::ratio_less_equal<clock::period, std::milli>::value,
        "Clock granularity is insufficent");

    static const auto epoch = clock::now();
    return duration_cast<milliseconds>(clock::now() - epoch).count();
}

extern "C" uint8_t LK_Chromatic_DMG_RAW_DATA_GET() {
    SendToDevice(Command::GetData);
    return RecvFromDevice<Command::GetData>();
}

extern "C" void LK_Chromatic_DELAY_100NS(const uint8_t count) {
    if (asyncEnabled) {
        for (uint8_t i = 0; i < count; ++i) {
            SendToDevice(Command::Ping, i);
            std::ignore = RecvFromDevice<Command::Ping>();
        }
    } else {
        std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<uint16_t>(count * 100)));
    }
}

extern "C" void LK_Chromatic_DELAY_MICROS(const uint16_t duration) {
    if (asyncEnabled) {
        LogError("DELAY_MICROS should not be called in an async batch");
    }
    std::this_thread::sleep_for(std::chrono::microseconds(duration));
}

extern "C" void LK_Chromatic_SET_PIN(uint8_t pin, uint8_t high) {
    const uint8_t pins = (1 << pin);
    const uint8_t values = (high << pin);
    if (tailIsSetPins()) {
        mergeSetPins(pins, values);
    } else {
        SendToDevice(Command::SetPins, pins, values);
        WaitForDevice<Command::SetPins>();
    }
}

extern "C" void LK_Chromatic_OUTPUT_ENABLE(uint8_t tristate_pin, uint8_t oe) {
    SendToDevice(Command::SetOutputEnable, (1 << tristate_pin), (oe << tristate_pin));
    WaitForDevice<Command::SetOutputEnable>();
}

extern "C" void LK_Chromatic_SET_ADDR_PIN(uint8_t pin, uint8_t high) {
    if (pin != 15) [[unlikely]] {
        LogError("SET_ADDR_PIN called with pin != 15 ({})", pin);
        return;
    }
    LK_Chromatic_SET_PIN(PIN_A15, high);
}

extern "C" void LK_Chromatic_DMG_ADDR_SET(const uint16_t address) {
    SendToDevice16(Command::SetAddress, address);
    WaitForDevice<Command::SetAddress>();
}

extern "C" void LK_Chromatic_DMG_DATA_SET(const uint8_t data) {
    SendToDevice(Command::SetData, data);
    WaitForDevice<Command::SetData>();
}

extern "C" void LK_Chromatic_CONN_SEND(uint8_t* data, uint16_t count) {
    toFlashGBX.write(data, count);
}

extern "C" void LK_Chromatic_CONN_RECV(uint8_t* data, uint16_t count) {
    fromFlashGBX.read(data, count);
}

///// Python API (PAPI) /////

extern "C" LK_CHROMATIC_EXPORT void papi_flashgbx_read(uint8_t* data, uint16_t count) {
    toFlashGBX.read(data, count);
}

extern "C" LK_CHROMATIC_EXPORT void papi_flashgbx_write(uint8_t* data, uint16_t count) {
    if (count == 0) {
        return;
    }

    fromFlashGBX.write(data, count);

    static bool haveWorker = false;
    if (!std::exchange(haveWorker, true)) {
        std::jthread {
            [] {
                while (true) {
                    uint8_t cmd {};
                    fromFlashGBX.read(&cmd, 1);
                    lk_loop(cmd);
                }
            }
        }.detach();
    }
}


extern "C" LK_CHROMATIC_EXPORT void papi_open(uint16_t vendorID, uint16_t productID, uint8_t interfaceNumber) {
    dprint("Attempting to open libusb device");
    gDevice.reset();
    gDevice.emplace(vendorID, productID, interfaceNumber);
}

extern "C" LK_CHROMATIC_EXPORT void papi_close() {
    gDevice.reset();
}

extern "C" LK_CHROMATIC_EXPORT void papi_set_on_error_callback(LK_Chromatic_data_callback cb) {
    PAPI_OnError = cb;
}