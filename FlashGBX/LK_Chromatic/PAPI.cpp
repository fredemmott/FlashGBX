extern "C" {
#define LK_DEVICE_NO_DPRINT
#include "LK.h"
}

#include "MC_impl_common.hpp"
#include "PAPI.hpp"

#include <array>
#include <format>
#include <thread>

#ifdef _WIN32
TRACELOGGING_DEFINE_PROVIDER(
    gTL,
    "LK-MC",
    (0x72b32b32, 0x28c9, 0x4298, 0xa4, 0x84, 0xc3, 0x85, 0xdd, 0xda, 0xa2, 0x1f));
#endif

namespace {

PAPIStringCallback PAPI_OnError = nullptr;

template<std::size_t N>
struct ContiguousSPSCStream {
    ContiguousSPSCStream(const ContiguousSPSCStream&) = delete;
    ContiguousSPSCStream(ContiguousSPSCStream&&) = delete;
    ContiguousSPSCStream& operator=(const ContiguousSPSCStream&) = delete;
    ContiguousSPSCStream& operator=(ContiguousSPSCStream&&) = delete;

    explicit ContiguousSPSCStream(const char* const label) : _label(label) {
    }

    template<std::invocable<uint8_t*, std::size_t> Fn>
    void write(const std::size_t count, Fn&& f) {
        SPAMMY(TraceLoggingThreadActivity<gTL> tla);
        SPAMMY(TraceLoggingWriteStart(tla, "Stream::write()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label")));

        const auto offset = _writePos.load(std::memory_order_relaxed);
        std::invoke(std::forward<Fn>(f),_buffer.data() + offset, count);
        _writePos.store(offset + count, std::memory_order_release);
        _writePos.notify_one();
        SPAMMY(TraceLoggingWriteStop(tla, "Stream::write()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label")));
    }

    void read(uint8_t* const dest, const std::size_t count) {
        // can't be cancelled if we don't have a stop_token
        std::ignore = read(dest, count, {});
    }

    [[nodiscard]]
    bool read(uint8_t* const dest, const std::size_t count, const std::stop_token& cancel) {
        SPAMMY(TraceLoggingThreadActivity<gTL> tla);
        SPAMMY(TraceLoggingWriteStart(tla, "Stream::read()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label")));

        const auto offset = _readPos.load(std::memory_order_relaxed);

        const std::stop_callback stopCallback{cancel, [this] {
            _writePos.notify_all();
        }};

        {
            std::size_t writeOff {};

            SPAMMY(TraceLoggingThreadActivity<gTL> tlb);
            SPAMMY(TraceLoggingWriteStart(tlb, "Stream::read()/wait"));
            while (true) {
                if (cancel.stop_requested()) {
                    SPAMMY(TraceLoggingWriteStop(tlb, "Stream::read()/wait", TraceLoggingValue("stopped", "result")));
                    break;
                }

                writeOff = _writePos.load(std::memory_order_acquire);
                if (writeOff - offset >= count) {
                    SPAMMY(TraceLoggingWriteStop(tlb, "Stream::read()/wait", TraceLoggingValue("OK", "result")));
                    break;
                }

                _mm_pause();

                //_writePos.wait(writeOff, std::memory_order_release);
            }
        }

        std::memcpy(dest, _buffer.data() + offset, count);
        _readPos.store(_readPos, std::memory_order_relaxed);
        _readPos += count;
        if (_readPos == _writePos) {
            _readPos.store(0, std::memory_order_relaxed);
            _writePos.store(0, std::memory_order_release);
        }

        SPAMMY(TraceLoggingWriteStop(tla, "Stream::read()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label")));

        return true;
    }

    [[nodiscard]]
    std::size_t pending_count() const {
        return _writePos.load(std::memory_order_acquire) - _readPos.load(std::memory_order_acquire);
    }
private:
    std::array<uint8_t, N> _buffer {};

    std::atomic<std::size_t> _readPos {};
    std::atomic<std::size_t> _writePos {};

    const char* const _label;
};

ContiguousSPSCStream<65536> fromFlashGBX("from-FlashGBX");
ContiguousSPSCStream<65536> toFlashGBX("to-FlashGBX");;

[[nodiscard]]
uint8_t GetPingCookie() {
    LARGE_INTEGER ret;
    QueryPerformanceCounter(&ret);

    // Fibonacci Hashing (TAOCP vol 3)
    // Magic number approach to 1/golden ratio from RC5
    return (ret.QuadPart * 0x9E3779B97F4A7C15ULL) >> 56;
}

}

