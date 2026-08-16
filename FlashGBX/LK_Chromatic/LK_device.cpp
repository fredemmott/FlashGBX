extern "C" {
    #include "LK_device.h"
}

#include "PAPI.hpp"

#include <array>
#include <chrono>
#include <format>
#include <functional>
#include <thread>

#if __has_include(<windows.h>)
#include <Windows.h>

static HANDLE mcHandle = nullptr;
static HANDLE mcEvent = nullptr;
#endif

template<std::size_t N>
class RingBuffer final {
public:
    void write(const void* data, std::size_t count) {
        const auto idx = _writeIndex.load(std::memory_order_relaxed);
        const auto firstCount = std::min(N - idx, count);

        if (firstCount) {
            memcpy(&_buffer[idx], data, firstCount);
        }
        if (firstCount != count) {
            memcpy(&_buffer[0], static_cast<const uint8_t*>(data) + firstCount, count - firstCount);
        }

        _writeIndex.store((idx + count) % N, std::memory_order_release);
        _writeIndex.notify_one();
    }

    void read(void* data, const std::size_t count) {
        std::size_t bytesRead = 0;
        const auto it = static_cast<uint8_t*>(data);

        while (bytesRead < count) {
            while (_readIndex == _writeIndex.load(std::memory_order_acquire)) {
                _writeIndex.wait(_readIndex, std::memory_order_relaxed);
            }
            const auto writeIdx = _writeIndex.load(std::memory_order_acquire);

            std::size_t available = 0;
            if (writeIdx >= _readIndex) {
                available = writeIdx - _readIndex;
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
    std::atomic<std::size_t> _writeIndex {};
};
RingBuffer<65536> fromFlashGBX;
RingBuffer<65536> toFlashGBX;

enum class Command : uint8_t {
    Ping = 0,
    DelayMicros = 1,
    DelayTicks = 2,
    SetPins = 3,
    SetOutputEnable = 4,
    SetAddress = 5,
    SetData = 6,
    GetData = 7
};

static LK_Chromatic_data_callback PAPI_OnError = nullptr;

template<class... Args>
void LogError(std::format_string<Args...> fmt, Args&&... args) {
    const auto s = std::vformat(fmt.get(), std::make_format_args(args...));
    PAPI_OnError((uint8_t*)s.data(), s.length());
}

static void MC_Write(const void*, uint16_t count);
static void MC_Read(void*, uint16_t count);

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

    auto submitted = std::min<uint16_t>(commandCount, 510);
    MC_Write(asyncBuffer, submitted * 3);
    auto pending = submitted;
    uint16_t read = 0;

    while (pending) {
        const auto to_read = std::min<uint16_t>(255, pending);
        MC_Read(responseBuffer + (read * 2), to_read * 2);
        read += to_read;
        pending -= to_read;

        if (submitted < commandCount) {
            const auto to_write = std::min<uint16_t>(to_read, commandCount - submitted);
            MC_Write(asyncBuffer + (submitted * 3), to_write * 3);
            pending += to_write;
            submitted += to_write;
        }
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
        MC_Write(buffer, 3);
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
    MC_Read(buffer, 2);
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

extern "C" void LK_Chromatic_DELAY_TICKS(const uint8_t ticks) {
    if (asyncEnabled) {
        SendToDevice16(Command::DelayTicks, ticks);
    } else if (ticks) {
        // While we don't have the same '2 ticks always' overhead, every command
        // - including the one after the delay - gets a bunch more going through
        // the USB stack and the FIFOs on the FPGA, so no correction needed
        std::this_thread::sleep_for(std::chrono::nanoseconds(ticks));
    }
}

extern "C" void LK_Chromatic_DELAY_MICROS(const uint16_t duration) {
    if (asyncEnabled) {
        SendToDevice16(Command::DelayMicros, duration);
    } else {
        std::this_thread::sleep_for(std::chrono::microseconds(duration));
    }
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
                    uint8_t cmd;
                    fromFlashGBX.read(&cmd, 1);
                    lk_loop(cmd);
                }
            }
        }.detach();
    }
}


extern "C" LK_CHROMATIC_EXPORT void papi_set_native_handle(void* handle) {
    mcHandle = handle;
    if (!mcEvent) {
        mcEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    }
}

extern "C" LK_CHROMATIC_EXPORT void papi_set_on_error_callback(LK_Chromatic_data_callback cb) {
    PAPI_OnError = cb;
}


///// Serial API /////

static void MC_Write(const void* const data, const uint16_t count) {
    if (count == 0) {
        return;
    }

    auto p = static_cast<const uint8_t*>(data);
    auto remaining = count;

    while (remaining) {
        OVERLAPPED o { .hEvent = mcEvent };
        WriteFile(mcHandle, p, remaining, nullptr, &o);
        DWORD transferred {};
        if (!GetOverlappedResult(mcHandle, &o, &transferred, TRUE)) [[unlikely]] {
            LogError("Failed to write to device - {} of {} bytes, result {}", transferred, count, GetLastError());
            return;
        }
        remaining -= transferred;
        p += transferred;
    }
}

static void MC_Read(void* const data, uint16_t const count) {
    if (count == 0) {
        return;
    }

    auto p = static_cast<uint8_t*>(data);
    auto remaining = count;

    while (remaining) {
        OVERLAPPED o { .hEvent = mcEvent };
        ReadFile(mcHandle, p, remaining, nullptr, &o);
        DWORD transferred {};
        if (!GetOverlappedResult(mcHandle, &o, &transferred, TRUE)) [[unlikely]] {
            LogError("Failed to read from device - {} of {} bytes, result {}", transferred, count, GetLastError());
            return;
        }
        remaining -= transferred;
        p += transferred;
    }
}