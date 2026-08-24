extern "C" {
#define LK_DEVICE_NO_DPRINT
#include "LK_device.h"
}

#include "PAPI.hpp"

#include <array>
#include <chrono>
#include <expected>
#include <format>
#include <functional>
#include <future>
#include <thread>
#include <optional>
#include <libusb.h>

#if __has_include(<windows.h>)
#include <Windows.h>
#include <TraceLoggingProvider.h>
#include <TraceLoggingActivity.h>
#endif

namespace {

TRACELOGGING_DEFINE_PROVIDER(
    gTL,
    "LK_Chromatic",
    (0x72b32b32, 0x28c9, 0x4298, 0xa4, 0x84, 0xc3, 0x85, 0xdd, 0xda, 0xa2, 0x1f));

template<class... Args>
void dprint(std::format_string<Args...> fmt, Args&&... args) {
    const auto s = std::vformat(fmt.get(), std::make_format_args(args...));
    TraceLoggingWrite(gTL, "dprint", TraceLoggingCountedString(s.data(), s.size(), "message"));
}

[[nodiscard]]
uint8_t GetPingCookie() {
    LARGE_INTEGER ret;
    QueryPerformanceCounter(&ret);

    // Fibonacci Hashing (TAOCP vol 3)
    // Magic number approach to 1/golden ratio from RC5
    return (ret.QuadPart * 0x9E3779B97F4A7C15ULL) >> 56;
}

template<std::size_t N>
struct ContiguousSPSCStream {
    ContiguousSPSCStream(const ContiguousSPSCStream&) = delete;
    ContiguousSPSCStream(ContiguousSPSCStream&&) = delete;
    ContiguousSPSCStream& operator=(const ContiguousSPSCStream&) = delete;
    ContiguousSPSCStream& operator=(ContiguousSPSCStream&&) = delete;

    explicit ContiguousSPSCStream(const char* const label) : _label(label) {
    }

    template<std::invocable<uint8_t*, std::size_t> Fn>
    void write(const std::size_t count, Fn&& f) {
        TraceLoggingThreadActivity<gTL> tla;
        TraceLoggingWriteStart(tla, "Stream::write()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label"));

        const auto offset = _writePos.load(std::memory_order_relaxed);
        std::invoke(std::forward<Fn>(f),_buffer.data() + offset, count);
        _writePos.store(offset + count, std::memory_order_release);
        _writePos.notify_one();
        TraceLoggingWriteStop(tla, "Stream::write()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label"));

    }

    void read(uint8_t* const dest, const std::size_t count) {
        // can't be cancelled if we don't have a stop_token
        std::ignore = read(dest, count, {});
    }

    [[nodiscard]]
    bool read(uint8_t* const dest, const std::size_t count, std::stop_token cancel) {
        TraceLoggingThreadActivity<gTL> tla;
        TraceLoggingWriteStart(tla, "Stream::read()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label"));

        const auto offset = _readPos.load(std::memory_order_relaxed);

        const std::stop_callback stopCallback{cancel, [this] {
            _writePos.notify_all();
        }};

        {
            std::size_t writeOff {};

            TraceLoggingThreadActivity<gTL> tlb;
            TraceLoggingWriteStart(tlb, "Stream::read()/wait");
            while (true) {
                if (cancel.stop_requested()) {
                    TraceLoggingWriteStop(tlb, "Stream::read()/wait", TraceLoggingValue("stopped", "result"));
                    break;
                }

                writeOff = _writePos.load(std::memory_order_acquire);
                if (writeOff - offset >= count) {
                    TraceLoggingWriteStop(tlb, "Stream::read()/wait", TraceLoggingValue("OK", "result"));
                    break;
                }

                _mm_pause();

                //_writePos.wait(writeOff, std::memory_order_release);
            }
        }

        std::memcpy(dest, _buffer.data() + offset, count);
        _readPos.store(_readPos, std::memory_order_relaxed);
        _readPos += count;
        if (_readPos == _writePos) {
            _readPos.store(0, std::memory_order_relaxed);
            _writePos.store(0, std::memory_order_release);
        }

        TraceLoggingWriteStop(tla, "Stream::read()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label"));

        return true;
    }

    [[nodiscard]]
    std::size_t pending_count() const {
        return _writePos.load(std::memory_order_acquire) - _readPos.load(std::memory_order_acquire);
    }
