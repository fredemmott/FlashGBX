// Python API (PAPI)

#include <cstdint>

extern "C" {

using LK_Chromatic_data_callback = void (*)(uint8_t*, uint16_t);

#define LK_CHROMATIC_EXPORT __declspec(dllexport)

LK_CHROMATIC_EXPORT void papi_flashgbx_write(uint8_t*, uint16_t);
LK_CHROMATIC_EXPORT void papi_flashgbx_read(uint8_t*, uint16_t);

LK_CHROMATIC_EXPORT void papi_set_native_handle(void*);

LK_CHROMATIC_EXPORT void papi_set_on_error_callback(LK_Chromatic_data_callback);

}