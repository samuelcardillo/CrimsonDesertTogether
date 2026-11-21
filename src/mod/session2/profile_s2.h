#pragma once
// Explicit address profile for the small native transport surface used by
// server.cpp / guest.cpp.  offsets_s2.h remains the 2976 research catalogue;
// callers must pass every executable/global RVA through rva() before use.

#include <cstdint>
#include <optional>
#include <span>

#include "mod/game/build_profile.h"
#include "mod/session2/offsets_s2.h"

namespace cdmp::session2::profile {

inline bool supported_build() noexcept {
    const auto b = game::current_build();
    // 3576 (2026-11-06 evening): every transport entry is ported statically (rva3576, sam_ptr) and the session core with
    // it (docs/research/port-3576.md). 3663 (2026-11-13 night): the same, rva3663 and the session core ported statically
    // by module (docs/research/port-3663.md); not yet run in the game.
    return b == game::Build::v2976 || b == game::Build::v3527 || b == game::Build::v3576 || b == game::Build::v3732;
}

// 1.0.0.3576 (static, 2026-11-06; docs/research/port-3576.md): vtables by RTTI name, functions by the source's byte
// arrays searched near 3527's address (every one 0x2E0 .. 0xFE0 further) and confirmed by vtable slots where they are
// virtual, globals by the code that reads them (the 0x71Bxxxx block moved by +0x2210). The ServerActorManager has no
// global of its own that plain code reads (3527's 0x71FC598 was found live; on 3576 it is written only from the
// protected section): it is read through the actor lookup instead, see sam_through_lookup().
inline uintptr_t rva3576(uintptr_t legacy) noexcept {
    switch (legacy) {
        case off::kServerNetSlot: return 0x71C05F0;           // 1572 code references (3527 +0x2210)
        case off::kClientNetSlot: return 0x71C00E0;           // 301 references; 0x3A6758 reads +8 of it
        case off::kSamSlot: return 0x71C0608;                 // the lookup's wrapper: [[slot]] (sam_through_lookup)
        case off::kServerActorLookupSlot: return 0x71C0608;   // read twice by the slot refresh sequence 0x2CACA2B
        case off::kWorldSystemSlot: return 0x71C0100;         // core_globals_3576.json; read by 0x72BA58
        case off::kServerUserActorVtable: return 0x5EEEEA8;   // RTTI
        case off::kNwVirtualAsyncSessionVtable: return 0x5AC5BD0;
        case off::kNwAsyncSessionVtable: return 0x5AC5CD8;
        case off::kServerOnRecvPacket: return 0x29AC440;
        case off::kSessionSend: return 0x161B4E0;
        case off::kNwSessionCtor: return 0x161ACC0;
        case off::kQueuePush: return 0x3A7D10;
        case off::kAllocGlobal: return 0x4B01B48;
        case off::kAllocThread: return 0x4B01A80;
        case off::kTlsIndexSlot: return 0x70D7E50;            // read by kAllocThread at +0x1B
        case off::kToNearExceptOneControlVtable: return 0x5EEEAE0;
        case off::kToNearExceptOneSelfVtable: return 0x5EEEB00;
        case off::kToNearVtable: return 0x5EEEB20;
        case off::kExceptOneVf1: return 0x2D05410;            // slot 1 of 0x5EEEAE0
        case off::kExceptOneSelfVf1: return 0x2D052E0;        // slot 1 of 0x5EEEB00
        case off::kToNearVf1: return 0x2D052C0;               // slot 1 of 0x5EEEB20 (and its bytes)
        case off::kSyncSessionSend: return 0x161BE50;
        case off::kNetLoopPump: return 0x3A5744;
        case off::kClientOnRecvPacket: return 0x3A66A0;
        case game::build_2976::kClientPostPacket: return 0x3A6310;
        case off::kTrocInstancesBegin: return 0x6E90D10;      // troc_mapping_3549_3576.json
        case off::kTrocInstancesEnd: return 0x6EC4400;
        case off::kWindowsLock3Vtable: return 0x590AE88;
        case off::kWindowsLockVf1: return 0x1553E30;          // slot 1 of 0x590AE88 (and its bytes)
        case off::kWindowsLockVf2: return 0xDF0590;           // slot 2
        case off::kSaveMgrRefreshSlots: return 0x19B2CC0;     // the call in the slot refresh sequence
        case off::kSlotRefreshReqSequence: return 0x2CACA2B;
        case off::kTitlePlayReadsSaveMgr: return 0x72BA58;
        default: return 0;
    }
}

// 1.0.0.3663 (static, 2026-11-13; never run; docs/research/port-3663.md). The 3576 exe is gone: functions by their head
// bytes from this source searched in 3663 near 3576 + the region's shift (anchors_3663.json; masked match, and
// byte-identical unless a 3663 array below says otherwise), vtables by RTTI name (vtable_map_3576_3663.json), virtual
// functions by their slot (heads checked), globals by the code that reads them and by their reference counts (the
// same as 3576's: server net 1578 vs 1572, client net 302 vs 301, lookup 197, world 3195).
// RETARGETED to 1.0.0.3732 in place (static, 2026-11-21; never run; the 3663 exe is gone, build_profile.h). Each row's
// own line says what it was in 3663 and how the 3732 value is known; the trailing comments are 3663's, with 3663's
// addresses. Method: the source's head bytes searched near 3663 + the region's shift (tools/porting/p3732lib.py; one
// match each, a .pdata start where it is a function), vtables by RTTI name (vtable_map_3663_3732.json), virtual
// functions by their slot, globals by the 3732 instruction that reads them and their reference counts (3663's: server
// net 1578, client net 302, lookup 198 vs 197, world 3197 vs 3195). The four globals keep 3663's spacing (+0x6190).
inline uintptr_t rva3732(uintptr_t legacy) noexcept {
    switch (legacy) {
        // 3732 (static, 2026-11-21; never run): was 0x72456B8; 1578 references, world + 0x4F8 as in 3663; written by
        // the server set-up 0x2A60F90 (0x2A6195A)
        case off::kServerNetSlot: return 0x724B848;           // 1578 references (3576 +0x850C8)
        // 3732 (static, 2026-11-21; never run): was 0x72451A0; 302 references; ClientNet::OnRecvPacket reads +8 of it
        // (0x3AA1B8 mov rcx,[0x724B338])
        case off::kClientNetSlot: return 0x724B330;           // 302 references (3576 +0x850C0)
        // 3732 (static, 2026-11-21; never run): was 0x72456D0; the lookup's wrapper, see kServerActorLookupSlot
        case off::kSamSlot: return 0x724B860;                 // the lookup's wrapper: [[slot]] (sam_through_lookup)
        // 3732 (static, 2026-11-21; never run): was 0x72456D0; both RIP loads of the slot refresh sequence 0x2D17F3B
        // (198 references)
        case off::kServerActorLookupSlot: return 0x724B860;   // read twice by the slot refresh sequence 0x2D134AB
        // 3732 (static, 2026-11-21; never run): was 0x72451C0; 3197 references; the title's read 0x7584B8 loads it
        case off::kWorldSystemSlot: return 0x724B350;         // two AOB patterns; read by 0x757F98 (the title's read)
        // 3732 (static, 2026-11-21; never run): was 0x5F68B10; pa::ServerUserActor by RTTI
        case off::kServerUserActorVtable: return 0x5F6DE90;   // RTTI
        // 3732 (static, 2026-11-21; never run): was 0x5B40310; pa::NwVirtualAsyncSession by RTTI (slot 9 = kSessionSend)
        case off::kNwVirtualAsyncSessionVtable: return 0x5B45590;
        // 3732 (static, 2026-11-21; never run): was 0x5B403A8; pa::NwAsyncSession by RTTI
        case off::kNwAsyncSessionVtable: return 0x5B45610;
        // 3732 (static, 2026-11-21; never run): was 0x2A0EA70; kServerOnRecvPacketHead, one match, .pdata
        // 0x2A111F0..0x2A11A1B (the same 0x82B bytes); mov esi,0x4430 at 0x2A10767
        case off::kServerOnRecvPacket: return 0x2A111F0;      // head; 3576 +0x62630 (mov esi,0x4430 at 0x2A0DFE7)
        // 3732 (static, 2026-11-21; never run): was 0x165C1F0; kSessionSendHead, one match, .pdata start; slot 9 of
        // 0x5B45590
        case off::kSessionSend: return 0x165DD70;             // head, unique in the image; 3576 +0x40D10
        // 3732 (static, 2026-11-21; never run): was 0x165B9D0; kCtor3527 identical, one match, .pdata start; the
        // protected ServerModule::Init calls it (0xDF5F790) on a 0xD0-byte block
        case off::kNwSessionCtor: return 0x165D550;           // kCtor3527, identical; 3576 +0x40D10
        // 3732 (static, 2026-11-21; never run): was 0x3AB4C0; kQueuePushHead identical, one match, .pdata start
        case off::kQueuePush: return 0x3AB8D0;                // head, identical
        // 3732 (static, 2026-11-21; never run): was 0x4B7E188; kAllocGlobalHead identical, one match, .pdata start
        case off::kAllocGlobal: return 0x4B82728;             // head; 3576 +0x7C640, like the thread allocator
        // 3732 (static, 2026-11-21; never run): was 0x4B7E0C0; the 92 bytes, one match (kAllocThread3663 below),
        // .pdata 0x4B82660..0x4B826BC
        case off::kAllocThread: return 0x4B82660;             // the whole function (92 bytes), unique; rel32 differ
        // 3732 (static, 2026-11-21; never run): was 0x715BFD0; kAllocThread +0x1B: mov r8d,[0x7162090]
        case off::kTlsIndexSlot: return 0x7162090;            // read by kAllocThread at +0x1B
        // 3732 (static, 2026-11-21; never run): was 0x5F686C0; pa::ToNearExceptOneControlSendTargetFinder by RTTI
        case off::kToNearExceptOneControlVtable: return 0x5F6DAC0;
        // 3732 (static, 2026-11-21; never run): was 0x5F686A0; pa::ToNearExceptOneSelfSendTargetFinder by RTTI
        case off::kToNearExceptOneSelfVtable: return 0x5F6DAA0;
        // 3732 (static, 2026-11-21; never run): was 0x5F68780; pa::ToNearSendTargetFinder by RTTI
        case off::kToNearVtable: return 0x5F6DA80;
        // 3732 (static, 2026-11-21; never run): was 0x2D6C360; slot 1 of 0x5F6DAC0, kExceptOneVf1Head identical
        case off::kExceptOneVf1: return 0x2D70DF0;            // slot 1 of 0x5F686C0 (head checked)
        // 3732 (static, 2026-11-21; never run): was 0x2D6C230; slot 1 of 0x5F6DAA0, kExceptOneVf1Head identical
        case off::kExceptOneSelfVf1: return 0x2D70CC0;        // slot 1 of 0x5F686A0
        // 3732 (static, 2026-11-21; never run): was 0x2D6C210; slot 1 of 0x5F6DA80, kToNearVf1Head identical
        case off::kToNearVf1: return 0x2D70CA0;               // slot 1 of 0x5F68780 (head checked)
        // 3732 (static, 2026-11-21; never run): was 0x165CB60; kSyncSessionSendHead identical, one match, .pdata start
        case off::kSyncSessionSend: return 0x165E6E0;         // head, unique; 3576 +0x40D10
        // 3732 (static, 2026-11-21; never run): was 0x3A8D94; kNetLoopPumpAob (39 bytes) identical, one match; a site
        // inside the NetworkThread loop 0x3A8ED0..0x3A9575 (mid hook, r15 = ClientNet)
        case off::kNetLoopPump: return 0x3A91A4;              // the AOB, unique; 3576 +0x3650
        // 3732 (static, 2026-11-21; never run): was 0x3A9CF0; kClientOnRecvPacketHead identical at 3663 +0x410, .pdata
        // start (the other masked match, 0x3AF6F0, is 0x5A00 away)
        case off::kClientOnRecvPacket: return 0x3AA100;       // head; 3576 +0x3650
        // 3732 (static, 2026-11-21; never run): was 0x3A9960; kClientPostPacketHead identical, one match, .pdata start
        case game::build_2976::kClientPostPacket: return 0x3A9D70;  // head; 3576 +0x3650
        // 3732 (static, 2026-11-21; never run): was 0x6F14E10; troc_mapping_3663_3732.json (the block moved +0x6000)
        case off::kTrocInstancesBegin: return 0x6F1AE10;      // troc_mapping_3576_3663.json
        // 3732 (static, 2026-11-21; never run): was 0x6F48840; troc_mapping_3663_3732.json
        case off::kTrocInstancesEnd: return 0x6F4E840;
        // 3732 (static, 2026-11-21; never run): was 0x59874C8; pa::WindowsLock<3> by RTTI
        case off::kWindowsLock3Vtable: return 0x598C5A8;
        // 3732 (static, 2026-11-21; never run): was 0x1593240; slot 1 of 0x598C5A8, kWindowsLockVf1Head identical
        case off::kWindowsLockVf1: return 0x1594DC0;          // slot 1 of 0x59874C8 (head checked)
        // 3732 (static, 2026-11-21; never run): was 0xE297F0; slot 2 of 0x598C5A8, kWindowsLockVf2Head identical
        case off::kWindowsLockVf2: return 0xE2A220;           // slot 2 (head checked)
        // 3732 (static, 2026-11-21; never run): was 0x1A18320; the target of the call at 0x2D17F93 in the sequence; a
        // 5-byte jmp into the protected section (0xB6AAD70; kSaveRefresh3663 below)
        case off::kSaveMgrRefreshSlots: return 0x1A19F90;     // the call in the slot refresh sequence (0x2D13503)
        // 3732 (static, 2026-11-21; never run): was 0x2D134AB; the 125 bytes, one match (masked; only the three
        // relative fields differ), inside 0x2D17E40..0x2D18146
        case off::kSlotRefreshReqSequence: return 0x2D17F3B;  // the whole sequence (125 bytes), unique; rel32 differ
        // 3732 (static, 2026-11-21; never run): was 0x757F98; the 14 bytes, one match (masked), mov rcx,[0x724B350] ;
        // mov rcx,[rcx+0x88], inside 0x758410..0x75864E
        case off::kTitlePlayReadsSaveMgr: return 0x7584B8;    // mov rcx,[world]; mov rcx,[rcx+0x88]; 3576 +0x2C540
        default: return 0;
    }
}

inline uintptr_t rva(uintptr_t legacy) noexcept {
    if (game::current_build() == game::Build::v2976) return legacy;
    if (game::current_build() == game::Build::v3576) return rva3576(legacy);
    if (game::current_build() == game::Build::v3732) return rva3732(legacy);
    if (game::current_build() != game::Build::v3527) return 0;
    switch (legacy) {
        case off::kServerNetSlot: return 0x71BE3E0;
        case off::kClientNetSlot: return 0x71BDED0;
        case off::kSamSlot: return 0x71FC598;  // live: ServerActorManager RTTI/vtable
        case off::kServerActorLookupSlot: return 0x71BE3F8;
        case off::kWorldSystemSlot: return 0x71BDEF0;
        case off::kServerUserActorVtable: return 0x5EECCA8;
        case off::kNwVirtualAsyncSessionVtable: return 0x5AC3B38;
        case off::kNwAsyncSessionVtable: return 0x5AC3AB8;
        case off::kServerOnRecvPacket: return 0x29AB7F0;
        case off::kSessionSend: return 0x161ACC0;
        case off::kNwSessionCtor: return 0x161A4A0;
        case off::kQueuePush: return 0x3A7A30;
        case off::kAllocGlobal: return 0x4B00B68;
        case off::kAllocThread: return 0x4B00AA0;
        case off::kTlsIndexSlot: return 0x70D5D50;
        case off::kToNearExceptOneControlVtable: return 0x5EEC7E0;
        case off::kToNearExceptOneSelfVtable: return 0x5EEC800;
        case off::kToNearVtable: return 0x5EEC820;
        case off::kExceptOneVf1: return 0x2D047A0;
        case off::kExceptOneSelfVf1: return 0x2D04670;
        case off::kToNearVf1: return 0x2D04650;
        case off::kSyncSessionSend: return 0x161B630;
        case off::kNetLoopPump: return 0x3A5464;
        case off::kClientOnRecvPacket: return 0x3A63C0;
        case game::build_2976::kClientPostPacket: return 0x3A6030;
        case off::kTrocInstancesBegin: return 0x6E8ED10;
        case off::kTrocInstancesEnd: return 0x6EC2400;
        case off::kWindowsLock3Vtable: return 0x5908E10;
        case off::kWindowsLockVf1: return 0x1553610;
        case off::kWindowsLockVf2: return 0xDF0170;
        case off::kSaveMgrRefreshSlots: return 0x19B2080;
        case off::kSlotRefreshReqSequence: return 0x2CABDBB;
        case off::kTitlePlayReadsSaveMgr: return 0x72B3F8;
        default: return 0;
    }
}

// 3527's layout of these per-thread / per-entry offsets (3527, 3549, 3576, 3663). 3663 (static): mov edx,0x1FE ;
// mov rax,gs:[0x58] is still the allocators' choice (8727 sites) and mov esi,0x4430 ; add rsi,r15 sits next to the
// server's recv (0x2A0DFE7); the slot entry size is 3527's, not re-derived (as in 3576).
// 3732 (static, 2026-11-21; never run): both re-checked on the 3732 exe. mov edx,0x1FE ; mov rax,gs:[0x58] has the
// same 8727 sites (0x1FD: 1, 0x200: 11) and the protected ServerModule::Init picks the allocator for the session's
// 0xD0 bytes by that byte; mov esi,0x4430 ; add rsi,r15 is at 0x2A10767 (0x2A0DFE7 + 0x2780, the recv's own shift),
// the only such site within 0x3000 of the recv. The slot entry size is still 3527's, not re-derived.
inline bool layout_3527() noexcept {
    const auto b = game::current_build();
    return b == game::Build::v3527 || b == game::Build::v3576 || b == game::Build::v3732;
}

// 3576 (static): 0x29FD2C7 mov edx,0x1FE ; gs:[0x58] ; [rax] ; add rax,rdx ; cmp byte [rax],0 (the allocator's choice).
inline uint32_t tls_heap_selector_offset() noexcept {
    return layout_3527() ? 0x1FE : off::kTls_UseGlobalHeap;
}

// 3576 (static): 0x29AB9B7 mov esi,0x4430 ; add rsi,r15 (r15 = the thread's TLS block), next to the server's recv.
inline uint32_t server_thread_park_tls_offset() noexcept {
    return layout_3527() ? 0x4430 : 0x4460;
}

inline uint32_t save_slot_entry_size() noexcept {
    // The slot number and timestamp remain at +0/+8, but the current native
    // table grew by 0x10 bytes per entry. 3576: 3527's, NOT re-derived (the refresh's body is protected code).
    return layout_3527() ? 0x78 : off::kSaveMgr_EntrySize;
}

// The ServerActorManager: [kSamSlot] on 2976 / 3527 (a global that holds it), [[kSamSlot]] on 3576, where kSamSlot is
// the actor lookup's global. 3576 (static): 0x29FD6A0 builds that wrapper (0x58 bytes, stored at 0x29FD6EF) with
// [wrapper] = rbp = [world+0xF0] (0x29FD5EF), and the manager (vtable 0x5EEF0A8, RTTI) is what 0x29FBBAB stores in
// [world+0xF0]; on 3527 the live check saw the same (the lookup's first field = the manager, live_native_3527.json).
// 3663 (static): the same wrapper, 0x58 bytes, [wrapper] = rbp = [rbx+0xF0] (0x2A6068F), stored at 0x2A6078F.
// 3732 (static, 2026-11-21; never run): the same in 0x2A629E0: mov rbp,[rbx+0xF0] (0x2A62DDF), 0x58 bytes allocated
// (0x2A62E4D), mov [rdi],rbp (0x2A62E90), the wrapper stored in the slot at 0x2A62EDF (its only plain-code write).
inline bool sam_through_lookup() noexcept {
    const auto b = game::current_build();
    return b == game::Build::v3576 || b == game::Build::v3732;
}
// `read(address)` reads a pointer (std::optional<uintptr_t>); the result is the manager, or nullopt.
template <class Read>
std::optional<uintptr_t> sam_ptr(uintptr_t base, Read&& read) {
    const uintptr_t slot = rva(off::kSamSlot);
    if (!slot) return std::nullopt;
    const std::optional<uintptr_t> p = read(base + slot);
    if (!p || !*p) return std::nullopt;
    if (!sam_through_lookup()) return p;
    const std::optional<uintptr_t> sam = read(*p);
    return sam && *sam ? sam : std::nullopt;
}

inline constexpr uint8_t kCtor3527[] = {
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x4C,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
inline constexpr uint8_t kAllocThread3527[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x65,0x48,0x8B,
    0x04,0x25,0x58,0x00,0x00,0x00,0x48,0x8B,0xFA,0x44,0x8B,0x05,0x8E,0x52,0x5D,0x02,0x48,0x8B,
    0xF1,0x4A,0x8B,0x1C,0xC0,0xB8,0x98,0x00,0x00,0x00,0x80,0x3C,0x18,0x00,0x75,0x05,0xE8,0xDB,
    0x59,0x0A,0x00,0xB9,0x90,0x00,0x00,0x00,0x4C,0x8B,0xC7,0x48,0x8B,0xD6,0x48,0x8B,0x0C,0x19,
    0x48,0x8B,0x5C,0x24,0x30,0x48,0x8B,0x74,0x24,0x38,0x48,0x83,0xC4,0x20,0x5F,0xE9,0x3C,0xFC,
    0xFF,0xFF};
inline constexpr uint8_t kSaveRefresh3527[] = {0xE9,0x3B,0x29,0xD9,0x09};
inline constexpr uint8_t kSaveRefresh3549[] = {0xE9,0x1B,0x00,0xE4,0x09};  // jmp 0xB7F20A0
inline constexpr uint8_t kTitlePlayReadsSaveMgr3527[] = {
    0x48,0x8B,0x0D,0xF1,0x2A,0xA9,0x06,0x48,0x8B,0x89,0x88,0x00,0x00,0x00};
// SaveDataSlotRefreshReq's native lock/busy/refresh/unlock sequence in 3527.
// Both RIP-relative loads resolve to kServerActorLookupSlot (0x71BE3F8),
// whose wrapper still exposes the save manager at +0x48; the direct call
// resolves to kSaveMgrRefreshSlots (0x19B2080).
inline constexpr uint8_t kSlotRefreshSequence3527[] = {
    0x48,0x8B,0x05,0x36,0x26,0x51,0x04,0x4C,0x8B,0x78,0x48,0x4C,0x89,0x7D,0x07,0x45,
    0x32,0xF6,0x44,0x88,0x75,0x0F,0x49,0x8B,0x1F,0x48,0x89,0x5D,0x77,0x48,0x8B,0x03,
    0x48,0x8B,0xCB,0xFF,0x50,0x08,0x90,0x45,0x38,0x77,0x60,0x75,0x0C,0x41,0xC6,0x47,
    0x60,0x01,0x41,0xB6,0x01,0x44,0x88,0x75,0x0F,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF,
    0x50,0x10,0x90,0x45,0x84,0xF6,0x75,0x05,0xE9,0x91,0x00,0x00,0x00,0x48,0x8B,0x0D,
    0xE9,0x25,0x51,0x04,0x48,0x8B,0x49,0x48,0xE8,0x68,0x62,0xD0,0xFE,0x90,0x49,0x8B,
    0x1F,0x48,0x89,0x5D,0x77,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF,0x50,0x08,0x90,0x41,
    0xC6,0x47,0x60,0x00,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF,0x50,0x10};

// 3576's own bytes where 3527's differ only in relative fields (the rest of the arrays are byte-identical there).
inline constexpr uint8_t kAllocThread3576[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x65,0x48,0x8B,
    0x04,0x25,0x58,0x00,0x00,0x00,0x48,0x8B,0xFA,0x44,0x8B,0x05,0xAE,0x63,0x5D,0x02,0x48,0x8B,
    0xF1,0x4A,0x8B,0x1C,0xC0,0xB8,0x98,0x00,0x00,0x00,0x80,0x3C,0x18,0x00,0x75,0x05,0xE8,0xDB,
    0x59,0x0A,0x00,0xB9,0x90,0x00,0x00,0x00,0x4C,0x8B,0xC7,0x48,0x8B,0xD6,0x48,0x8B,0x0C,0x19,
    0x48,0x8B,0x5C,0x24,0x30,0x48,0x8B,0x74,0x24,0x38,0x48,0x83,0xC4,0x20,0x5F,0xE9,0x3C,0xFC,
    0xFF,0xFF};
inline constexpr uint8_t kSaveRefresh3576[] = {0xE9,0x4B,0x87,0xEF,0x09};
inline constexpr uint8_t kTitlePlayReadsSaveMgr3576[] = {
    0x48,0x8B,0x0D,0xA1,0x46,0xA9,0x06,0x48,0x8B,0x89,0x88,0x00,0x00,0x00};
inline constexpr uint8_t kSlotRefreshSequence3576[] = {
    0x48,0x8B,0x05,0xD6,0x3B,0x51,0x04,0x4C,0x8B,0x78,0x48,0x4C,0x89,0x7D,0x07,0x45,
    0x32,0xF6,0x44,0x88,0x75,0x0F,0x49,0x8B,0x1F,0x48,0x89,0x5D,0x77,0x48,0x8B,0x03,
    0x48,0x8B,0xCB,0xFF,0x50,0x08,0x90,0x45,0x38,0x77,0x60,0x75,0x0C,0x41,0xC6,0x47,
    0x60,0x01,0x41,0xB6,0x01,0x44,0x88,0x75,0x0F,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF,
    0x50,0x10,0x90,0x45,0x84,0xF6,0x75,0x05,0xE9,0x91,0x00,0x00,0x00,0x48,0x8B,0x0D,
    0x89,0x3B,0x51,0x04,0x48,0x8B,0x49,0x48,0xE8,0x38,0x62,0xD0,0xFE,0x90,0x49,0x8B,
    0x1F,0x48,0x89,0x5D,0x77,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF,0x50,0x08,0x90,0x41,
    0xC6,0x47,0x60,0x00,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF,0x50,0x10};

// 3663's own bytes where 3576's differ only in relative fields (the rest are byte-identical there).
// 3732 (static, 2026-11-21; never run): these four now hold 3732's bytes; in each, only relative fields differ from
// 3663's (given per array as "was").
// 3732 (static, 2026-11-21; never run): was ..0x44,0x8B,0x05,0xEE,0xDE,0x5D,0x02.. (the _tls_index load at +0x1B,
// now -> 0x7162090); the function is at 0x4B82660.
inline constexpr uint8_t kAllocThread3732[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x65,0x48,0x8B,
    0x04,0x25,0x58,0x00,0x00,0x00,0x48,0x8B,0xFA,0x44,0x8B,0x05,0x0E,0xFA,0x5D,0x02,0x48,0x8B,
    0xF1,0x4A,0x8B,0x1C,0xC0,0xB8,0x98,0x00,0x00,0x00,0x80,0x3C,0x18,0x00,0x75,0x05,0xE8,0xDB,
    0x59,0x0A,0x00,0xB9,0x90,0x00,0x00,0x00,0x4C,0x8B,0xC7,0x48,0x8B,0xD6,0x48,0x8B,0x0C,0x19,
    0x48,0x8B,0x5C,0x24,0x30,0x48,0x8B,0x74,0x24,0x38,0x48,0x83,0xC4,0x20,0x5F,0xE9,0x3C,0xFC,
    0xFF,0xFF};
// 3732 (static, 2026-11-21; never run): was {0xE9,0x9B,0x48,0x0F,0x0A}; the thunk 0x1A19F90 now jumps to the
// protected 0xB6AAD70.
inline constexpr uint8_t kSaveRefresh3732[] = {0xE9,0xDB,0x0D,0xC9,0x09};
// 3732 (static, 2026-11-21; never run): was 0x48,0x8B,0x0D,0x21,0xD2,0xAE,0x06,..; at 0x7584B8 the load resolves to
// the world slot 0x724B350.
inline constexpr uint8_t kTitlePlayReadsSaveMgr3732[] = {
    0x48,0x8B,0x0D,0x91,0x2E,0xAF,0x06,0x48,0x8B,0x89,0x88,0x00,0x00,0x00};
// 3732 (static, 2026-11-21; never run): was 0x1E,0x22,0x53,0x04 (+3), 0xD1,0x21,0x53,0x04 (+80) and
// 0xE8,0x18,0x4E,0xD0,0xFE (+88); at 0x2D17F3B both loads resolve to 0x724B860 and the call to 0x1A19F90.
inline constexpr uint8_t kSlotRefreshSequence3732[] = {
    0x48,0x8B,0x05,0x1E,0x39,0x53,0x04,0x4C,0x8B,0x78,0x48,0x4C,0x89,0x7D,0x07,0x45,
    0x32,0xF6,0x44,0x88,0x75,0x0F,0x49,0x8B,0x1F,0x48,0x89,0x5D,0x77,0x48,0x8B,0x03,
    0x48,0x8B,0xCB,0xFF,0x50,0x08,0x90,0x45,0x38,0x77,0x60,0x75,0x0C,0x41,0xC6,0x47,
    0x60,0x01,0x41,0xB6,0x01,0x44,0x88,0x75,0x0F,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF,
    0x50,0x10,0x90,0x45,0x84,0xF6,0x75,0x05,0xE9,0x91,0x00,0x00,0x00,0x48,0x8B,0x0D,
    0xD1,0x38,0x53,0x04,0x48,0x8B,0x49,0x48,0xE8,0xF8,0x1F,0xD0,0xFE,0x90,0x49,0x8B,
    0x1F,0x48,0x89,0x5D,0x77,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF,0x50,0x08,0x90,0x41,
    0xC6,0x47,0x60,0x00,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF,0x50,0x10};

inline std::span<const uint8_t> signature(uintptr_t legacy,
                                          std::span<const uint8_t> old) noexcept {
    if (game::current_build() == game::Build::v2976) return old;
    if (game::current_build() == game::Build::v3576) {
        if (rva(legacy) == 0) return {};
        if (legacy == off::kNwSessionCtor) return kCtor3527;  // identical at 0x161ACC0
        if (legacy == off::kAllocThread) return kAllocThread3576;
        if (legacy == off::kSaveMgrRefreshSlots) return kSaveRefresh3576;
        if (legacy == off::kSlotRefreshReqSequence) return kSlotRefreshSequence3576;
        if (legacy == off::kTitlePlayReadsSaveMgr) return kTitlePlayReadsSaveMgr3576;
        return old;  // the other heads are byte-identical at their 3576 addresses
    }
    if (game::current_build() == game::Build::v3732) {
        if (rva(legacy) == 0) return {};
        // 3732 (static, 2026-11-21; never run): kCtor3527 identical at 0x165D550; every `old` head the callers pass
        // (offsets_s2.h, game/offsets.h kClientPostPacketHead) is byte-identical at its 3732 address.
        if (legacy == off::kNwSessionCtor) return kCtor3527;  // identical at 0x165B9D0
        if (legacy == off::kAllocThread) return kAllocThread3732;
        if (legacy == off::kSaveMgrRefreshSlots) return kSaveRefresh3732;
        if (legacy == off::kSlotRefreshReqSequence) return kSlotRefreshSequence3732;
        if (legacy == off::kTitlePlayReadsSaveMgr) return kTitlePlayReadsSaveMgr3732;
        return old;  // the other heads are byte-identical at their 3663 addresses
    }
    if (game::current_build() != game::Build::v3527 || rva(legacy) == 0) return {};
    if (legacy == off::kNwSessionCtor) return kCtor3527;
    if (legacy == off::kAllocThread) return kAllocThread3527;
    if (legacy == off::kSaveMgrRefreshSlots) {
        return game::is_3549() ? std::span<const uint8_t>(kSaveRefresh3549) : std::span<const uint8_t>(kSaveRefresh3527);
    }
    if (legacy == off::kSlotRefreshReqSequence) return kSlotRefreshSequence3527;
    if (legacy == off::kTitlePlayReadsSaveMgr) return kTitlePlayReadsSaveMgr3527;
    // All other core arrays were verified byte-identical in 3527.
    return old;
}

}  // namespace cdmp::session2::profile
