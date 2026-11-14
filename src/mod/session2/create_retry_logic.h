#pragma once
// The pure part of [session2] create_retry (create_retry.h): no game access, unit-tested in
// tests/optd_create_retry_tests.cpp.
//
// Build 1.0.0.3663, 2026-11-14 (two copies, the host's ship out; the guest's capture and its game-error lines).
// The host's vehicle actor is a user's child: child_hold / child_settle keep it from user 2 for a few seconds
// (child_settle_ms), with the family its record names (the hull and three more). The ship's FITTINGS are not in that
// family (their record's tail has no parent), so the server sends them to user 2 at once, seconds before the vehicle
// and the hull. Their records do name what they hang from, at +0x113 (the attach parent: the vehicle for a fitting
// docked to it, the fitting for its own parts). Most survive being created first (the six cannons, the sail); three
// do not: "Actor AsyncPostProcessForCreate failed" (from exe+0x8E03AF5, the actor object as the second value), and
// what hangs from them never appears either (the lit stern lantern: B01008A2 -> A3 -> A4 in that session). The game
// never sends them again. The same three failed at every join since 2026-11-13, with or without the ship out.
// Measured that morning: the records reached the client at 08:43:20.044, the three failures were reported at
// 08:43:20.415 (0.37 s later), the vehicle came 4 s after the records; minutes later none of the failed actors (nor
// what hangs from them) was in the client's actor table.
//
// So the guest keeps, for a while, every gimmick record whose attach parent was not in the same ActorLoadDataAck and
// is not on this client yet (and what hangs from a kept record); when a creation failure names one of them, its
// record is given to the client again once the actor has left the table and the parent is there; when that works,
// the kept records hanging from it that are still missing follow.
//
// A caution found later the same day: those three error lines are in EVERY archived guest log of 13/14-nov, also the
// shop sessions with no ship out. So they are probably NOT the ship's parts, which then go missing without any error.
// Hence the second trigger, off unless asked for (set_lost_budget): a kept record that is not in the table
// kLostAfterMs after it was seen, with its parent ready and not itself kept, is treated like a failed one, for at
// most that many records.
//
// Two questions are asked about an actor, and they are different ones:
//   exists(key): it is in the client's actor table (the record's OWN actor: "missing" must mean "not there at all",
//                or a second copy could be made of an actor that only lacks a component);
//   ready(key):  it is there with a TransformSync (the PARENT: what a docked part needs of it).
// Being in the table is not "created": an actor is registered before its creation can fail. So a waiting record is
// only dropped as created kWaitingSettleMs after it was seen, and one given again kSettleMs after that delivery.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "mod/session2/family_order_logic.h"