private:
    std::array<uint8_t, N> _buffer {};

    std::atomic<std::size_t> _readPos {};
    std::atomic<std::size_t> _writePos {};

    const char* const _label;
};

ContiguousSPSCStream<65536> fromFlashGBX("from-FlashGBX");
ContiguousSPSCStream<65536> toFlashGBX("to-FlashGBX");;

enum class Command : uint8_t {
    NOP = 0,
    Ping = 1,

    SetPins = 2,
    SetOutputEnable = 3,
    SetAddress = 4,
    SetData = 5,
    GetData = 6
};

LK_Chromatic_data_callback PAPI_OnError = nullptr;

template<class... Args>
void LogError(std::format_string<Args...> fmt, Args&&... args) {
    const auto s = std::vformat(fmt.get(), std::make_format_args(args...));
    dprint("LK_Chromatic error: {}", s);
    PAPI_OnError((uint8_t*)s.data(), s.length());
}

struct [[nodiscard]] LibUSBTransfer {
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
        unsigned char* buffer,
        int length,
        unsigned int timeout = 0) {

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
            length,
            &LibUSBTransfer::callback,
            &this->_libUSBCompletionFlag,
            timeout);

        return *this;
    }

    LibUSBTransfer& submit() {
        transition<State::Filled, State::Submitted>();

        libusb_submit_transfer(_transfer);
        return *this;
    }

    [[nodiscard]]
    std::expected<uint16_t, libusb_transfer_status> wait() noexcept {
        while (!_libUSBCompletionFlag) {
            libusb_handle_events_completed(_context, &_libUSBCompletionFlag);
        }
        this->transition<State::Submitted, State::Complete>();
        if (_transfer->status == LIBUSB_TRANSFER_COMPLETED) [[likely]] {
            return _transfer->actual_length;
        }
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

        dprint("LK_Chromatic: Opened libusb device {:#06x}/{:#06x} interface {:#04x}: epIn: {:#04x}, epOut: {:#04x}", vendorID, productID, interfaceNumber,_epIn, _epOut);

        // Doesn't need to be timestamp, just want to make sure that the response isn't hardcoded
        const uint8_t ping_req[] {
            std::bit_cast<uint8_t>(Command::Ping),
            GetPingCookie(),
            0x00
        };
        const auto expected = static_cast<uint8_t>(~ping_req[1]);
        dprint("Sending ping: {:#04x} -> {:#04x}", ping_req[1], expected);

        {
            const auto bytesWritten = this->write(ping_req, sizeof(ping_req)).submit().wait();
            if (!bytesWritten.has_value()) {
                LogError("Ping write failed: \"{}\" ({})", libusb_error_name(bytesWritten.error()), static_cast<int>(bytesWritten.error()));
                return;
            }
            if (bytesWritten.value() != 3) {
                LogError("Ping write failed: expected 3 bytes, got {}", bytesWritten.value());
                return;
            }
        }

        uint8_t ping_reply {};
        {
            const auto bytesRead = this->read(&ping_reply, 1).submit().wait();
            if (!bytesRead.has_value()) {
                LogError("Ping read failed: \"{}\" ({})", libusb_error_name(bytesRead.error()), static_cast<int>(bytesRead.error()));
                return;
            }
            if (bytesRead.value() != 1) {
                LogError("Ping read failed: expected 1 byte, got {}", bytesRead.value());
                return;
            }
        }
        if (ping_reply != expected) {
            LogError("LK_Chromatic: Ping response command mismatch - received {:#04x}, expected {:#04x}", ping_reply, expected);
            return;
        }
        dprint("LK_Chromatic: Initial ping OK, {:#04x} -> {:#04x}", ping_reply, expected);
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

    [[nodiscard]]
    LibUSBTransfer makeWriteTransfer() {
        return { _device, _context, _epOut };
    }

    [[nodiscard]]
    LibUSBTransfer makeReadTransfer() {
        return { _device, _context, _epIn };
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
        auto ret = LibUSBTransfer { _device, _context, endpoint};
        ret.fill(static_cast<uint8_t*>(data), count, 1000 /* ms */);
        return ret;

    }
};

using Device = LibUSBDevice;

std::optional<Device> gDevice;

struct CommandBuffer {
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    CommandBuffer& operator=(CommandBuffer&&) = delete;

