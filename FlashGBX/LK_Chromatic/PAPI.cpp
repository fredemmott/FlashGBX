#include "Command.hpp"
extern "C" {
#define LK_DEVICE_NO_DPRINT
#include "LK.h"
}

#include "ContiguousRingBuffer.hpp"
#include "MC_impl_common.hpp"
#include "PAPI.hpp"

#include <algorithm>
#include <bit>
#include <format>
#include <functional>
#include <thread>
#include <stop_token>

#include <cstring>

namespace {

struct callbacks_t {
    PAPIStringCallback on_error { nullptr };
    PAPIStringCallback on_debug_message { nullptr };
};
callbacks_t& callbacks() {
    static callbacks_t callbacks;
    return callbacks;
}

template<std::size_t N>
requires (std::has_single_bit(N)) // must be power of two
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
        auto span = _buffer.subspan(offset, count);
        std::invoke(std::forward<Fn>(f), span.data(), count);
        _writePos.store(offset + count, std::memory_order_release);
        _writePos.notify_one();
        _eventSeq.fetch_add(1, std::memory_order_release);
        _eventSeq.notify_one();
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

        {
            std::size_t writeOff {};

            SPAMMY(TraceLoggingThreadActivity<gTL> tlb);
            SPAMMY(TraceLoggingWriteStart(tlb, "Stream::read()/wait"));
            const auto spinSince = std::chrono::steady_clock::now();
            std::size_t spinCount = 0;
            auto lastTimeoutCheckAt = spinSince;
            while (true) {
                if (cancel.stop_requested()) {
                    SPAMMY(TraceLoggingWriteStop(tlb, "Stream::read()/wait", TraceLoggingValue("stopped", "result")));
                    return false;
                }

                writeOff = _writePos.load(std::memory_order_acquire);
                if (writeOff - offset >= count) {
                    SPAMMY(TraceLoggingWriteStop(tlb, "Stream::read()/wait", TraceLoggingValue("OK", "result")));
                    break;
                }

                // Idea is to busy-spin while FlashGBX is actually doing something,
                // but not to busy-spin if we're waiting for:
                // - extremely slow operations like chip erase
                // - user input
                if ((++spinCount & _timeoutCheckMask) == 0) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now - spinSince >= std::chrono::milliseconds(10)) {
                        if (!this->wait_until_have_at_least_n_bytes(count, cancel)) {
                            return false;
                        }
                        break;
                    }
                    if (now - lastTimeoutCheckAt < std::chrono::microseconds(100)) {
                        _timeoutCheckMask = (_timeoutCheckMask << 1) | 1;
                    }
                    lastTimeoutCheckAt = now;
                }

#ifdef _WIN32
                // this_thread::yield() is *very* slow on Windows; using it
                // halves throughput...
                _mm_pause();
#else
                // ... but on macOS and Linux, yield is much faster and I didn't
                // notice a significant impact on throughput
                std::this_thread::yield();
#endif
            }
        }

        std::memcpy(dest, _buffer.subspan(offset, count).data(), count);
        _readPos.store(offset + count, std::memory_order_release);

        SPAMMY(TraceLoggingWriteStop(tla, "Stream::read()", TraceLoggingValue(count, "count"), TraceLoggingValue(_label, "label")));

        return true;
    }

    [[nodiscard]]
    std::size_t pending_count() const {
        return _writePos.load(std::memory_order_acquire) - _readPos.load(std::memory_order_acquire);
    }

    void clear_read_buffer() {
        const auto drainTo = _writePos.load(std::memory_order_acquire);
        _readPos.store(drainTo, std::memory_order_release);
    }

    void clear_write_buffer() {
        const auto truncateTo = _readPos.load(std::memory_order_acquire);
        _writePos.store(truncateTo, std::memory_order_release);
    }

    [[nodiscard]]
    bool wait_until_have_at_least_n_bytes(const std::size_t count, const std::stop_token& token) {
        const auto readPos = _readPos.load(std::memory_order_relaxed);

        auto seq = _eventSeq.load(std::memory_order_acquire);
        auto writePos = _writePos.load(std::memory_order_acquire);
        if (writePos - readPos >= count) {
            return true;
        }

        const std::stop_callback callback {
            token,
            [this] {
                _eventSeq.fetch_add(1, std::memory_order_release);
                _eventSeq.notify_one();
            }
        };

        while (writePos - readPos < count) {
            if (token.stop_requested()) {
                return false;
            }
            _eventSeq.wait(seq);
            seq = _eventSeq.load(std::memory_order_relaxed);
            writePos = _writePos.load(std::memory_order_acquire);
        }

        return true;
    }

