extern "C" {
#define LK_DEVICE_NO_DPRINT
#include "LK_device.h"
#include "MC_transport.h"
}

#include "MC_impl_common.hpp"

#include <algorithm>
#include <chrono>
#include <expected>
#include <format>
#include <functional>
#include <future>
#include <ranges>

namespace {

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
    VerifyData = 9,
    VerifyStatusRegister = 10,
    SetStatusRegisterMask = 11,
    SetStatusRegisterValue  = 12,
    GetStateBits = 13,
    SetCartPower = 14
};

enum class StateBits : uint8_t {
    CartPresent = 1 << 0,
    CartPower = 1 << 1
};

enum class SetPinsA : uint8_t {
    CLK = 1 << 0,
    WR = 1 << 1,
    RD = 1 << 2,
    CS = 1 << 3,
};

enum class SetPinsB : uint8_t {
    A15 = 1 << 0,
    RST = 1 << 1,
    AUDIO = 1 << 2,
};

[[nodiscard]]
bool ProducesRX(const Command cmd) noexcept {
    using enum Command;

    switch (cmd) {
    case Ping:
    case GetData:
    case VerifyData:
    case VerifyStatusRegister:
    case GetStateBits:
        return true;
    default:
        return false;
    }
}

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
    uint8_t* begin() const noexcept { return _begin; }

    [[nodiscard]]
    uint8_t* end() const noexcept { return _end; }

private:

    uint8_t* _begin {};
    uint8_t* _end {};
    std::size_t _capacity {};

    void ensureCanAppend(const std::size_t required) {
        if (const auto s = size(); s + required > _capacity) {
            const auto newCapacity = std::max<std::size_t>(s + required, _capacity * 2);
            _begin = static_cast<uint8_t*>(std::realloc(_begin, newCapacity));
            _capacity = newCapacity;
            _end = _begin + s;
        }
    }
};

struct CommandQueue {
    [[nodiscard]] uint8_t* begin() const noexcept {
        return _buffer.begin();
    }

    [[nodiscard]] uint8_t* end() const noexcept {
        return _buffer.end();
    }

    void start_batch() {
        if (std::exchange(_enabled, true)) [[unlikely]] {
            LogError("CommandQueue::begin() called twice without end()");
            abort();
        }
    }

    void end_batch() {
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

    void push(const Command cmd, const uint8_t arg = 0x00) {
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
        const auto begin = _buffer.data() + base;
        const auto end = begin + count;
        for (auto it = begin; it < end; it += BytesPerCommand) {
          if (ProducesRX(static_cast<Command>(*it))) {
                _expectedRX++;
                SUPER_SPAMMY(TraceLoggingWrite(gTL, "pushBytes()/_expectedRX++", TraceLoggingHexInt8(std::to_underlying(*it), "MC"), TraceLoggingValue(_expectedRX, "newValue")));
            }
        }
    }

    void flush(uint8_t* data, uint16_t rxCount);

    static CommandQueue& get() {
        static CommandQueue instance {};
        return instance;
    }

private:
    CommandBuffer _buffer {};
    bool _enabled { false };
    std::size_t _expectedRX {};

    CommandQueue() = default;
};

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

template<std::unsigned_integral T>
void PushNOPs(const T count) {
    if (count == 0) {
        return;
    }

    const auto nopCount = HundredsOfNSToNOPCount(count);
    CommandQueue::get().pushBytes(nopCount * BytesPerCommand, [] (auto* p, const auto byteCount){
        // Argument is ignored, so we might as well fill it with NOPs as well :)
        std::memset(p, std::to_underlying(Command::NOP), byteCount);
    });
}

} // namespace

extern "C" uint8_t LK2MC_ping(const uint8_t cookie) {
    auto& cq = CommandQueue::get();
    cq.push(Command::Ping, cookie);
    uint8_t ret {};
    cq.flush(&ret, 1);
    return ret;
}

