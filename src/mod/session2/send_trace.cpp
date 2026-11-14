#include "mod/session2/send_trace.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/troc_sniffer.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/p8_probes.h"

namespace cdmp::session2 {
namespace {

constexpr size_t kMaxOps = 8;
constexpr uint32_t kLinesPerOp = 16;       // per opcode, then silent
constexpr size_t kStackBytes = 0x4000;     // of the sender's stack above the hook's rsp
constexpr size_t kMaxReturns = 48;
constexpr size_t kSameBytes = 12;          // two packets whose payloads start alike are one line (key, event, parameter)
constexpr uint32_t kExecute = 0x20000000, kWrite = 0x80000000;  // IMAGE_SCN_MEM_*

struct Op {
    uint16_t logical = 0;
    std::atomic<uint32_t> left{0};
    // What the lines written so far started with (a hash each). Written from the senders' threads without a lock:
    // a lost entry costs one repeated line.
    std::array<std::atomic<uint64_t>, 16> said{};
    std::atomic<uint32_t> said_next{0};
};
std::array<Op, kMaxOps> g_ops;
size_t g_op_count = 0;  // written before the probe is set
uintptr_t g_base = 0;
std::vector<gad::Section> g_secs;  // written before the probe is set, read-only afterwards

// 'R' the exe's ordinary code, 'P' its protected (writable and executable) section, 0: not code.
char code_kind(uintptr_t v) noexcept {
    if (v < g_base + 0x1000 || v - g_base > 0xFFFFFFFFull) return 0;
    const gad::Section* s = gad::section_of(g_secs, static_cast<uint32_t>(v - g_base));
    if (!s || !(s->flags & kExecute)) return 0;
    return (s->flags & kWrite) ? 'P' : 'R';
}

// The seven bytes before v end in a call instruction (the usual encodings).
bool after_call(uintptr_t v) noexcept {
    uint8_t b[7];
    if (!game::fast_read(v - 7, b, sizeof b)) return false;
    if (b[2] == 0xE8) return true;                                               // call rel32
    const uint8_t m2 = b[6];
    if (b[5] == 0xFF && (m2 & 0x38) == 0x10 &&
        ((m2 >> 6) == 3 || ((m2 >> 6) == 0 && (m2 & 7) != 4 && (m2 & 7) != 5)))
        return true;                                                             // call reg / call [reg]
    if (b[4] == 0xFF && (b[5] & 0xF8) == 0x50 && (b[5] & 7) != 4) return true;   // call [reg+disp8]
    if (b[3] == 0xFF && b[4] == 0x54) return true;                               // call [sib+disp8]
    if (b[1] == 0xFF && (((b[2] & 0xF8) == 0x90 && (b[2] & 7) != 4) || b[2] == 0x15)) return true;  // [reg+disp32], [rip+disp32]
    if (b[0] == 0xFF && b[1] == 0x94) return true;                               // call [sib+disp32]
    return false;
}

void on_post(uint16_t logical, const uint8_t* packet, uint16_t size, uint64_t rsp) noexcept {
    Op* op = nullptr;
    for (size_t i = 0; i < g_op_count; ++i)
        if (g_ops[i].logical == logical) op = &g_ops[i];
    if (!op || !op->left.load(std::memory_order_relaxed)) return;
    uint8_t head[24]{};
    const size_t n = size < sizeof head + 5 ? (size > 5 ? size - 5 : 0) : sizeof head;
    const bool read = n && game::fast_read(reinterpret_cast<uintptr_t>(packet) + 5, head, n);
    // A packet that starts like one already written (a gimmick's periodic update: the same key, event and parameter
    // twice a second) does not use up the opcode's lines.
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; read && i < n && i < kSameBytes; ++i) h = (h ^ head[i]) * 0x100000001B3ull;
    h |= 1;
    for (const auto& s : op->said)
        if (s.load(std::memory_order_relaxed) == h) return;
    uint32_t left = op->left.load(std::memory_order_relaxed);
    do {
        if (!left) return;
    } while (!op->left.compare_exchange_weak(left, left - 1, std::memory_order_relaxed));
    op->said[op->said_next.fetch_add(1, std::memory_order_relaxed) % op->said.size()].store(h, std::memory_order_relaxed);
    // The development line of a listed opcode (at most kLinesPerOp each): formatted here, on the sender's thread.
    try {
        std::string bytes;
        for (size_t i = 0; read && i < n; ++i) bytes += std::format("{:02x}{}", head[i], i + 1 < n ? " " : "");
        std::string returns;
        size_t found = 0;
        for (size_t off = 0; off < kStackBytes && found < kMaxReturns; off += 8) {
            uintptr_t v = 0;
            if (!game::fast_read(rsp + off, &v, sizeof v)) break;
            const char kind = code_kind(v);
            if (!kind || !after_call(v)) continue;
            returns += std::format(" +{:X}:{}{:X}", off, kind, v - g_base);
            ++found;
        }
        log::info("SendTrace: opcode {} ({} payload bytes: {}) thread {} | stack offset:R/P exe rva of each return "
                  "address found (R code, P protected):{}", logical, size > 5 ? size - 5 : 0, bytes,
                  GetCurrentThreadId(), returns.empty() ? std::string(" none") : returns);
    } catch (...) {
    }
}

}  // namespace

void send_trace_start(const std::string& raw) {
    size_t count = 0;
    uint32_t value = 0;
    bool in_number = false;
    const auto flush = [&] {
        if (in_number && value && value <= 0xFFFF && count < kMaxOps) {
            g_ops[count].logical = static_cast<uint16_t>(value);
            g_ops[count].left.store(kLinesPerOp);
            ++count;
        }
        value = 0;
        in_number = false;
    };
    for (const char c : raw) {
        if (c >= '0' && c <= '9') {
            value = value < 100000 ? value * 10 + static_cast<uint32_t>(c - '0') : value;
            in_number = true;
        } else {
            flush();
        }
    }
    flush();
    if (!count) return;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!p8_exe_sections(g_base, g_secs)) {
        log::error("SendTrace: the exe's sections are unreadable; off");
        return;
    }
    g_op_count = count;
    std::string list;
    for (size_t i = 0; i < count; ++i) list += std::format(" {}", g_ops[i].logical);
    if (!net::sniffer_add_post_probe(&on_post)) {
        log::error("SendTrace: no room among the sniffer's post probes; off");
        return;
    }
    log::info("SendTrace ON (development): the first {} different packets (by their first {} payload bytes) of each "
              "of these opcodes that this client sends are logged with the return addresses on the sender's stack:{}",
              kLinesPerOp, kSameBytes, list);
}

}  // namespace cdmp::session2
