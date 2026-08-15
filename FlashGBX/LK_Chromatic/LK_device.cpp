extern "C" {
    #include "LK_device.h"
}

#include "PAPI.hpp"

#include <chrono>
#include <format>
#include <thread>

enum class Command : uint8_t {
    Ping = 0,
    DelayMicros = 1,
    DelayNanos = 2,
    SetPins = 3,
    SetOutputEnable = 4,
    SetAddress = 5,
    SetData = 6,
    GetData = 7
};

static LK_Chromatic_data_callback
    PAPI_SendToFlashGBX = nullptr,
    PAPI_RecvFromFlashGBX = nullptr,
    PAPI_SendToDevice = nullptr,
    PAPI_RecvFromDevice = nullptr,
    PAPI_OnError = nullptr;

static void SendToDevice(const Command cmd, const uint8_t arg8a = 0, const uint8_t arg8b = 0) {
    uint8_t message[3];
    message[0] = static_cast<std::underlying_type_t<Command>>(cmd);
    message[1] = arg8a;
    message[2] = arg8b;
    PAPI_SendToDevice(message, 3);
}

static void SendToDevice16(const Command cmd, const uint16_t arg16) {
    SendToDevice(cmd, static_cast<uint8_t>(arg16 >> 8), static_cast<uint8_t>(arg16 & 0xFF));
}

template<class... Args>
void LogError(std::format_string<Args...> fmt, Args&&... args) {
    const auto s = std::vformat(fmt.get(), std::make_format_args(args...));
    PAPI_OnError((uint8_t*)s.data(), s.length());
}

template<Command T>
static [[nodiscard]] uint8_t RecvFromDevice() {
    static constexpr auto Expected = static_cast<uint8_t>(T);
    uint8_t buffer[2];
    PAPI_RecvFromDevice(buffer, 2);
    if (buffer[0] != Expected) [[unlikely]] {
        LogError("Response did not match command: expected '{}', got '{}'",
            Expected,
            buffer[0]
        );
        return 0;
    }
    return buffer[1];
}

template<Command T>
static void WaitForDevice() {
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

extern "C" void LK_Chromatic_DELAY_NANOS(const uint16_t duration) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(duration));
}

extern "C" void LK_Chromatic_DELAY_MICROS(const uint16_t duration) {
    std::this_thread::sleep_for(std::chrono::microseconds(duration));
}

extern "C" void LK_Chromatic_SET_PIN(uint8_t pin, uint8_t high) {
    SendToDevice(Command::SetPins, pin, high);
    WaitForDevice<Command::SetPins>();
}

extern "C" void LK_Chromatic_OUTPUT_ENABLE(uint8_t tristate_pin, uint8_t oe) {
    SendToDevice(Command::SetOutputEnable, oe);
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
    PAPI_SendToFlashGBX(data, count);
}

extern "C" void LK_Chromatic_CONN_RECV(uint8_t* data, uint16_t count) {
    PAPI_RecvFromFlashGBX(data, count);
}

///// Python API (PAPI) /////

extern "C" LK_CHROMATIC_EXPORT void papi_entrypoint(uint8_t command) {
    lk_loop(command);
}

#define CALLBACK(NAME, STORAGE) \
  extern "C" LK_CHROMATIC_EXPORT void papi_set_##NAME##_callback(LK_Chromatic_data_callback cb) { PAPI_##STORAGE = cb; }

CALLBACK(send_to_flashgbx, SendToFlashGBX)
CALLBACK(recv_from_flashgbx, RecvFromFlashGBX)
CALLBACK(send_to_device, SendToDevice)
CALLBACK(recv_from_device, RecvFromDevice)
CALLBACK(on_error, OnError)