private:
    // This provides:
    // - contiguous mapping regardless of offset, via mmap or similar tricks
    // - transparent modulo of offsets via `subspan()` so we can use
    //   perpetually incrementing counters for simplicity
    ContiguousRingBuffer _buffer { N };

    std::atomic<std::size_t> _readPos {};
    std::atomic<std::size_t> _writePos {};

    std::atomic<std::size_t> _eventSeq {};

    const char* const _label;

    std::size_t _timeoutCheckMask { 0xFFFF };
};

constexpr auto LargestDMGROM = 8 * 1024 * 1024;
struct Streams_t {
    ContiguousSPSCStream<LargestDMGROM> papi_to_lk { "PAPI-to-LK" };
    ContiguousSPSCStream<LargestDMGROM> lk_to_papi { "LK-to-PAPI" };
};
Streams_t& streams() {
    static Streams_t instance {};
    return instance;
}

struct Worker {
    Worker() {
        _thread = std::jthread { &Worker::thread_main };
    }
private:
    std::jthread _thread;

    static void thread_main(const std::stop_token& stop) {
        SET_THREAD_NAME("LK -> Microcode worker");

        while (!stop.stop_requested()) {
            uint8_t cmd {};
            if (!streams().papi_to_lk.read(&cmd, 1, stop)) {
                UNSET_THREAD_NAME();
                return;
            }
            SPAMMY(TraceLoggingThreadActivity<gTL> tla);
            SPAMMY(TraceLoggingWriteStart(tla, "mc_exec()", TraceLoggingHexInt8(cmd, "cmd")));
            mc_exec(cmd);
            SPAMMY(TraceLoggingWriteStop(tla, "mc_exec()", TraceLoggingHexInt8(cmd, "cmd")));
        }
    }
};
auto& worker() {
    static std::optional<Worker> worker;
    return worker;
}
void ensure_worker() {
    if (!worker()) {
        worker().emplace();
    }
}
}

extern "C" LK_CHROMATIC_EXPORT void papi_recv_from_lk(uint8_t* data, const uint16_t count) {
    if (count == 0) {
        return;
    }

    SPAMMY(TraceLoggingThreadActivity<gTL> tla);
    SPAMMY(TraceLoggingWriteStart(tla, "papi_recv_from_lk()"));


    streams().lk_to_papi.read(data, count);

    SPAMMY(TraceLoggingWriteStop(tla, "papi_recv_from_lk()"));
}

extern "C" LK_CHROMATIC_EXPORT void papi_send_to_lk(uint8_t* data, const uint16_t count) {
    if (count == 0) {
        return;
    }

    SPAMMY(TraceLoggingThreadActivity<gTL> tla);
    SPAMMY(TraceLoggingWriteStart(tla, "papi_send_to_lk()", TraceLoggingValue(count, "count")));

    streams().papi_to_lk.write(count, [src = data](uint8_t* const dst, const std::size_t n) {
        std::memcpy(dst, src, n);
    });

    ensure_worker();

    SPAMMY(TraceLoggingWriteStop(tla, "papi_send_to_lk()", TraceLoggingValue(count, "count")));
}


extern "C" LK_CHROMATIC_EXPORT int papi_open(
    const uint16_t vendorID,
    const uint16_t productID) {
    dprint("Attempting to open libusb device");
    if (const auto ret = mc_usb_open(vendorID, productID); ret != 0) {
        return ret;
    }

    std::ranges::fill(_lk_var8, 0);
    std::ranges::fill(_lk_var16, 0);
    std::ranges::fill(_lk_var32, 0);

    return static_cast<int>(papi_open_status::Success);
}

