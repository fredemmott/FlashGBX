extern "C" {
#define LK_DEVICE_NO_DPRINT
#include "LK_device.h"
}

#include "PAPI.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <ranges>
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

#define SET_THREAD_NAME(x) {std::ignore = SetThreadDescription(GetCurrentThread(), L##x);}
#define UNSET_THREAD_NAME() {std::ignore = SetThreadDescription(GetCurrentThread(), L"");}
#endif

namespace {

constexpr auto BytesPerCommand = 2;

TRACELOGGING_DEFINE_PROVIDER(
    gTL,
    "LK_Chromatic",
    (0x72b32b32, 0x28c9, 0x4298, 0xa4, 0x84, 0xc3, 0x85, 0xdd, 0xda, 0xa2, 0x1f));

// Anything other than 0 noticeably impacts performance, even if TraceLogging is disabled
#define SPAM_LEVEL 0

#if SPAM_LEVEL >= 1
#define SPAMMY(x) x;
#else
#define SPAMMY(x) {}
#endif

#if SPAM_LEVEL >= 2
#define SUPER_SPAMMY(x) x;
#else
#define SUPER_SPAMMY(x) {}
#endif

template<class... Args>
void dprint(std::format_string<Args...> fmt, Args&&... args) {
    const auto s = std::vformat(fmt.get(), std::make_format_args(args...));
    TraceLoggingWrite(gTL, "dprint", TraceLoggingCountedString(s.data(), s.size(), "message"));

    const auto ds = s + "\n";
    OutputDebugStringA(ds.c_str());
}

[[nodiscard]]
uint8_t GetPingCookie() {
    LARGE_INTEGER ret;
    QueryPerformanceCounter(&ret);

    // Fibonacci Hashing (TAOCP vol 3)
    // Magic number approach to 1/golden ratio from RC5
    return (ret.QuadPart * 0x9E3779B97F4A7C15ULL) >> 56;
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
        SPAMMY(TraceLoggingThreadActivity<gTL> tla);
        SPAMMY(TraceLoggingWriteStart(tla, "Stream::write()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label")));

