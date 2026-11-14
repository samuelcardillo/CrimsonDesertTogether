#include "mod/session2/create_retry.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "common/troc_frame.h"
#include "mod/game/actor_lookup.h"
#include "mod/game/build_profile.h"
#include "mod/game/game_errors.h"
#include "mod/log.h"
#include "mod/session2/create_retry_logic.h"
#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2 {
namespace {

using u2f::Mode;

constexpr uint64_t kStepMs = 250;
constexpr uint32_t kMaxLines = 40;  // per kind, per run

std::atomic<uint8_t> g_mode{0};  // u2f::Mode
uint32_t g_lost_budget = 0;      // per conversation (written before the mode is set)
// Everything below: the mod's network thread only.
crl::Store g_store;
crl::Stats g_st;
uint64_t g_epoch = 0;
uint64_t g_fail_cursor = 0;
uint64_t g_last_step_ms = 0;
uint32_t g_lines_fail = 0, g_lines_deliver = 0, g_lines_done = 0, g_lines_alive = 0;
bool g_hook_warned = false;
// For the status line (any thread).
std::atomic<uint64_t> a_kept{0}, a_failures{0}, a_not_kept{0}, a_delivered{0}, a_created{0}, a_gave_up{0}, a_parts{0},
    a_lost{0}, a_evicted{0}, a_size{0}, a_bytes{0}, a_failures_lost{0}, a_guessed{0}, a_named_alive{0}, a_credits{0},
    a_lost_retried{0};

Mode mode() { return static_cast<Mode>(g_mode.load(std::memory_order_relaxed)); }

// The two questions of create_retry_logic.h. ready: in the client's actor table with a TransformSync (what a docked
// part needs of its parent). in_table: registered at all (the record's own actor).
bool ready(uint32_t key) { return key && game::actor_world_position(key).has_value(); }
bool in_table(uint32_t key) { return key && game::find_actor(key).has_value(); }

void publish() {
    a_kept.store(g_st.kept, std::memory_order_relaxed);
    a_failures.store(g_st.failures, std::memory_order_relaxed);
    a_not_kept.store(g_st.failures_not_kept, std::memory_order_relaxed);
    a_delivered.store(g_st.delivered, std::memory_order_relaxed);
    a_created.store(g_st.created_after, std::memory_order_relaxed);
    a_gave_up.store(g_st.gave_up, std::memory_order_relaxed);
    a_parts.store(g_st.parts, std::memory_order_relaxed);
    a_lost.store(g_st.lost, std::memory_order_relaxed);
    a_evicted.store(g_st.evicted, std::memory_order_relaxed);
    a_guessed.store(g_st.guessed, std::memory_order_relaxed);
    a_named_alive.store(g_st.named_alive, std::memory_order_relaxed);
    a_credits.store(g_store.credits(), std::memory_order_relaxed);
    a_lost_retried.store(g_st.lost_retried, std::memory_order_relaxed);
    a_size.store(g_store.size(), std::memory_order_relaxed);
    a_bytes.store(g_store.bytes(), std::memory_order_relaxed);
}

void new_epoch(uint64_t epoch) {
    if (epoch == g_epoch) return;
    g_epoch = epoch;
    g_store.clear();  // another conversation: none of those actors
    g_store.set_lost_budget(g_lost_budget);
}

}  // namespace

bool create_retry_configure(const std::string& raw, uint32_t lost_budget) {
    const auto p = u2f::parse_mode(raw.empty() ? std::string_view("probe") : std::string_view(raw));
    if (!p.known) {
        log::warn("CreateRetry: create_retry='{}' is not 0, probe or 1; it stays off", raw);
        return false;
    }
    if (p.mode == Mode::Off) return false;
    if (game::current_build() != game::Build::v3663) {
        log::info("CreateRetry: only for build 3663 (the record's attach parent offset is that build's); off");
        return false;
    }
    g_lost_budget = lost_budget > 64 ? 64 : lost_budget;
    g_store.set_lost_budget(g_lost_budget);
    g_mode.store(static_cast<uint8_t>(p.mode));
    log::info("CreateRetry ON ({}): gimmick records that reach this client before what they dock to are kept {} s; "
              "when the client reports that one could not be created (game error from exe+0x8E03AF5), its record is "
              "{} once the actor has left the table and its parent is here, at most {} times, and then the kept parts "
              "hanging from it | create_retry_lost {}: {}",
              u2f::mode_name(p.mode), crl::kKeepMs / 1000,
              p.mode == Mode::On ? "given to the client again" : "NOT delivered again (probe: logged)",
              crl::kMaxAttempts, g_lost_budget,
              g_lost_budget ? std::format("up to that many kept records per conversation that are still not on the "
                                          "client {} s after they came, with their parent here, are treated the same",
                                          crl::kLostAfterMs / 1000)
                            : std::string("such records are only counted"));
    return true;
}

void create_retry_on_in(uint16_t opcode, std::span<const uint8_t> payload, uint64_t epoch) noexcept {
    if (mode() == Mode::Off) return;
    try {
        new_epoch(epoch);
        if (opcode == fam::kOpMoveField) {
            g_store.clear();  // a field change: the client starts empty
            return;
        }
        if (opcode == fam::kOpRemove) {
            // u8 | u32 | u8 reason | n x u32 key (keys at +6): what the server takes away is not given back.
            for (size_t i = 6; i + 4 <= payload.size(); i += 4) (void)g_store.drop(fam::le32(&payload[i]));
            return;
        }
        if (opcode != fam::kOpAnnounce) return;
        const uint64_t now = GetTickCount64();
        if (g_store.size()) fam::scan_2005(payload, [&](const fam::Record& r) { g_store.record_seen(r.key, now); });
        crl::scan_orphans(payload, [](uint32_t parent) { return in_table(parent); },
                          [](uint32_t parent) { return g_store.find(parent) != nullptr; },
                          [&](uint32_t key, uint32_t parent, std::span<const uint8_t> rec) {
                              g_store.keep(key, parent, rec, now, g_st);
                          });
    } catch (...) {
    }
}

