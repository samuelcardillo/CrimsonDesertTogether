#include "mod/session2/ride_null_guard.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "mod/game/build_profile.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/p8_probes.h"

namespace cdmp::session2 {
namespace {

// Build 1.0.0.3663 (capstone on the installed PE and the minidump of 2026-11-14 06:02).
//   0x620B13  41 80 FC 10              cmp r12b, 0x10        ; riding?
//   0x620B17  75 26                    jne 0x620B3F
//   0x620B19  48 8B 45 6F              mov rax, [rbp+0x6F]   ; the mount actor (the function's second argument)   <- hook
//   0x620B1D  48 8B 40 68              mov rax, [rax+0x68]   ; its components: the crash with a null mount
//   0x620B21  48 8B 48 30              mov rcx, [rax+0x30]
//   0x620B25  48 8B 89 48 04 00 00     mov rcx, [rcx+0x448]
//   0x620B2C  E8 ..                    call 0x620F90         ; the mount's state
//   0x620B3F  4C 8D 25 ..              lea r12, [rip+..]     ; where a character that is not riding goes on
// Nothing jumps into 0x620B19..0x620B20 (the function's only branch near it targets 0x620B13).
// Build 1.0.0.3732 (static, 2026-11-21; never run; the listing above is 3663's): the function is 0x620900 (.pdata
// start, was 0x620590) and the same instructions are 0x370 further on:
//   0x620E83 cmp r12b,0x10 ; 0x620E87 jne 0x620EAF ; 0x620E89 mov rax,[rbp+0x6F] ; 0x620E8D mov rax,[rax+0x68] ;
//   0x620E91 mov rcx,[rax+0x30] ; 0x620E95 mov rcx,[rcx+0x448] ; 0x620E9C call 0x621300 ; 0x620EAF lea r12,[rip+..]
// 3732 (static, 2026-11-21; never run): kPoint was 0x620B19: kPointBytes' 19 bytes are at 0x620E89 and nowhere else in
// the code section; kSkipTo was 0x620B3F: the target of the jne at 0x620E87 (and of 0x620EA3), kSkipBytes there;
// [rbp+0x6F] is still the second argument's home slot (mov [rsp+10h],rdx at 0x62090A, eight pushes, lea rbp,[rsp-1Fh]);
// the branches that land near are 0x620DD5 -> 0x620E83 and the two on 0x620EAF, none into 0x620E89..0x620E90, and no
// rel32 of the code section lands inside the 8 patched bytes. kWorldSlot was 0x72451C0 (3197 references).
constexpr uintptr_t kPoint = 0x620E89, kSkipTo = 0x620EAF, kWorldSlot = 0x724B350;
constexpr uint32_t kPatch = 8, kMountAtRbp = 0x6F;
constexpr uint8_t kPointBytes[19] = {0x48, 0x8B, 0x45, 0x6F, 0x48, 0x8B, 0x40, 0x68, 0x48, 0x8B,
                                     0x48, 0x30, 0x48, 0x8B, 0x89, 0x48, 0x04, 0x00, 0x00};
constexpr uint8_t kSkipBytes[3] = {0x4C, 0x8D, 0x25};
constexpr uint64_t kLoggedTimes = 8;

SafetyHookMid g_hook;
uintptr_t g_base = 0;
bool g_guest = false;
std::atomic<uint64_t> g_fired{0};

void on_point(safetyhook::Context& ctx) noexcept {
    uintptr_t mount = 0;
    if (!game::safe_read(ctx.rbp + kMountAtRbp, &mount, sizeof mount)) return;  // never decide on an unread value
    if (mount) return;
    ctx.rip = g_base + kSkipTo;
    if (g_fired.fetch_add(1, std::memory_order_relaxed) >= kLoggedTimes) return;
    log::warn("RideNullGuard: a riding character's update with no mount on this client: the mount is not looked at this "
              "time; this would have closed the game");
}

DWORD WINAPI install_thread(void*) {
    if (game::current_build() != game::Build::v3732) {
        log::info("RideNullGuard: only for build 3732, off");
        return 0;
    }
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uintptr_t world = 0;
    while (!game::safe_read(g_base + kWorldSlot, &world, sizeof world) || !world) Sleep(500);
    uint8_t skip[sizeof kSkipBytes] = {};
    if (!game::safe_read(g_base + kSkipTo, skip, sizeof skip) || std::memcmp(skip, kSkipBytes, sizeof skip) != 0) {
        log::error("RideNullGuard: the code at exe+0x{:X} is not the expected one; REFUSED, not hooked", kSkipTo);
        return 0;
    }
    std::vector<gad::Section> secs;
    if (!p8_exe_sections(g_base, secs)) {
        log::error("RideNullGuard: the exe's sections are unreadable; REFUSED, not hooked");
        return 0;
    }
    if (p8_install_mid(g_base, secs, kPoint, kPatch, kPointBytes, &on_point, g_hook, "RideNullGuard",
                       "the riding update's read of its mount")) {
        log::info("RideNullGuard ON ({}): a riding character with no mount on this client is updated as if on foot "
                  "that time (exe+0x{:X})", g_guest ? "guest" : "host", kPoint);
    }
    return 0;
}

}  // namespace

void ride_null_guard_start(bool enabled, bool guest) {
    if (!enabled) {
        log::info("RideNullGuard: off in cdmp.ini (ride_null_guard=0)");
        return;
    }
    g_guest = guest;
    if (const HANDLE t = CreateThread(nullptr, 0, install_thread, nullptr, 0, nullptr)) {
        CloseHandle(t);
    } else {
        log::error("RideNullGuard: no thread ({}); not hooked", GetLastError());
    }
}

}  // namespace cdmp::session2
