#include "mod/session2/sea_sync.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <string>

#include "common/clock.h"
#include "mod/game/build_profile.h"
#include "mod/game/hook_prepare.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/sea_sync_logic.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

// Build 1.0.0.3663 (read statically and live on 2026-11-16: docs/research/mar-3663.md).
//   0x3FCCF70 (sea, dt)  advances the trains: copies the previous frame of the history, adds each train's phase step,
//                        restarts one train at a time (its strength down, phase 0, the wind's direction, the camera
//                        as origin, its strength up again).
//   0x3FCE750 (sea)      copies four frames of that history into the static buffer exe+0x75AD350 (0x770 bytes,
//                        "OceanWaveConstantBuffer"); 0x3FCF410, its only caller, hands that buffer to the GPU.
// Build 1.0.0.3732 (static, 2026-11-21; never run; the values below are 3732's, the three lines above 3663's): the
// same two routines, each compared with its saved 3663 listing instruction by instruction: 0x3FD1460 (was 0x3FCCF70;
// all 551 instructions equal but for relative fields and the two tables' image addresses, now 0x597BA60 lengths and
// 0x597BAB0 multipliers) and 0x3FD2C40 (was 0x3FCE750; 0xCBC bytes as before, the 420 instructions of the saved
// listing equal but for relative fields, the rest had not been saved). The static buffer is exe+0x75B3590
// (was 0x75AD350); the callers are 0x3F2483C (advance) and 0x3FD3AE2 in 0x3FD3900 (fill, the only one).
// 3732 (static, 2026-11-21; never run): was 0x3FCE750; kFillHead's 24 bytes are there and nowhere else in the code
// section, a .pdata start; the sea object still comes in rcx (mov rcx,rdi at 0x3FD3ADF; mov rbx,rcx at 0x3FD2C85).
constexpr uintptr_t kFill = 0x3FD2C40;
constexpr std::array<uint8_t, 24> kFillHead = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x53, 0x56, 0x57, 0x41, 0x54,
                                               0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00};
constexpr size_t kFillPatch = 7;  // mov rax, rsp ; mov [rax+0x10], rdx
// 3732 (static, 2026-11-21; never run): the three offsets, the 16-byte records, the five slots and the twelve trains
// are unchanged: 0x3FD309E reads [sea+0x303C4], 0x3FD30A7 [sea+0x303E8], 0x3FD3124 [sea+0x303F8]; the advance writes
// [sea+slot*4+0x303F8] at 0x3FD178A, steps the slot modulo 5 (0x3FD15BF) and loops to 12 (cmp edi,0Ch, 0x3FD1947).
constexpr uint32_t kSeaSlot = 0x303C4;    // u32: the history frame being written, 0..4
constexpr uint32_t kSeaFrames = 0x303E8;  // -> records of 16 bytes; the first word of record n -> frame n (12 x 0x20)
constexpr uint32_t kSeaWind = 0x303F8;    // f32 per frame: the wind's strength for the swell, 0..1
constexpr uint32_t kSlots = 5;
// 3732 (static, 2026-11-21; never run): was 0x72451C0; the world system global, 3197 references (lead's anchor; vf178
// 0x5DF500 reads [[0x724B350]+0x30]+0x58).
constexpr uintptr_t kWorldSlot = 0x724B350;
constexpr uint64_t kSeaGoneUs = 2000000;  // the host: another sea object is taken once the one in use is not seen
constexpr uint64_t kFaultLimit = 50;      // writes or reads that faulted: the module stops touching the sea
constexpr size_t kMaxBacklog = 256 * 1024;  // the host does not add the sea to a tunnel this far behind
constexpr int kLogLines = 40;

std::atomic<bool> g_on{false};
bool g_guest = false;
uintptr_t g_base = 0;
SafetyHookInline g_hook;

// A lock nobody ever waits for: it is taken or it is not, and whoever does not get it does without (the next frame,
// the next pass of the loop). On 2026-11-16 these were SRWLOCKs, and the guest's network thread waited 22 s in
// AcquireSRWLockExclusive while the render thread kept taking and giving back the shared side eighty times a second
// with TryAcquireSRWLockShared; it came back the moment this module's own thread took the lock for its summary. The
// tunnel was lost meanwhile (the host: "the guest has been gone for 15 s"). No thread of the game or of the mod's
// network loop may ever wait here.
struct TryLock {
    std::atomic_flag taken;
    bool try_lock() noexcept { return !taken.test_and_set(std::memory_order_acquire); }
    void unlock() noexcept { taken.clear(std::memory_order_release); }
};

