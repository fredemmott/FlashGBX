// Python API (PAPI)
// a.k.a the FlashGBX interface/implementation
#pragma once

#include <cstdint>

#ifdef _WIN32
#define LK_CHROMATIC_EXPORT __declspec(dllexport)
#else
#define LK_CHROMATIC_EXPORT
#endif

extern "C" {

using PAPIStringCallback = void (*)(const char*, uint16_t);

enum class papi_open_status: int {
  Success = 0,
  OpenError = -1,
  PingError = -2,
};

[[nodiscard]]
LK_CHROMATIC_EXPORT papi_open_status papi_open(uint16_t vendorID, uint16_t productID);
LK_CHROMATIC_EXPORT void papi_close();

// Returns 1 if open, 0 otherwise
[[nodiscard]]
LK_CHROMATIC_EXPORT int papi_is_open();

[[nodiscard]]
LK_CHROMATIC_EXPORT uint16_t papi_get_fw_info(uint8_t*, uint16_t);

LK_CHROMATIC_EXPORT void papi_send_to_lk(uint8_t*, uint16_t);
LK_CHROMATIC_EXPORT void papi_send_to_lk_reset_output_buffer();
LK_CHROMATIC_EXPORT void papi_send_to_lk_flush();

LK_CHROMATIC_EXPORT void papi_recv_from_lk(uint8_t*, uint16_t);
LK_CHROMATIC_EXPORT void papi_recv_from_lk_reset_input_buffer();
LK_CHROMATIC_EXPORT uint16_t papi_recv_from_lk_pending_count();

LK_CHROMATIC_EXPORT void papi_set_on_error_callback(PAPIStringCallback);
LK_CHROMATIC_EXPORT void papi_set_on_debug_message_callback(PAPIStringCallback);

}