extern "C" void LK2MC_dprint(const char* const data, va_list args) {
    static char buffer[1024];
    const auto count = vsnprintf(buffer, sizeof(buffer), data, args);

    std::string_view s { buffer, static_cast<std::size_t>(count) };
    if (s.ends_with('\n')) {
        s.remove_suffix(1);
    }
    if (s.ends_with('\r')) {
        s.remove_suffix(1);
    }

    TraceLoggingWrite(gTL, "dprint-LK", TraceLoggingCountedString(s.data(), s.size(), "message"));
}

extern "C" void LK2MC_verify_data(const uint8_t expected) {
    // `lk_dmg_verify_data()`, with the loop body moved to a dedicated microcode command
    PIN_RD_L();
    PIN_CLK_L(); // Pocket Camera needs this
    RAW_DMG_DATA_SET(0);
    RAW_DMG_DATA_DIR_IN();
    RAW_DMG_ADDR_DIR_OUT();
    // Address should still be set from the write
    CommandQueue::get().push(Command::VerifyData, expected);
}

extern "C" void LK2MC_verify_status_register() {
    // `lk_dmg_verify_status_register()`, with the loop body moved to a dedicated microcode command
    RAW_DMG_DATA_SET(0);
    RAW_DMG_DATA_DIR_IN();
	RAW_DMG_ADDR_DIR_OUT();
    // Address should have already been set by the caller
    CommandQueue::get().push(Command::VerifyStatusRegister);
    RAW_DMG_DATA_DIR_OUT();
}

extern "C" uint8_t LK2MC_verify_status_register_flush(uint8_t* const buffer, const uint32_t count) {
    const auto mask = _lk_var16[LK_VAR16_STATUS_REGISTER_MASK];
    const auto value = _lk_var16[LK_VAR16_STATUS_REGISTER_VALUE];

    CommandQueue::get().flush(buffer, count);
    const auto begin = buffer;
    const auto end = buffer + count;

    for (auto it = begin; it != end; ++it) {
        if (((*it) & mask) != value) {
            dprint("LK2MC_verify_status_register_flush(): Timed out with {}!", *it);
            _lk_var16[LK_VAR16_STATUS_REGISTER] = *it;
            return LK_STATUS_ERROR;
        }
    }
    return LK_STATUS_OK;
}

extern "C" uint32_t LK2MC_get_pending_verify_status_register_count() {
    const auto& cq = CommandQueue::get();

    std::size_t ret {};

    for (auto it = cq.begin(); it != cq.end(); it += BytesPerCommand) {
        if (static_cast<Command>(*it) == Command::VerifyStatusRegister) {
            ++ret;
        }
    }

    if (ret > std::numeric_limits<uint32_t>::max()) [[unlikely]] {
        LogError("RX count > u32 max");
        abort();
    }
    if (ret > CHUNK_MAX_LEN) [[unlikely]] {
        LogError("RX count ({}) > CHUNK_MAX_LEN ({})", ret, CHUNK_MAX_LEN);
        abort();
    }
    return static_cast<uint32_t>(ret);
}

extern "C" void LK2MC_set_variable(const uint8_t size, const uint32_t key, const uint32_t value) {
    if (size != 2) {
        return;
    }
    switch (key) {
    case LK_VAR16_STATUS_REGISTER_MASK:
        CommandQueue::get().push(Command::SetStatusRegisterMask, value & 0xFF);
        break;
    case LK_VAR16_STATUS_REGISTER_VALUE:
        CommandQueue::get().push(Command::SetStatusRegisterValue, value & 0xFF);
        break;
    default: ;
    }
}

extern "C" void LK2MC_flush(uint8_t* const data, const uint16_t len) {
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

    mc_exec_batch(_buffer.data(), txCount, data, rxCount);

    _buffer.clear();
}

extern "C" uint32_t LK2MC_TIMESTAMP_NOW() {
    using namespace std::chrono;
    using clock = std::conditional_t<high_resolution_clock::is_steady, high_resolution_clock, steady_clock>;

    static_assert(
        std::ratio_less_equal<clock::period, std::milli>::value,
        "Clock granularity is insufficent");

    static const auto epoch = clock::now();
    return duration_cast<milliseconds>(clock::now() - epoch).count();
}

extern "C" uint8_t LK2MC_DMG_DATA_GET() {
    CommandQueue::get().push(Command::GetData);
    return 0xFF; // async
}