namespace cdmp::session2::crl {

inline constexpr uint8_t kTypeGimmick = 7;
// The record ([u8 type][u32 key]...) has its name at +0x22 (UTF-16, its length in characters at +0x1E) and, after the
// name, the attach parent (docked_gimmicks_logic.h: kRecLayout3527's second copy, the same in 3663). Every part of the
// ship of 2026-11-14 is named "Game Mechanic" (13 characters: the parent at +0x113); the ship itself has 19.
inline constexpr size_t kRecNameLen = 0x1E;
inline constexpr size_t kRecName = 0x22;
inline constexpr size_t kAttachParentAfterName = 0xD7;
inline constexpr uint32_t kMaxNameChars = 64;
inline constexpr size_t attach_parent_at(uint32_t name_chars) {
    return kRecName + 2 * static_cast<size_t>(name_chars) + kAttachParentAfterName;
}
static_assert(attach_parent_at(13) == 0x113);
// The join of 2026-11-14 08:43 (Vellua, the ship out): 288 gimmick records had their attach parent outside their own
// packet, 62 of them with the parent not announced yet (43 got it within 50 ms, the ship's 11 parts 4 s later).
inline constexpr size_t kMaxKept = 1024;
inline constexpr size_t kMaxKeptBytes = 8u << 20;
inline constexpr uint64_t kKeepMs = 120000;         // a kept record nothing asked for goes after this
inline constexpr uint64_t kWaitingSettleMs = 5000;  // a waiting record in the table is "created" only this old
inline constexpr uint64_t kFirstRetryMs = 1500;     // after its failure; and not before the actor is out of the table
inline constexpr uint64_t kGoneWaitMs = 6000;       // still in the table this long after "its" failure: not its own
inline constexpr uint64_t kRetryGapMs = 8000;       // between two deliveries of one record
inline constexpr uint64_t kSettleMs = 3000;         // after a delivery: in the table from then on = created
inline constexpr uint64_t kChildAfterMs = 3000;     // a part of a retried fitting: this long after the fitting settled
inline constexpr uint64_t kLostAfterMs = 15000;     // kept, never created, its parent there: counted (or retried)
inline constexpr uint64_t kWaitingCheckMs = 2000;   // a waiting record is looked up on the client this often
// A failure that names no kept record (no key could be read, or the key read is not a kept one: the name comes from
// stack leftovers and can be wrong) is a credit carrying its time. A kept record takes one, and is treated as that
// failure, when it was seen at most kUnknownWindowMs BEFORE the failure, is not in the table kUnknownAfterMs after
// it, and its parent is ready and not itself being worked on. A credit lasts kUnknownCreditMs.
inline constexpr uint64_t kUnknownWindowMs = 5000;
inline constexpr uint64_t kUnknownAfterMs = 6000;
inline constexpr uint64_t kUnknownCreditMs = 60000;
inline constexpr size_t kMaxCredits = 16;
inline constexpr uint8_t kMaxAttempts = 2;

// The attach parent a gimmick record names: an 0xB key other than its own. 0: none (or not a gimmick record).
inline uint32_t attach_parent(const fam::Record& r) {
    if (r.type != kTypeGimmick || r.bytes.size() < kRecName) return 0;
    const uint32_t chars = fam::le32(&r.bytes[kRecNameLen]);
    if (chars > kMaxNameChars) return 0;
    const size_t at = attach_parent_at(chars);
    if (r.bytes.size() < at + 4) return 0;
    const uint32_t p = fam::le32(&r.bytes[at]);
    return (p >> 28) == 0xB && p != r.key ? p : 0;
}

enum class State : uint8_t {
    Waiting,  // kept; nothing known about its creation
    Failed,   // the client reported that its creation failed (or a failure with no name was matched to it)
    Part,     // it hangs from a record this module delivered again
};

struct Kept {
    uint32_t key = 0, parent = 0;
    uint64_t first_ms = 0, next_ms = 0, check_ms = 0, fail_ms = 0, settle_ms = 0;
    State state = State::Waiting;
    uint8_t attempts = 0;
    bool lost_counted = false;
    bool parent_seen = false;  // its parent's own record went to the client after it
    bool guessed = false;      // Failed by a credit or as a lost one, not by name
    bool lost_retry = false;   // Failed as a lost one (set_lost_budget)
    std::vector<uint8_t> rec;  // the record's bytes (without its u16 length)
};

struct Stats {
    uint64_t kept = 0, replaced = 0, evicted = 0, expired = 0, removed = 0;
    uint64_t failures = 0, failures_not_kept = 0, named_alive = 0, credits = 0;
    uint64_t delivered = 0, created_after = 0, gave_up = 0, parts = 0, lost = 0, guessed = 0, lost_retried = 0;
};

class Store {
public:
    size_t size() const { return v_.size(); }
    size_t bytes() const { return bytes_; }
    size_t credits() const { return credits_.size(); }
    const std::vector<Kept>& entries() const { return v_; }

    Kept* find(uint32_t key) {
        for (Kept& e : v_)
            if (e.key == key) return &e;
        return nullptr;
    }
    void clear() {
        v_.clear();
        credits_.clear();
        bytes_ = 0;
    }
    // How many lost records (see the top) may still be given again; 0: they are only counted.
    void set_lost_budget(uint32_t n) { lost_budget_ = n; }
    uint32_t lost_budget() const { return lost_budget_; }
    // A record of `key` went to the client: what waits for it as its parent knows.
    void announced(uint32_t key) {
        for (Kept& e : v_)
            if (e.parent == key) e.parent_seen = true;
    }
    // Every record on its way to the client passes here (the host's, and the ones this module delivers itself),
    // orphan or not. For a key that is kept: the client has a record of it NOW, so the kept copy must not go out
    // right behind it. One already delivered waits a full gap again; one that had failed or was queued as a part
    // but not delivered yet starts over as a waiting one (the host announced it again).
    void record_seen(uint32_t key, uint64_t now) {
        announced(key);
        Kept* e = find(key);
        if (!e) return;
        if (e->attempts) {
            e->next_ms = std::max(e->next_ms, now + kRetryGapMs);
            e->settle_ms = std::max(e->settle_ms, now + kSettleMs);
        } else if (e->state != State::Waiting) {
            e->state = State::Waiting;
            e->guessed = e->lost_retry = false;
            e->first_ms = now;
            e->check_ms = 0;
            e->lost_counted = false;
        }
    }
    bool drop(uint32_t key) {
        for (size_t i = 0; i < v_.size(); ++i) {
            if (v_[i].key != key) continue;
            erase_at(i);
            return true;
        }
        return false;
    }
    // A record seen on its way to the client. One already being retried keeps its state (the record this module
    // delivers passes here again); a waiting one takes the newer bytes. Over the caps, waiting ones go (make_room).
    void keep(uint32_t key, uint32_t parent, std::span<const uint8_t> rec, uint64_t now, Stats& st) {
        if (!key || !parent || rec.empty() || rec.size() > kMaxKeptBytes / 4) return;
        if (Kept* e = find(key)) {
            if (e->state != State::Waiting || e->attempts) return;
            bytes_ -= e->rec.size();
            e->rec.assign(rec.begin(), rec.end());
            bytes_ += e->rec.size();
            e->parent = parent;
            e->first_ms = now;
            e->lost_counted = false;
            e->parent_seen = false;
            ++st.replaced;
            return;
        }
        if (!make_room(rec.size(), st)) return;  // only records being retried are left: the new one is not kept
        Kept e;
        e.key = key;
        e.parent = parent;
        e.first_ms = now;
        e.rec.assign(rec.begin(), rec.end());
        bytes_ += e.rec.size();
        v_.push_back(std::move(e));
        ++st.kept;
    }