void create_retry_tick(uint64_t epoch, bool remote_live, std::vector<std::vector<uint8_t>>& out) noexcept {
    const Mode m = mode();
    if (m == Mode::Off) return;
    try {
        const uint64_t now = GetTickCount64();
        if (now - g_last_step_ms < kStepMs) return;
        g_last_step_ms = now;
        new_epoch(epoch);
        // Without the errors hook no failure is seen; the step still runs (kept records expire).
        const bool hooked = game::game_errors_installed();
        if (!hooked && !g_hook_warned) {
            g_hook_warned = true;
            log::warn("CreateRetry: the game errors hook is not installed ([sniffer] enabled=0?): no failure is seen, "
                      "nothing will be retried");
        }
        uint64_t keys[16];
        uint64_t lost = 0;
        const size_t n = hooked ? game::game_errors_post_create_failures_since(g_fail_cursor, keys, 16, &lost) : 0;
        if (lost) a_failures_lost.fetch_add(lost, std::memory_order_relaxed);
        std::vector<uint32_t> failed;
        failed.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const auto key = static_cast<uint32_t>(keys[i]);
            failed.push_back(remote_live ? key : 0);  // a failure of this PC's own game is not the host's to mend
            if (g_lines_fail < kMaxLines) {
                ++g_lines_fail;
                const crl::Kept* e = key && remote_live ? g_store.find(key) : nullptr;
                log::info("CreateRetry [{}]: the client could not create {} ({})", u2f::mode_name(m),
                          key ? std::format("0x{:08X}", key) : std::string("an actor whose key could not be read"),
                          !remote_live ? "not playing on the host: ignored"
                          : e ? std::format("its record is kept: {} bytes, attach parent 0x{:08X}, {} ms old",
                                            e->rec.size(), e->parent, now - e->first_ms)
                              : std::string("no record of it kept: counted as a failure with no name, for a kept "
                                            "record seen just before it that turns out to be missing"));
            }
        }
        if (!remote_live) {
            g_store.clear();
            publish();
            return;
        }
        const uint64_t created_before = g_st.created_after, gave_up_before = g_st.gave_up,
                       alive_before = g_st.named_alive;
        g_store.step(now, failed, [](uint32_t k) { return in_table(k); }, [](uint32_t k) { return ready(k); },
                     [&](const crl::Kept& e) {
                         if (g_lines_deliver < kMaxLines) {
                             ++g_lines_deliver;
                             log::info("CreateRetry [{}]: 0x{:08X} ({}) is not on this client and its parent 0x{:08X} "
                                       "is: its record ({} bytes, first seen {} ms ago) {} (time {} of at most {})",
                                       u2f::mode_name(m), e.key,
                                       e.state == crl::State::Part ? "a part of a fitting given again"
                                       : e.lost_retry ? "never created, with no error (create_retry_lost)"
                                       : e.guessed ? "a failure with no usable name was matched to it"
                                                   : "its creation failed",
                                       e.parent, e.rec.size(), now - e.first_ms,
                                       m == Mode::On ? "goes to the client again" : "would go to the client again",
                                       e.attempts, crl::kMaxAttempts);
                         }
                         if (m != Mode::On) return;
                         const auto payload = crl::single_record_payload(e.rec);
                         if (payload.empty()) return;
                         out.push_back(troc::make(fam::kOpAnnounce, -1, payload));
                     },
                     g_st);
        if (g_st.named_alive != alive_before && g_lines_alive < kMaxLines) {
            ++g_lines_alive;
            log::info("CreateRetry [{}]: an actor a failure named is still on this client {} s later: the failure was "
                      "something else's (so far {} such; each counts as a failure with no name)",
                      u2f::mode_name(m), crl::kGoneWaitMs / 1000, g_st.named_alive);
        }
        if ((g_st.created_after != created_before || g_st.gave_up != gave_up_before) && g_lines_done < kMaxLines) {
            ++g_lines_done;
            log::info("CreateRetry [{}]: so far {} record(s) given again, {} of those actors are on this client now, "
                      "{} given up after {} tries; {} part(s) queued behind them",
                      u2f::mode_name(m), g_st.delivered, g_st.created_after, g_st.gave_up, crl::kMaxAttempts,
                      g_st.parts);
        }
        publish();
    } catch (...) {
    }
}

std::string create_retry_describe() {
    const Mode m = mode();
    if (m == Mode::Off) return {};
    return std::format(" | create retry [{}]: kept now {} ({} KB; ever {}, evicted {}) | creation failures {} (no "
                       "record kept or no actor named {}, named an actor still alive {}; of those matched to a "
                       "missing record {}, waiting for one {}; not read in time {}) | given again {} -> created {} "
                       "(given up {}), parts queued {} | kept but never created with the parent here {} (of which "
                       "given again {})",
                       u2f::mode_name(m), a_size.load(), a_bytes.load() / 1024, a_kept.load(), a_evicted.load(),
                       a_failures.load(), a_not_kept.load(), a_named_alive.load(), a_guessed.load(), a_credits.load(),
                       a_failures_lost.load(), a_delivered.load(), a_created.load(), a_gave_up.load(), a_parts.load(),
                       a_lost.load(), a_lost_retried.load());
}

}  // namespace cdmp::session2