    CommandBuffer() {
        constexpr std::size_t InitialSize = 65536;
        _begin = _end = static_cast<uint8_t*>(std::malloc(InitialSize));
        _capacity = InitialSize;
    }

    ~CommandBuffer() {
        std::free(_begin);
    }

    void push8(const Command cmd, const uint8_t arg8a, const uint8_t arg8b = 0) {
        ensureCanAppend(3);

        _end[0] = static_cast<std::underlying_type_t<Command>>(cmd);
        _end[1] = arg8a;
        _end[2] = arg8b;

        _end += 3;
    }

    template<std::invocable<uint8_t*, uint16_t> Fn>
    void pushBytes(const uint16_t count, Fn&& fn) {
        if (count % 3) [[unlikely]] {
            LogError("Attempted to push {} bytes, which is not a multiple of 3", count);
            abort();
        }
        ensureCanAppend(count);

        std::invoke(std::forward<Fn>(fn), _end, count);
        _end += count;
    }

    void clear() {
        _end = _begin;
    }

    [[nodiscard]]
    uint8_t* data() { return _begin; }
    [[nodiscard]]
    std::size_t size() const { return _end - _begin; }

    [[nodiscard]]
    uint8_t* begin() { return _begin; }

    [[nodiscard]]
    uint8_t* end() { return _end; }

private:

    uint8_t* _begin {};
    uint8_t* _end {};
    std::size_t _capacity {};

    void ensureCanAppend(const std::size_t required) {
        if (const auto s = size(); s + required > _capacity) [[unlikely]] {

            _begin = static_cast<uint8_t*>(std::realloc(_begin, _capacity * 2));
            _capacity *= 2;
            _end = _begin + s;
        }
    }
};

bool gAsyncEnabled = false;
CommandBuffer gAsyncBuffer;


void SendToDevice(const Command cmd, const uint8_t arg8a = 0, const uint8_t arg8b = 0) {
    if (gAsyncEnabled) {
        gAsyncBuffer.push8(cmd, arg8a, arg8b);
        return;
    }

#pragma pack(push, 1)
    static struct {
        Command cmd {};
        uint8_t arg8a {};
        uint8_t arg8b {};
    } request {};
#pragma pack(pop)
    request.cmd = cmd;
    request.arg8a = arg8a;
    request.arg8b = arg8b;

    const auto bytesWritten = gDevice->write(&request, sizeof(request)).submit().wait();
    if (!bytesWritten.has_value()) {
        LogError(
            "SendToDevice failed: \"{}\" ({})",
            libusb_strerror(bytesWritten.error()),
            static_cast<int>(bytesWritten.error()));
        return;
    }
    if (bytesWritten.value() != 3) {
        LogError("SendToDevice failed: expected 3 bytes written, got {}", bytesWritten.value());
    }
}

void SendToDevice16(const Command cmd, const uint16_t arg16) {
    SendToDevice(cmd, static_cast<uint8_t>(arg16 >> 8), static_cast<uint8_t>(arg16 & 0xFF));
}

[[nodiscard]]
uint8_t RecvFromDevice() {
    if (gAsyncEnabled) {
        return 0xFF;
    }

    uint8_t buffer;

    const auto bytesRead = gDevice->read(&buffer, 1).submit().wait();
    if (!bytesRead.has_value()) [[unlikely]] {
        LogError("RecvFromDevice failed: \"{}\" ({})", libusb_strerror(bytesRead.error()), static_cast<int>(bytesRead.error()));
        return 0;
    }
    if (bytesRead.value() != 1) [[unlikely]] {
        LogError("RecvFromDevice failed: expected 1 byte, got {}", bytesRead.value());
        return 0;
    }

    return buffer;
}

} // namespace

extern "C" void LK_Chromatic_dprint(const char* const data, va_list args) {
    static char buffer[1024];
    const auto count = vsnprintf(buffer, sizeof(buffer), data, args);

    std::string_view s { data, static_cast<std::size_t>(count) };
    if (s.ends_with('\n')) {
        s.remove_suffix(1);
    }
    if (s.ends_with('\r')) {
        s.remove_suffix(1);
    }

    TraceLoggingWrite(gTL, "dprint-LK", TraceLoggingCountedString(s.data(), s.size(), "message"));
}

extern "C" void LK_Chromatic_async_start() {
    gAsyncEnabled = true;
    gAsyncBuffer.clear();
    TraceLoggingWrite(gTL, "LK_Chromatic_async_start()");
}

