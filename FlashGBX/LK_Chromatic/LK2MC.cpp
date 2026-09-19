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
    Delay = 2,
    Flush = 3,

    SetAddressMSB = 4,
    SetAddressLSB = 5,
    SetOutputEnable = 6,
    SetData = 7,
    GetData = 8,
    SetPinsA = 9,
    SetPinsB = 10,
    VerifyData = 11,
    VerifyStatusRegister = 12,
    SetStatusRegisterMask = 13,
    SetStatusRegisterValue = 14,
    GetStateBits = 15,
};

enum class StateBits : uint8_t {
    CartPresent = 1 << 0,
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
constexpr bool ProducesRX(const Command cmd) noexcept {
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
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&& other) noexcept { this->moveFrom(std::move(other)); }
    CommandBuffer& operator=(CommandBuffer&& other) noexcept {
        this->moveFrom(std::move(other));
        return *this;
    }

    explicit CommandBuffer(const std::size_t initialCapacity) {
        _begin = _end = static_cast<uint8_t*>(std::malloc(initialCapacity));
        _capacity = initialCapacity;
    }

    CommandBuffer() : CommandBuffer(65536) {
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

    [[nodiscard]]
    std::size_t capacity() const noexcept {
        return _capacity;
    }

    void reset(const std::size_t capacity) {
        std::free(_begin);
        _begin = _end = static_cast<uint8_t*>(std::malloc(capacity));
        _capacity = capacity;
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

    void pop() {
        _end -= BytesPerCommand;
    }

    void clear() {
        _end = _begin;
    }

    [[nodiscard]]
    uint8_t* data() { return _begin; }
    [[nodiscard]]
    std::size_t byte_count() const { return _end - _begin; }

    [[nodiscard]]
    uint8_t* begin() const noexcept { return _begin; }

    [[nodiscard]]
    uint8_t* end() const noexcept { return _end; }

private:

    uint8_t* _begin {};
    uint8_t* _end {};
    std::size_t _capacity {};

    void ensureCanAppend(const std::size_t required) {
        if (const auto s = byte_count(); s + required > _capacity) {
            const auto newCapacity = std::max<std::size_t>(s + required, _capacity * 2);
            _begin = static_cast<uint8_t*>(std::realloc(_begin, newCapacity));
            _capacity = newCapacity;
            _end = _begin + s;
        }
    }

    void moveFrom(CommandBuffer&& other) {
        std::free(_begin);
        _begin = std::exchange(other._begin, nullptr);
        _end = std::exchange(other._end, nullptr);
        _capacity = std::exchange(other._capacity, 0);
    }
};

struct CommandQueue {
    [[nodiscard]] uint8_t* begin() const noexcept {
        return _buffer.begin();
    }

    [[nodiscard]] uint8_t* end() const noexcept {
        return _buffer.end();
    }

    [[nodiscard]]
    std::tuple<Command, uint8_t> last() const noexcept {
        if (end() - begin() < BytesPerCommand) [[unlikely]] {
            LogError("CommandQueue::last() called with less than 2 bytes");
            abort();
        }
        static_assert(BytesPerCommand == 2);
        return {static_cast<Command>(_buffer.end()[-2]), _buffer.end()[-1]};
    }

    void pop() {
        _buffer.pop();
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

        LK_ASYNC_FLUSH(nullptr, 0);
    }

    void push(const Command cmd, const uint8_t arg = 0x00) {
        _buffer.push(cmd, arg);
        if (ProducesRX(cmd)) {
            _producesRX = true;
        }
    }

    template<std::invocable<uint8_t*, std::size_t> Fn>
    void pushBytes(const std::size_t count, Fn&& fn) {
        const auto offset = byte_count();
        _buffer.pushBytes(count, std::forward<Fn>(fn));
        for (auto it = begin() + offset; it != end(); it += BytesPerCommand) {
            if (ProducesRX(static_cast<Command>(*it))) {
                _producesRX = true;
            }
        }
    }

    struct Instruction {
        Command cmd;
        uint8_t arg8;
    };
    static_assert(sizeof(Instruction) == BytesPerCommand);

    template<std::size_t N>
    void append(const Instruction (&instructions)[N]) {
        static_assert(sizeof(instructions) == N * BytesPerCommand);
        pushBytes(N * BytesPerCommand, [src = &instructions](uint8_t* const p, std::size_t) {
            std::memcpy(p, src, N * BytesPerCommand);
        });
    }


    void flush();

    [[nodiscard]] std::size_t byte_count() const noexcept { return _buffer.byte_count(); }

    [[nodiscard]] bool empty() const noexcept { return byte_count() == 0; }

    static CommandQueue& get() {
        static CommandQueue instance {};
        return instance;
    }

    void on_tx_progress(const mc_transport_progress& p) {
        while ((!_busy.empty()) && _busy.front().fence <= p.completed_cumulative) {
            _busy.pop_front();
        }
    }
private:
    struct SubmittedBuffer {
        CommandBuffer buffer {0};
        std::size_t fence {};
    };

    CommandBuffer _buffer {};

    bool _enabled { false };
    bool _producesRX { false };

    std::list<SubmittedBuffer> _busy;

    CommandQueue() = default;
};

void tx_progress_callback(
    [[maybe_unused]] void* user_data,
    const mc_transport_progress* p) {
    CommandQueue::get().on_tx_progress(*p);
}

constexpr uint8_t HundredsOfNSToDelayArg(const uint8_t hundredsOfNS) {
    // The microcode is executed using the USB clock as the execution
    // clock - so byte count == tick count.
    //
    // Our clock interval is 16.6667ns, so 100ns is 6 ticks. Our
    // commands are currently 2 bytes == 2 ticks, so 33.333ns.
    //
    // If we have `foo(); bar(); baz();` though, we have a 4 tick delay
    // between `foo()` and `baz()`:
    //
    //     foo(); bar(); baz();
    //     |------| 2 bytes = 2 ticks
    //            |------| 2 bytes = 2 ticks
    //     |-------------| 4 bytes = 4 ticks
    //
    // So, even `delay(0)` gets 33.3333ns just by virtue of being a command
    // and arg (with BytesPerCommand = 2)
    //
    // Incrementing the argument gets us an extra 16.6667ns
    //
    // So, for 100ns, we want a delay of (100 - 33.333) / 16.6667 = 4
    //         200ns                     (200 - 33.333) / 16.6667 = 10
    //         ...                       ...                      = ...
    //
    // given `n` is hundreds of NS, that is:
    //
    //     (100n - (100 / 3)) / (100 / 6)
    //     ...  (n - (1 / 3)) / (1 / 6)
    //     ...  (6n - 2)
    static_assert(BytesPerCommand == 2);
    return (6 * hundredsOfNS) - 2;
}
// We want to maximize n where
//   (6n - 2) <= 255
//   ... 6n <= 257
//   ... n <= 257 / 6
//   ... n <= 42.8333
// So with int math, n = 42
constexpr auto MaxHundredsOfNSDelay = 42;

struct output_enable_state_t {
    std::optional<bool> audio;
    std::optional<bool> data;
    std::optional<bool> address;
};

output_enable_state_t& output_enable_state() {
    static output_enable_state_t state;
    return state;
}

} // namespace

extern "C" uint8_t LK2MC_ping(const uint8_t cookie) {
    CommandQueue::get().push(Command::Ping, cookie);
    uint8_t ret {};
    LK_ASYNC_ENQUEUE_RX(&ret, 1);
    LK_ASYNC_FLUSH(nullptr, 0);
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

    LK_ASYNC_FLUSH(buffer, count);
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

extern "C" void LK2MC_enqueue_rx(uint8_t* const data, const uint16_t len) {
    if (!(data && len)) {
        return;
    }
    std::memset(data, 0xA0, len); // arbitrary marker
    CommandQueue::get().flush();
    std::ignore = mc_transport_enqueue_rx(data, len);
}

extern "C" void LK2MC_flush(uint8_t* const data, const uint16_t len) {
    CommandQueue::get().flush();
    LK_ASYNC_ENQUEUE_RX(data, len);
    mc_transport_flush();
}

void CommandQueue::flush() {
    if (empty()) {
        return;
    }
    if (std::exchange(_producesRX, false)) {
        push(Command::Flush);
    }
    const auto txCount = _buffer.byte_count();

    const auto finishedAt = mc_transport_enqueue_tx(_buffer.data(), txCount);

    const auto capacity = _buffer.capacity();
    _busy.emplace_back(std::move(_buffer), finishedAt);
    _buffer.reset(capacity);
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

    CommandQueue::get().push(Command::Delay, HundredsOfNSToDelayArg(count));
}

extern "C" void LK2MC_DELAY_MICROS(const uint32_t duration) {
    if (duration == 0) [[unlikely]] {
        return;
    }

    const auto hundredsOfNS = static_cast<uint64_t>(duration) * 10;
    const auto completeDelays = hundredsOfNS / MaxHundredsOfNSDelay;
    const auto remainder = hundredsOfNS % MaxHundredsOfNSDelay;

    auto& cq = CommandQueue::get();
    for (int i = 0; i < completeDelays; ++i) {
        cq.push(Command::Delay, MaxHundredsOfNSDelay);
    }
    if (remainder) {
        cq.push(Command::Delay, HundredsOfNSToDelayArg(remainder));
    }
}

extern "C" uint8_t LK2MC_CART_PRESENCE_SWITCH_GET() {
    auto& cq = CommandQueue::get();
    cq.push(Command::GetStateBits);

    uint8_t value;
    LK_ASYNC_ENQUEUE_RX(&value, 1);
    LK_ASYNC_FLUSH(nullptr, 0);

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

    if (!high) {
        return;
    }

    // Insert a delay after each write to hold the pin high for a little bit;
    // doing flash writes - even ID checks - back to back is too fast for many
    // cartridges, e.g. FunnyPlaying MidnightTrace/EverSave

    if (
        const auto we = _lk_var8[LK_VAR8_FLASH_WE_PIN];
        (we == LK_FLASH_WE_PIN_WR && pin == PIN_WR)
        || (we == LK_FLASH_WE_PIN_AUDIO && pin == PIN_AUDIO)
        || (we == LK_FLASH_WE_PIN_WR_RESET && pin == PIN_CS2)
    ) {
        _delay_200ns();
    }
}

extern "C" void LK2MC_OUTPUT_ENABLE(const uint8_t tristate_pin, const uint8_t oe_uint) {
    const auto oe = static_cast<bool>(oe_uint);
    switch (tristate_pin) {
    case TRISTATE_AUDIO:
        if (output_enable_state().audio == oe) {
            return;
        }
        output_enable_state().audio = oe;
        break;
    case TRISTATE_DATA:
        if (output_enable_state().data == oe) {
            return;
        }
        output_enable_state().data = oe;
        break;
    case TRISTATE_ADDRESS:
        if (output_enable_state().address == oe) {
            return;
        }
        output_enable_state().address = oe;
        break;
    default:
        break;
    }

    auto& cq = CommandQueue::get();
    const auto data_in_delays =
        (tristate_pin == TRISTATE_DATA)
        && (!oe)
        && (cq.byte_count() >= BytesPerCommand)
        && (cq.last() == std::tuple {Command::SetData, 0});
    if (data_in_delays) {
        cq.pop();
        _delay_200ns();
    }
    CommandQueue::get().push(
        Command::SetOutputEnable,
        (1 << (tristate_pin + 4)) | (oe << tristate_pin));
    if (data_in_delays) {
        // Minimum for ModRetro with 39VF1681, e.g. Centipede
        _delay_300ns();
    }
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
    cq.append({
        {Command::SetAddressMSB, static_cast<uint8_t>(address >> 8)},
        {Command::SetAddressLSB, static_cast<uint8_t>(address & 0xff)},
    });
}

extern "C" void LK2MC_DMG_DATA_SET(const uint8_t data) {
    CommandQueue::get().push(Command::SetData, data);
}

extern "C" void mc_exec(const uint8_t command) {
    auto& cq = CommandQueue::get();
    cq.start_batch();
    lk_loop(command);
    cq.end_batch();
}

extern "C" uint8_t mc_standalone_ping(const uint8_t cookie) {
    return LK2MC_ping(cookie);
}

extern "C" void mc_init() {
    const mc_transport_callbacks callbacks {
        .on_tx_progress = &tx_progress_callback,
    };
    mc_transport_set_callbacks(&callbacks);

    output_enable_state() = {};
}

extern "C" void mc_reset() {
    mc_transport_set_callbacks(nullptr);
}

extern "C" void LK2MC_lk_recv_from_host(uint8_t* const data, const uint16_t count) {
    LK_ASYNC_FLUSH(nullptr, 0);
    lk_recv_from_host(data, count);
}

extern "C" void LK2MC_lk_send_to_host(const uint8_t* const data, const uint16_t count) {
    LK_ASYNC_FLUSH(nullptr, 0);
    lk_send_to_host(data, count);
}