extern "C" void LK2MC_DELAY_100NS(const uint8_t count) {
    if (count == 0) [[unlikely]] {
        return;
    }

    const auto nopCount = HundredsOfNSToNOPCount(count);
    PushNOPs(nopCount);
}

extern "C" void LK2MC_DELAY_MICROS(const uint32_t duration) {
    if (duration == 0) [[unlikely]] {
        return;
    }

    // Maybe I should add a Command::DelayMicros to the firmware again to reduce
    // input spam - but then we'd need an input buffer on the FPGA to accumulate
    // commands while the wait is in progress.

    const auto nopCount = HundredsOfNSToNOPCount(static_cast<uint64_t>(duration) * 10);
    PushNOPs(nopCount);
}

extern "C" void LK2MC_CART_ENABLE(const uint8_t enable) {
    auto& cq = CommandQueue::get();
    cq.push(Command::SetCartPower, enable == 1);
    cq.flush(nullptr, 0);
}

extern "C" uint8_t LK2MC_CART_PRESENCE_SWITCH_GET() {
    auto& cq = CommandQueue::get();
    cq.push(Command::GetStateBits);

    uint8_t value;
    cq.flush(&value, 1);

    static constexpr auto cmp = std::to_underlying(StateBits::CartPresent);
    return (value & cmp) == cmp;
}

extern "C" void LK2MC_SET_PIN(const uint8_t pin, const uint8_t high) {
    const auto push = [high](const Command cmd, const auto mcPin) {
        const auto mask = std::to_underlying(mcPin);
        const auto sel = (mask << 4);
        const auto value = high ? mask : 0;
        CommandQueue::get().push(cmd, (sel | value) & 0xFF);
    };
    const auto a = [&push](const SetPinsA mcPins) { push(Command::SetPinsA, mcPins); };
    const auto b = [&push](const SetPinsB mcPins) { push(Command::SetPinsB, mcPins); };
    switch (pin) {
    case PIN_CLK: a(SetPinsA::CLK); break;
    case PIN_WR: a(SetPinsA::WR); break;
    case PIN_RD: a(SetPinsA::RD); break;
    case PIN_CS: a(SetPinsA::CS); break;
    case LK2MC_PIN_A15: b(SetPinsB::A15); break;
    case PIN_CS2: b(SetPinsB::RST); break; // CS2 is AGB name for RST pin
    case PIN_AUDIO: b(SetPinsB::AUDIO); break;
    default:
        LogError("LK2MC_SET_PIN called with invalid pin {}", pin);
        abort();
    }
}

extern "C" void LK2MC_OUTPUT_ENABLE(const uint8_t tristate_pin, const uint8_t oe) {
    CommandQueue::get().push(
        Command::SetOutputEnable,
        (1 << (tristate_pin + 4)) | (oe << tristate_pin));
}

extern "C" void LK2MC_SET_ADDR_PIN(uint8_t pin, uint8_t high) {
    if (pin != 15) [[unlikely]] {
        LogError("SET_ADDR_PIN called with pin != 15 ({})", pin);
        return;
    }
    LK2MC_SET_PIN(LK2MC_PIN_A15, high);
}

extern "C" void LK2MC_DMG_ADDR_SET(const uint16_t address) {
    auto& cq = CommandQueue::get();
    cq.push(Command::SetAddressMSB, address >> 8);
    cq.push(Command::SetAddressLSB, address & 0xff);
}

extern "C" void LK2MC_DMG_DATA_SET(const uint8_t data) {
    CommandQueue::get().push(Command::SetData, data);
}

extern "C" void mc_begin_async_batch() {
    CommandQueue::get().start_batch();
}

extern "C" void mc_end_async_batch() {
    CommandQueue::get().end_batch();
}

extern "C" void LK2MC_lk_recv_from_host(uint8_t* const data, const uint16_t count) {
    CommandQueue::get().flush(nullptr, 0);
    lk_recv_from_host(data, count);
}

extern "C" void LK2MC_lk_send_to_host(const uint8_t* const data, const uint16_t count) {
    CommandQueue::get().flush(nullptr, 0);
    lk_send_to_host(data, count);
}
