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

LK_CHROMATIC_EXPORT void papi_open(uint16_t vendorID, uint16_t productID, uint8_t interfaceNumber);
LK_CHROMATIC_EXPORT void papi_close();

LK_CHROMATIC_EXPORT void papi_send_to_lk(uint8_t*, uint16_t);
LK_CHROMATIC_EXPORT void papi_send_to_lk_reset_output_buffer();
LK_CHROMATIC_EXPORT void papi_send_to_lk_flush();

LK_CHROMATIC_EXPORT void papi_recv_from_lk(uint8_t*, uint16_t);
LK_CHROMATIC_EXPORT void papi_recv_from_lk_reset_input_buffer();
LK_CHROMATIC_EXPORT uint16_t papi_recv_from_lk_pending_count();

LK_CHROMATIC_EXPORT void papi_set_on_error_callback(PAPIStringCallback);
LK_CHROMATIC_EXPORT void papi_set_on_debug_message_callback(PAPIStringCallback);

}