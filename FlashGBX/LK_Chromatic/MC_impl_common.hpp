#ifndef _FLASHGBX_NATIVE_IMPL_COMMON_HPP
#define _FLASHGBX_NATIVE_IMPL_COMMON_HPP

extern "C" {
#include "MC_transport.h"
}

#include <format>

#if __has_include(<windows.h>)
#include <Windows.h>
#include <TraceLoggingProvider.h>
#include <TraceLoggingActivity.h>

#define SET_THREAD_NAME(x) {std::ignore = SetThreadDescription(GetCurrentThread(), L##x);}
#define UNSET_THREAD_NAME() {std::ignore = SetThreadDescription(GetCurrentThread(), L"");}

TRACELOGGING_DECLARE_PROVIDER(gTL);
#endif

// Anything other than 0 noticeably impacts performance, even if TraceLogging is disabled
#define SPAM_LEVEL 0

#if SPAM_LEVEL >= 1
#define SPAMMY(x) x;
#else
#define SPAMMY(x) {}
#endif

#if SPAM_LEVEL >= 2
#define SUPER_SPAMMY(x) x;
#else
#define SUPER_SPAMMY(x) {}
#endif

constexpr auto BytesPerCommand = 2;

template<class... Args>
void dprint(std::format_string<Args...> fmt, Args&&... args) {
  const auto s = std::vformat(fmt.get(), std::make_format_args(args...));
  mc_on_debug_message(s.c_str(), s.size());
}

template<class... Args>
void LogError(std::format_string<Args...> fmt, Args&&... args) {
  const auto s = std::vformat(fmt.get(), std::make_format_args(args...));
  mc_on_error(s.c_str(), s.size());
}

void mc_usb_open(uint16_t vendorID, uint16_t productID, uint8_t interfaceNumber);
void mc_usb_close();

#endif