extern "C" void LK_Chromatic_async_flush(uint8_t* const data, const uint16_t len) {
    // Limited by device-side TX buffer
    if (len > 4096) [[unlikely]] {
        LogError("Can't flush more than 4096 bytes");
        return;
    }

    gAsyncEnabled = false;
    const auto txCount = gAsyncBuffer.size();
    const auto commandCount = txCount / 3;
    const auto rxCount = len;

    TraceLoggingThreadActivity<gTL> tla;
    TraceLoggingWriteStart(
        tla,
        "LK_Chromatic_async_flush()",
        TraceLoggingValue(txCount, "txCount"),
        TraceLoggingValue(rxCount, "rxCount"),
        TraceLoggingValue(commandCount, "commandCount"),
        TraceLoggingValue(len, "len")
    );

    auto writeOp = gDevice->write(gAsyncBuffer.data(), txCount);
    auto readOp = gDevice->read(data, rxCount);

    readOp.submit();
    writeOp.submit();

    const auto [bytesRead, bytesWritten] = [&]
    {
        TraceLoggingThreadActivity<gTL> tlb;
        TraceLoggingWriteStart(tlb, "LK_Chromatic_async_flush()/wait");
        const auto rx = readOp.wait();
        const auto tx = writeOp.wait();
        TraceLoggingWriteStop(tlb, "LK_Chromatic_async_flush()/wait");
        return std::tuple {rx, tx};
    }();

    bool error = false;

    if (!bytesRead.has_value()) [[unlikely]] {
        error = true;
        LogError("Failed RX: {}", static_cast<int>(bytesRead.error()));
        TraceLoggingWriteTagged(tla, "LK_Chromatic_async_flush()/rx-error", TraceLoggingValue(std::to_underlying(bytesRead.error()), "error"));
    } else if (bytesRead.value() != rxCount) {
        error = true;
        LogError("RX: expected {} bytes, got {}", rxCount, bytesRead.value());
        TraceLoggingWriteTagged(tla, "LK_Chromatic_async_flush()/rx-count", TraceLoggingValue(rxCount, "expected"), TraceLoggingValue(bytesRead.value(), "actual"));
    }

    if (!bytesWritten.has_value()) [[unlikely]] {
        error = true;
        LogError("Failed TX: {}", static_cast<int>(bytesWritten.error()));
        TraceLoggingWriteTagged(tla, "LK_Chromatic_async_flush()/tx-error", TraceLoggingValue(std::to_underlying(bytesWritten.error()), "error"));
    } else if (bytesWritten.value() != txCount) [[unlikely]] {
        error = true;
        LogError("Incorrect TX length - expected {}, got {}", txCount, bytesWritten.value());
        TraceLoggingWriteTagged(tla, "LK_Chromatic_async_flush()/tx-count", TraceLoggingValue(rxCount, "expected"), TraceLoggingValue(bytesWritten.value(), "actual"));
    }

    if (error) [[unlikely]] {
        TraceLoggingWriteStop(tla, "LK_Chromatic_async_flush()", TraceLoggingValue("error", "result"));
        return;
    }

    TraceLoggingWriteStop(tla, "LK_Chromatic_async_flush()");
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
    return RecvFromDevice();
}

extern "C" void LK_Chromatic_DELAY_100NS(const uint8_t count) {
    static constexpr uint8_t MaxCount = 5;

    if (count == 0) [[unlikely]] {
        return;
    }

    if (count > MaxCount) [[unlikely]] {
        LogError("LK_Chromatic_DELAY_100NS() count {} is greater than max of {}", count, MaxCount);
        abort();
    }

    if (gAsyncEnabled) {
        static constexpr uint8_t MaxNOPCount = (2 * (MaxCount - 1)) + 1;
        static constexpr uint8_t MaxNOPBytes = 3 * MaxNOPCount;

        // 16.6667ns per tick; every instruction is 3 bytes to read and execute.
        //
        // If we have `foo(); NOP(); bar();` time between `foo()` and `bar()`
        // is 6 ticks, so exactly 100ns.
        //
        // However, for `foo(); NOP(); NOP(); bar();` we we get 9 ticks between
        // foo() and bar() which is only 150ns
        //
        // So, for the first 100ns, we need one NOP(), but after that, we need
        // two NOPs per 100ns
        const auto nopCount = (2 * (count - 1)) + 1;

        static constexpr auto NOPBuffer = [] constexpr {
            // 3 bytes per command, and each NOP is 50ns
            std::array<uint8_t, MaxNOPBytes> ret {};
            for (auto it = ret.begin(); it != ret.end(); it += 3) {
                it[0] = std::to_underlying(Command::NOP);
                it[1] = 0; // arg8a
                it[2] = 0; // arg8b
            }

            return ret;
        }();

        gAsyncBuffer.pushBytes(nopCount * 3, [] (auto* p, const auto byteCount){
            std::memcpy(p, NOPBuffer.data(), byteCount);
        });
    } else {
        // The OS is going to over-wait, but non-async mode is so slow anyway it doesn't matter
        std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<uint16_t>(count * 100)));
    }
}

