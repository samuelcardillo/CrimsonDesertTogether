#pragma once

#include <span>
#include "mod/game/build_profile.h"

namespace cdmp::game::character_profile {

#include "mod/game/character_guard_signatures.inc"

// Build 1.0.0.3576 (static, 2026-11-06; not yet run): the six functions moved whole with the same 32-byte heads
// (kLayer3527.. match byte for byte, each a .pdata function start, each unique near old + shift): layer advance +0xC40,
// layers advance and pending +0xDD0, update / post-update / restore +0x370. The 3527 call chain holds: 0x9453A2 ->
// 0x9496C0, 0x949986 -> layers 0x251AC20, 0x251ADC0 -> layer 0x214F8D0, 0x94A2F5 -> pending 0x251B420; restore reads
// `49 8B 45 40 44 38 70 30` at 0x949F1F; the layers function compares the chart at layer+0x118 (0x251AC92).
inline uintptr_t rva3576(uintptr_t old) noexcept {
    switch (old) {
        case 0x1F63F70: return 0x214F8D0;
        case 0x2329700: return 0x251AC20;
        case 0x85FBA0: return 0x9451E0;
        case 0x860D30: return 0x9463D0;
        case 0x8646D0: return 0x949EB0;
        case 0x2329D70: return 0x251B420;
        default: return 0;
    }
}

// Build 1.0.0.3663 (static, 2026-11-13; never run): the same six 32-byte heads, byte for byte, each a .pdata function
// start: layer 0x21BD690 (unique in the code), layers 0x258C5A0 (unique), update 0x9729C0 (unique), post-update 0x973BB0
// and restore 0x9778B0 (the near one of two), pending 0x258CDA0 (unique); update and post-update are both called by
// 0x9CEED0 (0x9CEFAE, 0x9CF092). The 3576 call chain holds at the same offsets:
// update+0x1C2 (0x972B82) -> 0x9770C0, whose +0x2C6 (0x977386) calls layers; layers+0x1A0 (0x258C740) calls layer;
// restore+0x445 (0x977CF5) calls pending; restore+0x6F reads `49 8B 45 40 44 38 70 30`; layers+0x72 (0x258C612) is
// `cmp [rax+0x118],r13` (the layer chart).
// Retargeted to build 1.0.0.3732: the same six 32-byte heads (kLayer3527..), byte for byte, each a .pdata function start
// and the only match near old + shift; the call chain holds at the same offsets: update+0x1C2 (0x9732A2) -> 0x9777E0,
// whose +0x2C6 (0x977AA6) calls layers; layers+0x1A0 (0x258EBD0) calls layer; restore+0x445 (0x978415) calls pending;
// restore+0x6F (0x97803F) reads `49 8B 45 40 44 38 70 30`; layers+0x72 (0x258EAA2) is `cmp [rax+0x118],r13`; update and
// post-update are both called by 0x9CF750 (0x9CF82E, 0x9CF912).
inline uintptr_t rva3732(uintptr_t old) noexcept {
    switch (old) {
        case 0x1F63F70: return 0x21BF2B0;  // 3732 (static, 2026-11-21; never run): was 0x21BD690; kLayer3527 at +0x1C20, unique in the code, called by layers+0x1A0
        case 0x2329700: return 0x258EA30;  // 3732 (static, 2026-11-21; never run): was 0x258C5A0; kLayers3527 at +0x2490, unique in the code, called by 0x9777E0+0x2C6
        case 0x85FBA0: return 0x9730E0;    // 3732 (static, 2026-11-21; never run): was 0x9729C0; kUpdate3527 at +0x720, unique in the code
        case 0x860D30: return 0x9742D0;    // 3732 (static, 2026-11-21; never run): was 0x973BB0; kPost3527 at +0x720, the near one of two (0xE7A6C0)
        case 0x8646D0: return 0x977FD0;    // 3732 (static, 2026-11-21; never run): was 0x9778B0; kRestore3527 at +0x720, the near one of two (0x2BB4200)
        case 0x2329D70: return 0x258F230;  // 3732 (static, 2026-11-21; never run): was 0x258CDA0; kPending3527 at +0x2490, unique in the code, called by restore+0x445
        default: return 0;
    }
}

inline uintptr_t rva(uintptr_t old) noexcept {
    if (current_build() == Build::v2976) return old;
    if (current_build() == Build::v3576) return rva3576(old);
    if (current_build() == Build::v3732) return rva3732(old);
    if (current_build() != Build::v3527) return 0;
    switch (old) {
        case 0x1F63F70: return 0x214EC90;
        case 0x2329700: return 0x2519E50;
        case 0x85FBA0: return 0x944E70;
        case 0x860D30: return 0x946060;
        case 0x8646D0: return 0x949B40;
        case 0x2329D70: return 0x251A650;
        default: return 0;
    }
}

// 3576's and 3663's heads are 3527's (see rva3576 / rva3663).
inline std::span<const uint8_t> signature(uintptr_t old, std::span<const uint8_t> previous) noexcept {
    if (current_build() == Build::v2976) return previous;
    if (current_build() != Build::v3527 && current_build() != Build::v3576 && current_build() != Build::v3732) return {};
    switch (old) {
        case 0x1F63F70: return kLayer3527;
        case 0x2329700: return kLayers3527;
        case 0x85FBA0: return kUpdate3527;
        case 0x860D30: return kPost3527;
        case 0x8646D0: return kRestore3527;
        case 0x2329D70: return kPending3527;
        default: return {};
    }
}

// 3527, 3576 and 3663 (`cmp [rax+0x118],r13` at 0x2519EC2 / 0x251AC92 / 0x258C612); 2976 0x110.
// 3732 (static, 2026-11-21; never run): 0x118 unchanged; the same compare at layers+0x72 (0x258EAA2).
inline uint32_t layer_chart_offset() noexcept {
    return layout_3527() || current_build() == Build::v3732 ? 0x118 : build_2976::kLayer_Chart;
}

inline bool matches(uintptr_t base, uintptr_t old, std::span<const uint8_t> previous) noexcept {
    const auto addr = rva(old);
    const auto bytes = signature(old, previous);
    std::array<uint8_t, 32> actual{};
    return addr && !bytes.empty() && bytes.size() <= actual.size() &&
           safe_read(base + addr, actual.data(), bytes.size()) &&
           std::equal(bytes.begin(), bytes.end(), actual.begin());
}

} // namespace cdmp::game::character_profile