// Host: the newest frame (the render thread writes, the network thread reads).
TryLock g_cap_lock;
ssl::Sample g_cap;
std::atomic<uintptr_t> g_sea{0};
std::atomic<uint64_t> g_sea_seen_us{0};
uint32_t g_sent_seq = 0;  // the network thread only
uint64_t g_sent_us = 0;

// Guest: the newest sample (the network thread stores it, the render thread takes it), and the host's sea as the
// render thread has it (g_mirror_lock: a second sea object's frame on another thread does without).
struct Box {
    bool fresh = false;
    ssl::Sample s;
    uint64_t recv_us = 0, epoch = 0;
};
TryLock g_box_lock, g_mirror_lock;
Box g_box;
ssl::Mirror g_mirror;
uint64_t g_epoch = 0;
std::atomic<bool> g_writing{false};
// For the summary (the render thread writes them after each sample).
std::atomic<uint32_t> g_stat_seq{0};
std::atomic<bool> g_stat_has{false}, g_stat_moving{false};
std::atomic<double> g_stat_offset_us{0.0}, g_stat_rate_long{0.0}, g_stat_rate_short{0.0};

// What the module's thread says (the hook never logs).
struct Note {
    bool fresh = false;
    uintptr_t sea = 0, block = 0;
    uint32_t slot = 0;
    std::array<ssl::Band, ssl::kBands> own{}, host{};
    float own_wind = 0, host_wind = 0;
};
TryLock g_note_lock;
Note g_note_begin, g_note_first;
std::atomic<bool> g_first_noted{false};
std::atomic<uint64_t> c_frames{0}, c_captured{0}, c_sent{0}, c_refused{0}, c_other_sea{0}, c_bad_sea{0}, c_implausible{0};
std::atomic<uint64_t> c_rx{0}, c_rx_bad{0}, c_rx_old{0}, c_rx_busy{0}, c_written{0}, c_own{0}, c_busy{0}, c_faults{0},
    c_begins{0}, c_ends{0};

bool plausible_ptr(uintptr_t a) noexcept { return a >= 0x10000 && a <= 0x00007FFFFFFF0000ull; }

int av_filter(unsigned long code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER
                                                                              : EXCEPTION_CONTINUE_SEARCH;
}
bool seh_write(uintptr_t dst, const void* src, size_t n) noexcept {
    __try {
        std::memcpy(reinterpret_cast<void*>(dst), src, n);
        return true;
    } __except (av_filter(GetExceptionCode())) {
        return false;
    }
}

void fault() noexcept {
    if (c_faults.fetch_add(1, std::memory_order_relaxed) + 1 >= kFaultLimit) g_on.store(false, std::memory_order_release);
}