extern "C" void LK_Chromatic_DELAY_MICROS(const uint16_t duration) {
    if (gAsyncEnabled) {
        LogError("DELAY_MICROS should not be called in an async batch");
    }
    std::this_thread::sleep_for(std::chrono::microseconds(duration));
}

extern "C" void LK_Chromatic_SET_PIN(uint8_t pin, uint8_t high) {
    const uint8_t pins = (1 << pin);
    const uint8_t values = (high << pin);
    SendToDevice(Command::SetPins, pins, values);
}

extern "C" void LK_Chromatic_OUTPUT_ENABLE(uint8_t tristate_pin, uint8_t oe) {
    SendToDevice(Command::SetOutputEnable, (1 << tristate_pin), (oe << tristate_pin));
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
}

extern "C" void LK_Chromatic_DMG_DATA_SET(const uint8_t data) {
    SendToDevice(Command::SetData, data);
}

extern "C" void LK_Chromatic_CONN_SEND(uint8_t* data, uint16_t count) {
    toFlashGBX.write(count, [src = data](uint8_t* const dest, const std::size_t n) {
        std::memcpy(dest, src, n);
    });
}

extern "C" void LK_Chromatic_CONN_RECV(uint8_t* data, uint16_t count) {
    fromFlashGBX.read(data, count);
}

///// Python API (PAPI) /////

extern "C" LK_CHROMATIC_EXPORT void papi_flashgbx_read(uint8_t* data, uint16_t count) {
    if (count == 0) {
        return;
    }

    TraceLoggingThreadActivity<gTL> tla;
    TraceLoggingWriteStart(tla, "papi_flashgbx_read()");


    toFlashGBX.read(data, count);

    TraceLoggingWriteStop(tla, "papi_flashgbx_read()");
}

extern "C" LK_CHROMATIC_EXPORT void papi_flashgbx_write(uint8_t* data, uint16_t count) {
    if (count == 0) {
        return;
    }

    TraceLoggingThreadActivity<gTL> tla;
    TraceLoggingWriteStart(tla, "papi_flashgbx_write()", TraceLoggingValue(count, "count"));

    static std::atomic_flag haveWorker {};
    if (!haveWorker.test_and_set()) {
        std::jthread {
            [] (const std::stop_token& stop) {
                while (!stop.stop_requested()) {
                    uint8_t cmd {};
                    {
                        if (!fromFlashGBX.read(&cmd, 1, stop)) {
                            haveWorker.clear();
                            return;
                        }
                    }
                    TraceLoggingThreadActivity<gTL> tla;
                    TraceLoggingWriteStart(tla, "lk_loop()", TraceLoggingHexInt8(cmd, "cmd"));
                    lk_loop(cmd);
                    TraceLoggingWriteStop(tla, "lk_loop()", TraceLoggingHexInt8(cmd, "cmd"));
                }
            }
        }.detach();
    }

    fromFlashGBX.write(count, [src = data](uint8_t* const dst, const std::size_t n) {
        std::memcpy(dst, src, n);
    });

    TraceLoggingWriteStop(tla, "papi_flashgbx_write()", TraceLoggingValue(count, "count"));
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

BOOL WINAPI DllMain(HINSTANCE, const DWORD fdwReason, const LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            TraceLoggingRegister(gTL);
            break;
        case DLL_PROCESS_DETACH:
            if (lpvReserved != nullptr) {
                // No cleanup on process exit
                break;
            }
            TraceLoggingUnregister(gTL);
            break;
        default: break;
    }
    return TRUE;
}