// Python API (PAPI)

#include <cstdint>

extern "C" {

using PAPIStringCallback = void (*)(const char*, uint16_t);

#define LK_CHROMATIC_EXPORT __declspec(dllexport)

LK_CHROMATIC_EXPORT void papi_flashgbx_write(uint8_t*, uint16_t);
LK_CHROMATIC_EXPORT void papi_flashgbx_read(uint8_t*, uint16_t);

LK_CHROMATIC_EXPORT void papi_open(uint16_t vendorID, uint16_t productID, uint8_t interfaceNumber);
LK_CHROMATIC_EXPORT void papi_close();

LK_CHROMATIC_EXPORT void papi_set_on_error_callback(PAPIStringCallback);

}