// The render thread, once per frame and sea object, right before the game copies the frame to the GPU's buffer.
void on_fill(uintptr_t sea) noexcept {
    c_frames.fetch_add(1, std::memory_order_relaxed);
    uint32_t slot = 0;
    uintptr_t frames = 0, block = 0;
    uint8_t raw[ssl::kFrameSize];
    float wind = 0;
    if (!plausible_ptr(sea) || !game::fast_read(sea + kSeaSlot, &slot, sizeof slot) || slot >= kSlots ||
        !game::fast_read(sea + kSeaFrames, &frames, sizeof frames) || !plausible_ptr(frames) ||
        !game::fast_read(frames + slot * 16ull, &block, sizeof block) || !plausible_ptr(block) ||
        !game::fast_read(block, raw, sizeof raw) || !game::fast_read(sea + kSeaWind + slot * 4ull, &wind, sizeof wind)) {
        c_bad_sea.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::array<ssl::Band, ssl::kBands> own;
    ssl::from_entries(raw, own);
    const bool sane = ssl::finite_in(wind, 0.0f, 1.0001f) &&
                      std::all_of(own.begin(), own.end(), [](const ssl::Band& b) { return ssl::plausible(b); });
    if (!sane) {  // not the twelve trains: nothing of it is sent, nothing is written over it
        c_implausible.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint64_t now = now_us();
    if (!g_first_noted.exchange(true, std::memory_order_acq_rel)) {
        if (g_note_lock.try_lock()) {
            g_note_first = Note{true, sea, block, slot, own, own, wind, wind};
            g_note_lock.unlock();
        }
    }
    if (!g_guest) {
        // One sea object is the source (a second one would mix two histories into one stream).
        const uintptr_t source = g_sea.load(std::memory_order_relaxed);
        if (source != sea) {
            if (source && now - g_sea_seen_us.load(std::memory_order_relaxed) < kSeaGoneUs) {
                c_other_sea.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            g_sea.store(sea, std::memory_order_relaxed);
        }
        g_sea_seen_us.store(now, std::memory_order_relaxed);
        if (g_cap_lock.try_lock()) {
            g_cap.band = own;
            g_cap.wind = wind > 1.0f ? 1.0f : wind;
            g_cap.host_us = now;
            ++g_cap.seq;
            g_cap_lock.unlock();
            c_captured.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }
    std::array<ssl::Band, ssl::kBands> host;
    float host_wind = 0;
    bool have = false;
    if (!g_mirror_lock.try_lock()) {
        c_busy.fetch_add(1, std::memory_order_relaxed);  // another sea object's frame is at it: this one keeps what it has
        return;
    }
    Box box;
    if (g_box_lock.try_lock()) {
        if (g_box.fresh) {
            box = g_box;
            g_box.fresh = false;
        }
        g_box_lock.unlock();
    }
    if (box.fresh) {
        if (box.epoch != g_epoch) {  // a new connection: what was known of the old one goes
            g_epoch = box.epoch;
            g_mirror.reset();
        }
        (g_mirror.on_sample(box.s, box.recv_us) ? c_rx : c_rx_old).fetch_add(1, std::memory_order_relaxed);
        g_stat_seq.store(g_mirror.last().seq, std::memory_order_relaxed);
        g_stat_has.store(g_mirror.has(), std::memory_order_relaxed);
        g_stat_moving.store(g_mirror.moving(), std::memory_order_relaxed);
        g_stat_offset_us.store(g_mirror.offset_us(), std::memory_order_relaxed);
        g_stat_rate_long.store(g_mirror.rate(0), std::memory_order_relaxed);
        g_stat_rate_short.store(g_mirror.rate(ssl::kBands - 1), std::memory_order_relaxed);
    }
    have = g_mirror.at(now, host, host_wind);
    g_mirror_lock.unlock();
    if (!have) {
        c_own.fetch_add(1, std::memory_order_relaxed);
        if (g_writing.exchange(false, std::memory_order_acq_rel)) c_ends.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    ssl::to_entries(host, raw);
    if (!seh_write(block, raw, sizeof raw) || !seh_write(sea + kSeaWind + slot * 4ull, &host_wind, sizeof host_wind)) {
        fault();
        return;
    }
    c_written.fetch_add(1, std::memory_order_relaxed);
    if (!g_writing.exchange(true, std::memory_order_acq_rel)) {
        c_begins.fetch_add(1, std::memory_order_relaxed);
        if (g_note_lock.try_lock()) {
            g_note_begin = Note{true, sea, block, slot, own, host, wind, host_wind};
            g_note_lock.unlock();
        }
    }
}

uintptr_t fill_detour(uintptr_t sea, uintptr_t a2, uintptr_t a3, uintptr_t a4) {
    if (g_on.load(std::memory_order_acquire)) on_fill(sea);
    return g_hook.unsafe_call<uintptr_t>(sea, a2, a3, a4);  // never inside a __try: the game's exceptions are the game's
}

bool install() {
    std::array<uint8_t, kFillHead.size()> actual{};
    if (!game::safe_read(g_base + kFill, actual.data(), actual.size()) || actual != kFillHead) {
        log::error("SeaSync: exe+0x{:X} is not build 3732's entry of the sea's buffer routine; REFUSED, nothing written",
                   kFill);
        return false;
    }
    auto whole = [](const std::vector<uint8_t>& o) {
        return o.size() == kFillPatch && std::memcmp(o.data(), kFillHead.data(), kFillPatch) == 0;
    };
    const std::string what = std::format("0x{:X}", kFill);
    auto h = game::prepare_inline(g_base + kFill, reinterpret_cast<void*>(&fill_detour), whole, "SeaSync", what.c_str());
    if (!h) {
        log::error("SeaSync: the hook on 0x{:X} could not be prepared ({}); REFUSED", kFill,
                   game::inline_error_text(h.error()));
        return false;
    }
    if (!whole(h->original_bytes())) {
        log::error("SeaSync: the hook on 0x{:X} would patch {} bytes, not the two verified instructions; REFUSED, nothing "
                   "written",
                   kFill, h->original_bytes().size());
        return false;
    }
    g_hook = std::move(*h);
    if (!g_hook.enable()) {
        log::error("SeaSync: the hook on 0x{:X} could not be enabled; REFUSED", kFill);
        g_hook = {};
        return false;
    }
    return true;
}

std::string band_text(const ssl::Band& b) {
    return std::format("phase {:.2f} strength {:.2f} direction {:.1f} origin ({:.0f}, {:.0f})", b.phase, b.fade, b.dir, b.ox,
                       b.oz);
}

DWORD WINAPI module_thread(void*) {
    uintptr_t world = 0;
    while (!game::safe_read(g_base + kWorldSlot, &world, sizeof world) || !world) Sleep(500);
    if (!install()) return 0;
    g_on.store(true, std::memory_order_release);
    log::info("SeaSync ON ({}): exe+0x{:X} hooked at its entry ({} bytes); {}", g_guest ? "guest" : "host", kFill, kFillPatch,
              g_guest ? "the host's twelve wave trains (tunnel control state 17) are written over this client's own "
                        "while they arrive"
                      : "this client's twelve wave trains go to user 2 at 10 Hz (tunnel control state 17); nothing is "
                        "written here");
    int lines = 0;
    uint64_t last_summary = GetTickCount64(), last_ends = 0;
    bool stopped_said = false;
    for (;;) {
        Sleep(1000);
        Note first, begin;
        for (int tries = 0; tries < 50; ++tries) {  // this thread may wait; the others never do
            if (g_note_lock.try_lock()) {
                first = g_note_first;
                begin = g_note_begin;
                g_note_first.fresh = false;
                g_note_begin.fresh = false;
                g_note_lock.unlock();
                break;
            }
            Sleep(1);
        }
        if (first.fresh && lines < kLogLines) {
            ++lines;
            log::info("SeaSync: the sea object 0x{:X} (frame {} of its history at 0x{:X}, wind {:.3f}): the longest train "
                      "{}; the shortest {}",
                      first.sea, first.slot, first.block, first.own_wind, band_text(first.own[0]),
                      band_text(first.own[ssl::kBands - 1]));
        }
        if (begin.fresh && lines < kLogLines) {
            ++lines;
            const ssl::Apart a = ssl::apart(begin.own, begin.host);
            log::info("SeaSync [guest]: the host's sea is drawn from now. This client's own was apart by up to {:.2f} rad of "
                      "phase, {:.1f} degrees of direction and {:.0f} m of origin (wind {:.3f} here, {:.3f} there). The "
                      "longest train: here {} | the host's {}",
                      a.phase, a.dir, a.origin, begin.own_wind, begin.host_wind, band_text(begin.own[0]),
                      band_text(begin.host[0]));
        }
        const uint64_t ends = c_ends.load(std::memory_order_relaxed), begins = c_begins.load(std::memory_order_relaxed);
        if (ends != last_ends && lines < kLogLines) {
            ++lines;
            log::info("SeaSync [guest]: no fresh sample of a moving sea from the host: this client's own sea goes on from "
                      "here (begun {} times, ended {})",
                      begins, ends);
        }
        last_ends = ends;
        if (!g_on.load(std::memory_order_acquire) && !stopped_said) {
            stopped_said = true;
            log::error("SeaSync: {} reads or writes of the sea faulted: the module no longer touches it",
                       c_faults.load(std::memory_order_relaxed));
        }
        const uint64_t now = GetTickCount64();
        if (now - last_summary >= 60000 && c_frames.load(std::memory_order_relaxed)) {
            last_summary = now;
            if (g_guest) {
                const double offset = g_stat_offset_us.load(), rate0 = g_stat_rate_long.load(),
                             rate11 = g_stat_rate_short.load();
                const bool has = g_stat_has.load(), moving = g_stat_moving.load();
                const uint32_t seq = g_stat_seq.load();
                log::info("SeaSync summary [guest]: frames {} | the host's sea written in {}, its own kept in {} (busy "
                          "{}) | samples taken {} (malformed {}, late {}, replaced before a frame took them {}), the last "
                          "#{}{} | the trains' speed {:.3f} .. {:.3f} rad/s | clocks {:.0f} ms apart | not the trains {} "
                          "unreadable {} faults {}",
                          c_frames.load(), c_written.load(), c_own.load(), c_busy.load(), c_rx.load(), c_rx_bad.load(),
                          c_rx_old.load(), c_rx_busy.load(), seq,
                          !has ? " (none yet)" : moving ? "" : " (the host's sea stands still)",
                          rate0 * 1.0e6, rate11 * 1.0e6, offset / 1000.0, c_implausible.load(), c_bad_sea.load(),
                          c_faults.load());
            } else {
                log::info("SeaSync summary [host]: frames {} | kept {} | sent to user 2 {} (refused by the tunnel {}) | of "
                          "another sea object {} | not the trains {} unreadable {}",
                          c_frames.load(), c_captured.load(), c_sent.load(), c_refused.load(), c_other_sea.load(),
                          c_implausible.load(), c_bad_sea.load());
            }
        }
    }
}

}  // namespace

void sea_sync_start(bool enabled, bool guest) {
    if (!enabled) {
        log::info("SeaSync: off (sea_sync=0)");
        return;
    }
    if (game::current_build() != game::Build::v3732) {
        log::info("SeaSync: only for build 1.0.0.3732, off");
        return;
    }
    g_guest = guest;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (const HANDLE t = CreateThread(nullptr, 0, module_thread, nullptr, 0, nullptr)) {
        CloseHandle(t);
    } else {
        log::error("SeaSync: no thread ({})", GetLastError());
    }
}

void sea_sync_host_tick(troc::TrocLink& link) noexcept {
    if (g_guest || !g_on.load(std::memory_order_acquire)) return;
    try {
        const uint64_t now = now_us();
        if (now - g_sent_us < ssl::kSendEveryUs) return;
        // Nobody to tell (as the weather, weather_sync_host_tick): never a backlog of seas for a guest that is not there.
        if (!link.connected() || link.broken() || !server_user2_in_world() || link.backlog_bytes() > kMaxBacklog) return;
        ssl::Sample s;
        if (!g_cap_lock.try_lock()) return;  // a frame is being kept right now: the next pass of the loop
        s = g_cap;
        g_cap_lock.unlock();
        // A frame already sent, or one from a sea that is no longer drawn (a loading screen): nothing to say.
        if (!s.seq || s.seq == g_sent_seq || now - s.host_us > 1000000) return;
        g_sent_seq = s.seq;
        g_sent_us = now;
        (link.push(ssl::frame(s)) ? c_sent : c_refused).fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
    }
}

bool sea_sync_on_frame(std::span<const uint8_t> frame, uint64_t epoch) noexcept {
    if (!ssl::is_frame(frame)) return false;
    if (!g_guest || !g_on.load(std::memory_order_acquire)) return true;  // ours, ignored
    const auto s = ssl::decode(frame);
    if (!s) {
        c_rx_bad.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    // Only stored: the render thread takes the newest one at its next frame (a sample it never took is replaced: the
    // speed of each train is measured with the host's own times, so any two samples do).
    const uint64_t now = now_us();
    if (g_box_lock.try_lock()) {
        if (g_box.fresh) c_rx_busy.fetch_add(1, std::memory_order_relaxed);
        g_box = Box{true, *s, now, epoch};
        g_box_lock.unlock();
    } else {
        c_rx_busy.fetch_add(1, std::memory_order_relaxed);  // a frame is taking the one before: this one is not needed
    }
    return true;
}

}  // namespace cdmp::session2