    // One step. failed: the keys of the actors whose creation the client reported as failed since the last step (0:
    // no key could be read). exists(key) / ready(key): see the top of this file. emit(const Kept&): deliver its record
    // again (not in probe: the caller decides; the attempt is counted either way).
    template <class Exists, class Ready, class Emit>
    void step(uint64_t now, std::span<const uint32_t> failed, Exists&& exists, Ready&& ready, Emit&& emit, Stats& st) {
        for (const uint32_t k : failed) {
            ++st.failures;
            Kept* e = k ? find(k) : nullptr;
            if (!e) {
                ++st.failures_not_kept;
                add_credit(now, st);
                continue;
            }
            if (e->state == State::Waiting) {
                e->state = State::Failed;
                e->fail_ms = now;
                e->next_ms = now + kFirstRetryMs;
            }
            // One already being retried goes again at its own time.
        }
        std::erase_if(credits_, [&](uint64_t t) { return now - t > kUnknownCreditMs; });
        for (size_t i = 0; i < v_.size();) {
            Kept& e = v_[i];
            // A waiting record is looked up now and then (hundreds can wait after a join); one being retried, every
            // step.
            if (e.state == State::Waiting && now < e.check_ms) {
                ++i;
                continue;
            }
            e.check_ms = now + kWaitingCheckMs;
            const bool in = exists(e.key);
            bool done = false;
            if (e.state == State::Waiting) {
                if (in) {
                    if (now - e.first_ms >= kWaitingSettleMs) {
                        ++st.removed;
                        done = true;
                    }
                } else if (now - e.first_ms > kKeepMs) {
                    ++st.expired;
                    done = true;
                } else if (ready(e.parent) && !find(e.parent)) {
                    // Missing, with what it hangs from there (and not one this module is still working on).
                    uint64_t when = 0;
                    if (take_credit(e, now, when)) {
                        e.state = State::Failed;
                        e.guessed = true;
                        e.fail_ms = when;
                        e.next_ms = now;
                        ++st.guessed;
                    } else if (!e.lost_counted && now - e.first_ms > kLostAfterMs) {
                        e.lost_counted = true;
                        ++st.lost;
                        if (lost_budget_) {
                            --lost_budget_;
                            e.state = State::Failed;
                            e.guessed = e.lost_retry = true;
                            e.fail_ms = now;
                            e.next_ms = now;
                            ++st.lost_retried;
                        }
                    }
                }
            } else if (!e.attempts) {
                if (in && e.state == State::Failed && !e.guessed) {
                    // The actor the failure named is still registered: on its way out, or the name was wrong.
                    if (now - e.fail_ms < kGoneWaitMs) {
                        ++i;
                        continue;
                    }
                    ++st.named_alive;
                    add_credit(e.fail_ms, st);
                    done = true;
                } else if (in) {
                    ++st.removed;  // a part or a guess that came by itself
                    done = true;
                }
            } else if (in && now >= e.settle_ms) {
                ++st.created_after;
                // What hangs from it and is still missing follows, a little later.
                for (Kept& c : v_) {
                    if (c.parent != e.key || c.state != State::Waiting || exists(c.key)) continue;
                    c.state = State::Part;
                    c.next_ms = now + kChildAfterMs;
                    ++st.parts;
                }
                done = true;
            }
            if (!done && e.state != State::Waiting && now - e.first_ms > 2 * kKeepMs) {
                ++st.expired;  // its parent never came
                done = true;
            }
            if (done) {
                erase_at(i);
                continue;
            }
            if (e.state == State::Waiting || in || now < e.next_ms || !ready(e.parent)) {
                ++i;
                continue;
            }
            if (e.attempts >= kMaxAttempts) {
                ++st.gave_up;
                erase_at(i);
                continue;
            }
            ++e.attempts;
            e.next_ms = now + kRetryGapMs;
            e.settle_ms = now + kSettleMs;
            ++st.delivered;
            emit(static_cast<const Kept&>(e));
            ++i;
        }
    }

private:
    void erase_at(size_t i) {
        bytes_ -= v_[i].rec.size();
        v_.erase(v_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    void add_credit(uint64_t when, Stats& st) {
        if (credits_.size() >= kMaxCredits) credits_.erase(credits_.begin());
        credits_.push_back(when);
        ++st.credits;
    }
    bool take_credit(const Kept& e, uint64_t now, uint64_t& when) {
        for (size_t i = 0; i < credits_.size(); ++i) {
            const uint64_t t = credits_[i];
            if (e.first_ms > t || t - e.first_ms > kUnknownWindowMs || now - t < kUnknownAfterMs) continue;
            when = t;
            credits_.erase(credits_.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
        return false;
    }
    // Room for one more record of `incoming` bytes. Waiting records go: first those whose parent has been announced
    // since and is not itself kept (the usual case: parent and child in one burst), the oldest of them; then the
    // oldest waiting ones. False: nothing more can go.
    bool make_room(size_t incoming, Stats& st) {
        if (v_.size() < kMaxKept && bytes_ + incoming <= kMaxKeptBytes) return true;
        std::vector<uint32_t> keys;
        keys.reserve(v_.size());
        for (const Kept& e : v_) keys.push_back(e.key);
        std::sort(keys.begin(), keys.end());
        std::vector<std::pair<uint64_t, uint32_t>> order;  // (not safe << 63 | first seen, key)
        order.reserve(v_.size());
        for (const Kept& e : v_) {
            if (e.state != State::Waiting) continue;
            const bool safe = e.parent_seen && !std::binary_search(keys.begin(), keys.end(), e.parent);
            order.emplace_back((safe ? 0ull : 1ull << 63) | e.first_ms, e.key);
        }
        std::sort(order.begin(), order.end());
        size_t next = 0;
        while (!v_.empty() && (v_.size() >= kMaxKept || bytes_ + incoming > kMaxKeptBytes)) {
            if (next == order.size()) return false;
            if (drop(order[next++].second)) ++st.evicted;
        }
        return true;
    }

    std::vector<Kept> v_;
    std::vector<uint64_t> credits_;  // the times of the failures not yet matched to a record
    size_t bytes_ = 0;
    uint32_t lost_budget_ = 0;
};

// The records of one ActorLoadDataAck payload worth keeping: a gimmick whose attach parent is neither announced in
// the same payload nor on this client (present(parent)); one whose parent is itself kept (tracked(parent)); and,
// within the payload, what hangs from a record that is kept (the lantern's lamp when both come in one packet: the
// lamp never appears if the lantern fails). f(key, parent, record bytes), in the payload's order.
template <class Present, class Tracked, class F>
void scan_orphans(std::span<const uint8_t> payload, Present&& present, Tracked&& tracked, F&& f) {
    struct Cand {
        uint32_t key, parent;
        std::span<const uint8_t> bytes;
        bool keep;
    };
    std::vector<Cand> c;
    fam::scan_2005(payload, [&](const fam::Record& r) {
        const uint32_t parent = attach_parent(r);
        if (!parent) return;
        c.push_back({r.key, parent, r.bytes, tracked(parent) || (!fam::announces(payload, parent) && !present(parent))});
    });
    for (bool more = true; more;) {
        more = false;
        for (Cand& x : c) {
            if (x.keep) continue;
            for (const Cand& p : c) {
                if (!p.keep || p.key != x.parent) continue;
                x.keep = more = true;
                break;
            }
        }
    }
    for (const Cand& x : c)
        if (x.keep) f(x.key, x.parent, x.bytes);
}

// The ActorLoadDataAck payload that carries one record: [u16 length][record].
inline std::vector<uint8_t> single_record_payload(std::span<const uint8_t> rec) {
    std::vector<uint8_t> p;
    if (rec.empty() || rec.size() > 0xFFFF) return p;
    p.reserve(rec.size() + 2);
    p.push_back(static_cast<uint8_t>(rec.size() & 0xFF));
    p.push_back(static_cast<uint8_t>(rec.size() >> 8));
    p.insert(p.end(), rec.begin(), rec.end());
    return p;
}

}  // namespace cdmp::session2::crl
