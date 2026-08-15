extern "C" {
    #include "LK_device.h"
}

#include <chrono>
#include <thread>

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
    return 0xFF;
}

extern "C" void LK_Chromatic_DELAY_NANOS(const uint16_t duration) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(duration));
}

extern "C" void LK_Chromatic_DELAY_MICROS(const uint16_t duration) {
    std::this_thread::sleep_for(std::chrono::microseconds(duration));
}

extern "C" void LK_Chromatic_SET_PIN(uint8_t pin, uint8_t high) {
}

extern "C" void LK_Chromatic_OUTPUT_ENABLE(uint8_t tristate_pin, uint8_t oe) {
}

extern "C" void LK_Chromatic_SET_ADDR_PIN(uint8_t pin, uint8_t high) {
    // TODO: raise error if pin != 15
}

extern "C" void LK_Chromatic_DMG_ADDR_SET(const uint16_t address) {
}

extern "C" void LK_Chromatic_DMG_DATA_SET(const uint8_t data) {
}

extern "C" void LK_Chromatic_CONN_SEND(uint8_t* data, uint16_t count) {
}

extern "C" void LK_Chromatic_CONN_RECV(uint8_t* data, uint16_t count) {
}
