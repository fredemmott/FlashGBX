// Python API (PAPI)

#include <cstdint>

extern "C" {

using LK_Chromatic_data_callback = void (*)(uint8_t*, uint16_t);

#define LK_CHROMATIC_EXPORT __declspec(dllexport)

LK_CHROMATIC_EXPORT void papi_set_send_to_flashgbx_callback(LK_Chromatic_data_callback);
LK_CHROMATIC_EXPORT void papi_set_recv_from_flashgbx_callback(LK_Chromatic_data_callback);

LK_CHROMATIC_EXPORT void papi_set_send_to_device_callback(LK_Chromatic_data_callback);
LK_CHROMATIC_EXPORT void papi_set_recv_from_device_callback(LK_Chromatic_data_callback);

LK_CHROMATIC_EXPORT void papi_set_on_error_callback(LK_Chromatic_data_callback);

LK_CHROMATIC_EXPORT void papi_entrypoint(uint8_t command);

}