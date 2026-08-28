#include "MC_impl_common.hpp"

// Initialize TraceLogging; useful with tools like Instant Trace Viewer or
// Windows Performance Analyzer (WPA)

TRACELOGGING_DEFINE_PROVIDER(
    gTL,
    "LK-MC",
    (0x72b32b32, 0x28c9, 0x4298, 0xa4, 0x84, 0xc3, 0x85, 0xdd, 0xda, 0xa2, 0x1f));

BOOL WINAPI DllMain(HINSTANCE, const DWORD fdwReason, LPVOID lpvReserved) {
  switch (fdwReason) {
  case DLL_PROCESS_ATTACH:
    TraceLoggingRegister(gTL);
    break;
  case DLL_PROCESS_DETACH:
    if (lpvReserved != nullptr) {
      // No cleanup on process exit
      break;
    }
    TraceLoggingUnregister(gTL);
    break;
  default: break;
  }
  return TRUE;
}