extern "C" LK_CHROMATIC_EXPORT void papi_close() {
    mc_usb_close();
}

extern "C" LK_CHROMATIC_EXPORT int papi_is_open() {
    return mc_usb_is_open() ? 1 : 0;
}

extern "C" LK_CHROMATIC_EXPORT void papi_set_on_error_callback(PAPIStringCallback cb) {
    callbacks().on_error = cb;
}

extern "C" LK_CHROMATIC_EXPORT void papi_set_on_debug_message_callback(PAPIStringCallback cb) {
    callbacks().on_debug_message = cb;
}

extern "C" void mc_on_debug_message(const char* const str, const std::size_t length) {

#ifdef _WIN32
    TraceLoggingWrite(gTL, "dprint", TraceLoggingCountedString(str, length, "message"));

    const auto ds = std::format("{}\n", std::string_view { str, length });
    OutputDebugStringA(ds.c_str());
#endif

    if (const auto cb = callbacks().on_debug_message) {
        cb(str, length);
    }
}

extern "C" void mc_on_error(const char* const str, const std::size_t length) {
#ifdef _WIN32
    TraceLoggingWrite(gTL, "ERROR", TraceLoggingCountedString(str, length, "message"));

    const auto ds = std::format("ERROR: {}\n", std::string_view { str, length });
    OutputDebugStringA(ds.c_str());
#endif

    if (const auto cb = callbacks().on_error) {
        cb(str, length);
    }
}

extern "C" void papi_send_to_lk_reset_output_buffer() {
    worker().reset();

    streams().papi_to_lk.clear_write_buffer();
}

extern "C" void papi_send_to_lk_flush() {
    while (streams().papi_to_lk.pending_count()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

extern "C" uint16_t papi_recv_from_lk_pending_count() {
    return streams().lk_to_papi.pending_count();
}

extern "C" void papi_recv_from_lk_reset_input_buffer() {
    worker().reset();

    streams().lk_to_papi.clear_read_buffer();
}

///// implement LK host IO functions using the PAPI buffers //////

extern "C" void lk_send_to_host(const uint8_t* data, const uint16_t count) {
    streams().lk_to_papi.write(count, [src = data](uint8_t* const dest, const std::size_t n) {
        std::memcpy(dest, src, n);
    });
}


extern "C" void lk_recv_from_host(uint8_t* data, const uint16_t count) {
    streams().papi_to_lk.read(data, count);
}

extern "C" uint16_t papi_get_fw_info(uint8_t* const buffer, const uint16_t count) {
    if (!(buffer && count)) {
        return 0;
    }
    static constexpr uint8_t GetSize [] {
        static_cast<uint8_t>(Command::GetFWInfo),
        0,
        static_cast<uint8_t>(Command::Flush),
        0,
    };

    buffer[0] = 0;
    std::ignore = mc_transport_enqueue_tx(GetSize, sizeof(GetSize), 10 /* ms */);
    std::ignore = mc_transport_enqueue_rx(buffer, 1, 10 /* ms */);
    mc_transport_flush();

    const auto size = buffer[0];
    if (size == 0) {
        return 0;
    }

    // -1 because we already have the size byte
    const auto toRead = std::min<uint8_t>(size - 1, count - 1);
    if (toRead == 0) {
        return 1;
    }

    std::basic_string<uint8_t> commands;
    commands.resize_and_overwrite(
        2 * (toRead + 1), // +1 again to have space for Command::Flush
        [](uint8_t* const p, const std::size_t n) {
            for (std::size_t i = 0; i < n; i += 2) {
                p[i] = static_cast<uint8_t>(Command::GetFWInfo);
                p[i + 1] = (i / 2) + 1;
            }
            p[n - 2] = static_cast<uint8_t>(Command::Flush);
            return n;
        }
    );
    std::ignore = mc_transport_enqueue_tx(commands.data(), commands.size());
    std::ignore = mc_transport_enqueue_rx(buffer + 1, toRead);
    mc_transport_flush();
    return size;
}