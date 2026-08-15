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
    // TODO STUB
    return 0xFF;
}

extern "C" void LK_Chromatic_DELAY_NANOS(const uint16_t duration) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(duration));
}

extern "C" void LK_Chromatic_DELAY_MICROS(const uint16_t duration) {
    std::this_thread::sleep_for(std::chrono::microseconds(duration));
}