        const auto offset = _writePos.load(std::memory_order_relaxed);
        std::invoke(std::forward<Fn>(f),_buffer.data() + offset, count);
        _writePos.store(offset + count, std::memory_order_release);
        _writePos.notify_one();
        SPAMMY(TraceLoggingWriteStop(tla, "Stream::write()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label")));
    }

    void read(uint8_t* const dest, const std::size_t count) {
        // can't be cancelled if we don't have a stop_token
        std::ignore = read(dest, count, {});
    }

    [[nodiscard]]
    bool read(uint8_t* const dest, const std::size_t count, const std::stop_token& cancel) {
        SPAMMY(TraceLoggingThreadActivity<gTL> tla);
        SPAMMY(TraceLoggingWriteStart(tla, "Stream::read()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label")));

        const auto offset = _readPos.load(std::memory_order_relaxed);

        const std::stop_callback stopCallback{cancel, [this] {
            _writePos.notify_all();
        }};

        {
            std::size_t writeOff {};

            SPAMMY(TraceLoggingThreadActivity<gTL> tlb);
            SPAMMY(TraceLoggingWriteStart(tlb, "Stream::read()/wait"));
            while (true) {
                if (cancel.stop_requested()) {
                    SPAMMY(TraceLoggingWriteStop(tlb, "Stream::read()/wait", TraceLoggingValue("stopped", "result")));
                    break;
                }

                writeOff = _writePos.load(std::memory_order_acquire);
                if (writeOff - offset >= count) {
                    SPAMMY(TraceLoggingWriteStop(tlb, "Stream::read()/wait", TraceLoggingValue("OK", "result")));
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

        SPAMMY(TraceLoggingWriteStop(tla, "Stream::read()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label")));

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

    SetAddressMSB = 2,
    SetAddressLSB = 3,
    SetOutputEnable = 4,
    SetData = 5,
    GetData = 6,
    SetPinsA = 7,
    SetPinsB = 8,
};

LK_Chromatic_data_callback PAPI_OnError = nullptr;

template<class... Args>
void LogError(std::format_string<Args...> fmt, Args&&... args) {
    const auto s = std::vformat(fmt.get(), std::make_format_args(args...));
    dprint("LK_Chromatic error: {}", s);
    PAPI_OnError((uint8_t*)s.data(), s.length());
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

        dprint("LK_Chromatic: Opened libusb device {:#06x}/{:#06x} interface {:#04x}: epIn: {:#04x}, epOut: {:#04x}", vendorID, productID, interfaceNumber,_epIn, _epOut);

        // Doesn't need to be timestamp, just want to make sure that the response isn't hardcoded
        const auto cookie = GetPingCookie();
        const auto expected = (~cookie) & 0xff;

        dprint("Sending ping: {:#04x} -> {:#04x}", cookie, expected);
        const auto actual = LK_Chromatic_ping(cookie);
        if (actual != expected) {
            LogError("Ping response command mismatch - received {:#04x}, expected {:#04x}", actual, expected);
            return;
        }
        dprint("LK_Chromatic: Initial ping OK");
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
    static constexpr unsigned int BufferSize = 65536;

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

    void push(const Command cmd, const uint8_t arg8) {
        ensureCanAppend(BytesPerCommand);

        static_assert(BytesPerCommand == 2);
        _end[0] = static_cast<std::underlying_type_t<Command>>(cmd);
        _end[1] = arg8;

        _end += BytesPerCommand;
    }

    template<std::invocable<uint8_t*, std::size_t> Fn>
    void pushBytes(const std::size_t count, Fn&& fn) {
        if (count % BytesPerCommand) [[unlikely]] {
            LogError("Attempted to push {} bytes, which is not a multiple of {}", count, BytesPerCommand);
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

struct CommandQueue {
    void begin() {
        if (std::exchange(_enabled, true)) [[unlikely]] {
            LogError("CommandQueue::begin() called twice without end()");
            abort();
        }
    }

    void end() {
        if (std::exchange(_enabled, false) == false) [[unlikely]] {
            LogError("CommandQueue::end() called without begin()");
            abort();
        }

        if (const auto unflushed = std::exchange(_expectedRX, 0); unflushed != 0) [[unlikely]] {
            LogError("CommandQueue::end() called with {} expected RX bytes; call flush() first", unflushed);
            abort();
        }

        if (_buffer.size() == 0) {
            return;
        }
        flush(nullptr, 0);
        _buffer.clear();
    }

    void push(const Command cmd, const uint8_t arg) {
        _buffer.push(cmd, arg);

        if (ProducesRX(cmd)) {
            _expectedRX++;
            SUPER_SPAMMY(TraceLoggingWrite(gTL, "push()/_expectedRX++", TraceLoggingHexInt8(std::to_underlying(cmd), "MC"), TraceLoggingValue(_expectedRX, "newValue")));
        }
    }

    template<std::invocable<uint8_t*, std::size_t> Fn>
    void pushBytes(const std::size_t count, Fn&& fn) {
        const auto base = _buffer.size();
        _buffer.pushBytes(count, std::forward<Fn>(fn));
        const auto begin = _buffer.data();
        const auto end = begin + count;
        for (auto it = begin; it < end; it += BytesPerCommand) {
          if (ProducesRX(static_cast<Command>(*it))) {
                _expectedRX++;
                SUPER_SPAMMY(TraceLoggingWrite(gTL, "pushBytes()/_expectedRX++", TraceLoggingHexInt8(std::to_underlying(cmd), "MC"), TraceLoggingValue(_expectedRX, "newValue")));
            }
        }
    }

    void flush(uint8_t* data, uint16_t rxCount);

    static CommandQueue& get() {
        static CommandQueue instance {};
        return instance;
    }

    [[nodiscard]]
    CommandBuffer& buffer() {
        return _buffer;
    }
private:
    CommandBuffer _buffer {};
    bool _enabled { false };
    std::size_t _expectedRX {};

    CommandQueue() = default;
};

void SendToDevice(const Command cmd, const uint8_t arg = 0x00) {
    CommandQueue::get().push(cmd, arg);
}

template<std::unsigned_integral T>
constexpr T HundredsOfNSToNOPCount(const T count) {
    // The microcode is executed using the USB clock as the execution
    // clock - so byte count == tick count.
    //
    // Our clock interval is 16.6667ns, so 100ns is 6 ticks. Our
    // commands are currently 2 bytes == 2 ticks, so 33.333ns.
    //
    // If we have `foo(); NOP(); bar();` though, we have a 4 tick delay
    // between `foo()` and `bar()`:
    //
    //     foo(); NOP(); bar();
    //     |------| 2 bytes = 2 ticks
    //            |------| 2 bytes = 2 ticks
    //     |-------------| 4 bytes = 4 ticks
    //
    // ... and each additional NOP gets us another 2 ticks:
    //
    //     foo(); NOP(); NOP(); bar();
    //     |------|      |------|
    //            |------|
    //     |--------------------| 6 bytes = 6 ticks
    //
    // So, for the first 100ns, we need two NOPs(), but after that, we need
    // three NOPs per 100ns.
    //
    // For 2 bytes per command, that gets us:
    //
    //     (3 * count - 1)) + 2
    //
    // Let's generalize that:
    static constexpr auto TicksPerCommand = BytesPerCommand;
    // 2x for the interval between `foo()` and `bar()` in `foo(); NOP(); bar();`
    static constexpr auto FirstNOPTicks = 2 * TicksPerCommand;
    // 6x because 16.667ns tick rate = 100ns/6
    const auto requiredTicks = 6 * count;
    return 1 + ((requiredTicks - FirstNOPTicks) / TicksPerCommand);
};

// Used to force overload selection
struct ForceImpl {};

void PushNOPs(const uint8_t count, ForceImpl = {}) {
    if (count == 0) {
        return;
    }
    static constexpr uint8_t MaxCount = std::numeric_limits<decltype(count)>::max();
    static constexpr auto MaxBytes = BytesPerCommand * static_cast<std::size_t>(MaxCount);

    static constexpr auto Buffer = [] constexpr {
        std::array<uint8_t, MaxBytes> ret {};
        // Arguments are unused, so we can just use NOP as the arg
        ret.fill(std::to_underlying(Command::NOP));
        return ret;
    }();

    const auto nopCount = HundredsOfNSToNOPCount(count);
    CommandQueue::get().pushBytes(nopCount * BytesPerCommand, [] (auto* p, const auto byteCount){
        std::memcpy(p, Buffer.data(), byteCount);
    });
}

template<std::unsigned_integral T>
requires (!std::same_as<uint8_t, std::remove_cvref_t<T>>)
void PushNOPs(const T count) {
    const T full = count / 0xFF;
    const auto partial = static_cast<uint8_t>(count % 0xFF);

    for (T i = 0; i < full; i = i + 1) {
        PushNOPs(0xFF, ForceImpl {});
    }

    PushNOPs(partial, ForceImpl {});
}

} // namespace

extern "C" uint8_t LK_Chromatic_ping(const uint8_t cookie) {
    auto& cq = CommandQueue::get();
    cq.push(Command::Ping, cookie);
    uint8_t ret {};
    cq.flush(&ret, 1);
    return ret;
}

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
    SPAMMY(TraceLoggingWrite(gTL, "LK_Chromatic_async_start()"));
}

extern "C" void LK_Chromatic_async_end() {
    gAsyncEnabled = false;
    if (gAsyncBuffer.size() != 0) {
        LK_Chromatic_async_flush(nullptr, 0);
    }
    SPAMMY(TraceLoggingWrite(gTL, "LK_Chromatic_async_end()"));
}

extern "C" void LK_Chromatic_async_flush(uint8_t* const data, const uint16_t len) {
    CommandQueue::get().flush(data, len);
}

void CommandQueue::flush(uint8_t* const data, const uint16_t rxCount) {
    const auto txCount = _buffer.size();

    // Limited by device-side TX buffer
    if (rxCount> 4096) [[unlikely]] {
        LogError("Can't flush more than 4096 bytes");
        abort();
    }
    if (const auto unflushed = std::exchange(_expectedRX, 0); unflushed != rxCount) [[unlikely]] {
        LogError("Expected to flush {} RX bytes, asked for {}", unflushed, rxCount);
        abort();
    }

#ifdef ENABLE_SPAMMY
    LARGE_INTEGER qpBegin, qpEnd;
    QueryPerformanceCounter(&qpBegin);
#endif

    SPAMMY(TraceLoggingThreadActivity<gTL> tla);
    SPAMMY(TraceLoggingWriteStart(
        tla,
        "CommandQueue::flush()",
        TraceLoggingValue(txCount, "txCount"),
        TraceLoggingValue(rxCount, "rxCount"),
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
            txOps.emplace_back(gDevice->write(_buffer.data() + i, count));
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

        if (rxCount == 0) {
            bytesRead = 0;
        } else {
            auto rxOp = gDevice->read(data, rxCount);
            bytesRead = rxOp.submit().wait();
        }
    };

    bool error = false;

    if (!bytesRead.has_value()) [[unlikely]] {
        error = true;
        LogError("CommandQueue::flush()/rx-error: libusb status: {}", std::to_underlying(bytesRead.error()));
    } else if (bytesRead.value() != rxCount) {
        error = true;
        LogError("CommandQueue::flush()/rx-count: expected {} actual {}", rxCount, bytesRead.value());
    }

    if (!bytesWritten.has_value()) [[unlikely]] {
        error = true;
        LogError("CommandQueue::flush()/tx-error: libusb status: {}", std::to_underlying(bytesWritten.error()));
    } else if (bytesWritten.value() != txCount) [[unlikely]] {
        error = true;
        LogError("CommandQueue::flush()/tx-count: expected {} actual {}", txCount, bytesWritten.value());
    }

    if (error) [[unlikely]] {
        abort();
    }


    _buffer.clear();
#ifdef ENABLE_SPAMMY
    QueryPerformanceCounter(&qpEnd);
    const auto elapsed = SecondsBetween(qpBegin, qpEnd);

    TraceLoggingWriteStop(tla, "CommandQueue::flush()",
        TraceLoggingValue(static_cast<double>(txCount) / elapsed, "usb-tx-EBps"),
        TraceLoggingValue(static_cast<double>(rxCount) / elapsed, "usb-rx-EBps"),
        TraceLoggingValue(static_cast<double>(rxCount + txCount) / elapsed, "usb-trx-EBps"));
#endif
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
    return 0xFF; // async
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
        static constexpr auto ToNOPCount = [](const uint8_t count) constexpr {
            // The microcode is executed using the USB clock as the execution
            // clock - so byte count == tick count.
            //
            // Our clock interval is 16.6667ns, so 100ns is 6 ticks. Our
            // commands are currently 2 bytes, so 33.333ns per command.
            //
            // If we have `foo(); NOP(); bar();` though, we have a 4 tick delay
            // between `foo()` and `bar()`:
            //
            //     foo(); NOP(); bar();
            //     |------| 2 bytes = 2 ticks
            //            |------| 2 bytes = 2 ticks
            //     |-------------| 4 bytes = 4 ticks
            //
            // ... and each additional NOP gets us another 2 ticks:
            //
            //     foo(); NOP(); NOP(); bar();
            //     |------|      |------|
            //            |------|
            //     |--------------------| 6 bytes = 6 ticks
            //
            // So, for the first 100ns, we need two NOPs(), but after that, we need
            // three NOPs per 100ns.
            //
            // For 2 bytes per command, that gets us:
            //
            //     (3 * count - 1)) + 2
            //
            // Let's generalize that:
            static constexpr auto TicksPerCommand = BytesPerCommand;
            // 2x for the interval between `foo()` and `bar()` in `foo(); NOP(); bar();`
            static constexpr auto FirstNOPTicks = 2 * TicksPerCommand;
            // 6x because 16.667ns tick rate = 100ns/6
            const auto requiredTicks = 6 * count;
            return 1 + ((requiredTicks - FirstNOPTicks) / TicksPerCommand);
        };

        static constexpr uint8_t MaxNOPCount = ToNOPCount(MaxCount);
        static constexpr uint8_t MaxNOPBytes = BytesPerCommand * MaxNOPCount;

        static constexpr auto NOPBuffer = [] constexpr {
            std::array<uint8_t, MaxNOPBytes> ret {};
            // Arguments are unused, so we can just use NOP as the arg
            ret.fill(std::to_underlying(Command::NOP));
            return ret;
        }();

        const auto nopCount = ToNOPCount(count);
        gAsyncBuffer.pushBytes(nopCount * BytesPerCommand, [] (auto* p, const auto byteCount){
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

extern "C" void LK_Chromatic_SET_PIN(const uint8_t pin, const uint8_t high) {
    const auto command = ((pin & LK_CHROMATIC_SET_PINS_COMMAND_MASK) == LK_CHROMATIC_SET_PINS_A_MASK)
        ? Command::SetPinsA
        : Command::SetPinsB;

    const auto bitIdx = static_cast<uint8_t>(pin & 0b1111);

    SendToDevice(command, (1 << (bitIdx + 4)) | (high << bitIdx));
}

extern "C" void LK_Chromatic_OUTPUT_ENABLE(const uint8_t tristate_pin, const uint8_t oe) {
    SendToDevice(
        Command::SetOutputEnable,
        (1 << (tristate_pin + 4)) | (oe << tristate_pin));
}

extern "C" void LK_Chromatic_SET_ADDR_PIN(uint8_t pin, uint8_t high) {
    if (pin != 15) [[unlikely]] {
        LogError("SET_ADDR_PIN called with pin != 15 ({})", pin);
        return;
    }
    LK_Chromatic_SET_PIN(LK_CHROMATIC_PIN_A15, high);
}

extern "C" void LK_Chromatic_DMG_ADDR_SET(const uint16_t address) {
    SendToDevice(Command::SetAddressMSB, address >> 8);
    SendToDevice(Command::SetAddressLSB, address & 0xff);
}

extern "C" void LK_Chromatic_DMG_DATA_SET(const uint8_t data) {
    SendToDevice(Command::SetData, data);
}

extern "C" void LK_Chromatic_CONN_SEND(uint8_t* data, const uint16_t count) {
    toFlashGBX.write(count, [src = data](uint8_t* const dest, const std::size_t n) {
        std::memcpy(dest, src, n);
    });
}

extern "C" void LK_Chromatic_CONN_RECV(uint8_t* data, const uint16_t count) {
    fromFlashGBX.read(data, count);
}

///// Python API (PAPI) /////

extern "C" LK_CHROMATIC_EXPORT void papi_flashgbx_read(uint8_t* data, const uint16_t count) {
    if (count == 0) {
        return;
    }

    SPAMMY(TraceLoggingThreadActivity<gTL> tla);
    SPAMMY(TraceLoggingWriteStart(tla, "papi_flashgbx_read()"));


    toFlashGBX.read(data, count);

    SPAMMY(TraceLoggingWriteStop(tla, "papi_flashgbx_read()"));
}

extern "C" LK_CHROMATIC_EXPORT void papi_flashgbx_write(uint8_t* data, const uint16_t count) {
    if (count == 0) {
        return;
    }

    SPAMMY(TraceLoggingThreadActivity<gTL> tla);
    SPAMMY(TraceLoggingWriteStart(tla, "papi_flashgbx_write()", TraceLoggingValue(count, "count")));

    static std::atomic_flag haveWorker {};
    if (!haveWorker.test_and_set()) {
        std::jthread {
            [] (const std::stop_token& stop) {
                SET_THREAD_NAME("LK -> Microcode worker");

                auto& cq = CommandQueue::get();
                while (!stop.stop_requested()) {
                    uint8_t cmd {};
                    {
                        if (!fromFlashGBX.read(&cmd, 1, stop)) {
                            haveWorker.clear();
                            UNSET_THREAD_NAME();
                            return;
                        }
                    }
                    TraceLoggingThreadActivity<gTL> tla;
                    TraceLoggingWriteStart(tla, "lk_loop()", TraceLoggingHexInt8(cmd, "cmd"));
                    cq.begin();
                    lk_loop(cmd);
                    cq.end();
                    TraceLoggingWriteStop(tla, "lk_loop()", TraceLoggingHexInt8(cmd, "cmd"));
                }
            }
        }.detach();
    }

    fromFlashGBX.write(count, [src = data](uint8_t* const dst, const std::size_t n) {
        std::memcpy(dst, src, n);
    });

    SPAMMY(TraceLoggingWriteStop(tla, "papi_flashgbx_write()", TraceLoggingValue(count, "count")));
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