extern "C" LK_CHROMATIC_EXPORT void papi_recv_from_lk(uint8_t* data, const uint16_t count) {
    if (count == 0) {
        return;
    }

    SPAMMY(TraceLoggingThreadActivity<gTL> tla);
    SPAMMY(TraceLoggingWriteStart(tla, "papi_recv_from_lk()"));


    toFlashGBX.read(data, count);

    SPAMMY(TraceLoggingWriteStop(tla, "papi_recv_from_lk()"));
}

extern "C" LK_CHROMATIC_EXPORT void papi_send_to_lk(uint8_t* data, const uint16_t count) {
    if (count == 0) {
        return;
    }

    SPAMMY(TraceLoggingThreadActivity<gTL> tla);
    SPAMMY(TraceLoggingWriteStart(tla, "papi_send_to_lk()", TraceLoggingValue(count, "count")));

    static std::atomic_flag haveWorker {};
    if (!haveWorker.test_and_set()) {
        std::jthread {
            [] (const std::stop_token& stop) {
                SET_THREAD_NAME("LK -> Microcode worker");

                while (!stop.stop_requested()) {
                    uint8_t cmd {};
                    {
                        if (!fromFlashGBX.read(&cmd, 1, stop)) {
                            haveWorker.clear();
                            UNSET_THREAD_NAME();
                            return;
                        }
                    }
                    TraceLoggingThreadActivity<gTL> tla;
                    TraceLoggingWriteStart(tla, "lk_loop()", TraceLoggingHexInt8(cmd, "cmd"));
                    mc_begin_async_batch();
                    lk_loop(cmd);
                    mc_end_async_batch();
                    TraceLoggingWriteStop(tla, "lk_loop()", TraceLoggingHexInt8(cmd, "cmd"));
                }
            }
        }.detach();
    }

    fromFlashGBX.write(count, [src = data](uint8_t* const dst, const std::size_t n) {
        std::memcpy(dst, src, n);
    });

    SPAMMY(TraceLoggingWriteStop(tla, "papi_send_to_lk()", TraceLoggingValue(count, "count")));
}


extern "C" LK_CHROMATIC_EXPORT void papi_open(uint16_t vendorID, uint16_t productID, uint8_t interfaceNumber) {
    dprint("Attempting to open libusb device");
    mc_usb_open(vendorID, productID, interfaceNumber);

    // Doesn't need to be timestamp, just want to make sure that the response isn't hardcoded
    const auto cookie = GetPingCookie();
    const auto expected = (~cookie) & 0xff;

    dprint("Sending ping: {:#04x} -> {:#04x}", cookie, expected);
    mc_begin_async_batch();
    const auto actual = LK2MC_ping(cookie);
    mc_end_async_batch();
    if (actual != expected) {
        LogError("Ping response command mismatch - received {:#04x}, expected {:#04x}", actual, expected);
        return;
    }
    dprint("LK_Chromatic: Initial ping OK");
}

extern "C" LK_CHROMATIC_EXPORT void papi_close() {
    mc_usb_close();
}

extern "C" LK_CHROMATIC_EXPORT void papi_set_on_error_callback(PAPIStringCallback cb) {
    PAPI_OnError = cb;
}

extern "C" void mc_on_debug_message(const char* const str, const std::size_t length) {
    TraceLoggingWrite(gTL, "dprint", TraceLoggingCountedString(str, length, "message"));

    const auto ds = std::format("{}\n", std::string_view { str, length });
    OutputDebugStringA(ds.c_str());
}

extern "C" void mc_on_error(const char* const str, const std::size_t length) {
    TraceLoggingWrite(gTL, "ERROR", TraceLoggingCountedString(str, length, "message"));

    const auto ds = std::format("ERROR: {}\n", std::string_view { str, length });
    OutputDebugStringA(ds.c_str());

    if (PAPI_OnError) {
        const auto fgbx = std::format("LK-MC: {}\r\n", std::string_view { str, length });
        PAPI_OnError(fgbx.c_str(), fgbx.length());
    }
}

extern "C" void lk_send_to_host(const uint8_t* data, const uint16_t count) {
    toFlashGBX.write(count, [src = data](uint8_t* const dest, const std::size_t n) {
        std::memcpy(dest, src, n);
    });
}

extern "C" void lk_recv_from_host(uint8_t* data, const uint16_t count) {
    fromFlashGBX.read(data, count);
}

#ifdef _WIN32
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
#endif
