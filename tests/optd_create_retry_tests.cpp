// create_retry (src/mod/session2/create_retry_logic.h): which gimmick records the guest keeps and when it gives one
// to its client again. Keys from the 2026-11-14 session (the host's ship 0xB010000A, the stern lantern A2 -> A3 -> A4).

#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "mod/session2/create_retry_logic.h"

using namespace cdmp::session2;
using namespace cdmp::session2::crl;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

constexpr uint32_t kShip = 0xB010000A, kHull = 0xB0100890, kCannon = 0xB010089B, kLantern = 0xB01008A2,
                   kLamp = 0xB01008A3, kGlass = 0xB01008A4;

// A record: [u8 type][u32 key] ... [u32 name length at +0x1E][the name, UTF-16] ... [u32 attach parent, 0xD7 after
// the name] ... (len bytes). 13 characters: "Game Mechanic", what every part of the ship is called.
std::vector<uint8_t> record(uint8_t type, uint32_t key, uint32_t parent, size_t len = 0x140, uint32_t name_chars = 13) {
    std::vector<uint8_t> r(len, 0);
    r[0] = type;
    std::memcpy(&r[1], &key, 4);
    if (len >= kRecName) std::memcpy(&r[kRecNameLen], &name_chars, 4);
    const size_t at = attach_parent_at(name_chars);
    if (len >= at + 4) std::memcpy(&r[at], &parent, 4);
    return r;
}
void append(std::vector<uint8_t>& payload, const std::vector<uint8_t>& rec) {
    payload.push_back(static_cast<uint8_t>(rec.size() & 0xFF));
    payload.push_back(static_cast<uint8_t>(rec.size() >> 8));
    payload.insert(payload.end(), rec.begin(), rec.end());
}

// The client of these tests. Two sets, because the logic asks two different questions: is the actor in the table
// (the record's own actor), and is it there with a TransformSync (its parent).
struct Client {
    std::set<uint32_t> table, ready;
    std::vector<uint32_t> emitted;
    void add(uint32_t k) {
        table.insert(k);
        ready.insert(k);
    }
    void remove(uint32_t k) {
        table.erase(k);
        ready.erase(k);
    }
    void step(Store& s, Stats& st, uint64_t now, std::span<const uint32_t> failed = {}) {
        s.step(now, failed, [&](uint32_t k) { return table.count(k) != 0; },
               [&](uint32_t k) { return ready.count(k) != 0; }, [&](const Kept& e) { emitted.push_back(e.key); }, st);
    }
};

void test_attach_parent() {
    const auto a = record(7, kLantern, kShip);
    fam::Record r{a, a[0], kLantern};
    CHECK(attach_parent(r) == kShip && attach_parent_at(13) == 0x113);
    const auto self = record(7, kLantern, kLantern);
    CHECK(attach_parent(fam::Record{self, 7, kLantern}) == 0);
    const auto npc = record(4, 0xB01008A9, kHull);  // not a gimmick
    CHECK(attach_parent(fam::Record{npc, 4, 0xB01008A9}) == 0);
    const auto shorty = record(7, kLantern, kShip, 0x100);  // too short to hold the field
    CHECK(attach_parent(fam::Record{shorty, 7, kLantern}) == 0);
    const auto character = record(7, kLantern, 0xA0100001);  // a character is not what a fitting docks to
    CHECK(attach_parent(fam::Record{character, 7, kLantern}) == 0);
    // The field sits after the name: a 19-character name ("Small Greymane Ship") moves it by 12 bytes.
    const auto longer = record(7, kLantern, kShip, 0x140, 19);
    CHECK(attach_parent(fam::Record{longer, 7, kLantern}) == kShip);
    auto misread = longer;  // the same bytes with the wrong length: the parent is not where a fixed offset looks
    const uint32_t thirteen = 13, absurd = 5000;
    std::memcpy(&misread[kRecNameLen], &thirteen, 4);
    CHECK(attach_parent(fam::Record{misread, 7, kLantern}) == 0);
    auto broken = a;
    std::memcpy(&broken[kRecNameLen], &absurd, 4);
    CHECK(attach_parent(fam::Record{broken, 7, kLantern}) == 0);
}

void test_scan_orphans() {
    std::vector<uint8_t> payload;
    append(payload, record(7, kCannon, kShip));
    append(payload, record(7, kLamp, kLantern));     // its parent is in this payload, and that parent is kept
    append(payload, record(7, kLantern, kShip));
    append(payload, record(7, 0xB0100891, kHull));   // its parent is on the client already
    append(payload, record(7, kGlass, kLamp));       // hangs from the lamp, which hangs from the kept lantern
    std::set<uint32_t> here = {kHull};
    std::set<uint32_t> tracked;
    std::vector<uint32_t> kept;
    const auto scan = [&](const std::vector<uint8_t>& p) {
        kept.clear();
        scan_orphans(p, [&](uint32_t k) { return here.count(k) != 0; },
                     [&](uint32_t k) { return tracked.count(k) != 0; },
                     [&](uint32_t key, uint32_t, std::span<const uint8_t> rec) {
                         kept.push_back(key);
                         CHECK(rec.size() == 0x140);
                     });
    };
    scan(payload);
    CHECK(kept.size() == 4 && kept[0] == kCannon && kept[1] == kLamp && kept[2] == kLantern && kept[3] == kGlass);
    // Parent and child in one payload with the grandparent on the client: nothing is an orphan.
    here.insert(kShip);
    scan(payload);
    CHECK(kept.empty());
    // A parent that is itself kept (from an earlier payload) keeps its child, even while it is in the table.
    std::vector<uint8_t> later;
    append(later, record(7, kLamp, kLantern));
    here.insert(kLantern);
    scan(later);
    CHECK(kept.empty());
    tracked.insert(kLantern);
    scan(later);
    CHECK(kept.size() == 1 && kept[0] == kLamp);
}

void test_retry_and_parts() {
    Store s;
    Stats st;
    Client c;
    s.keep(kCannon, kShip, record(7, kCannon, kShip), 1000, st);
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    s.keep(kLamp, kLantern, record(7, kLamp, kLantern), 1000, st);
    s.keep(kGlass, kLamp, record(7, kGlass, kLamp), 1000, st);
    CHECK(s.size() == 4 && st.kept == 4);
    // The cannon is registered at once (kept a while longer: a creation can still fail); the lantern fails; nothing
    // is delivered while the ship is not there.
    c.add(kCannon);
    const uint32_t failed[] = {kLantern, 0xB0100FFF};
    c.step(s, st, 1100, failed);
    CHECK(s.size() == 4 && st.failures == 2 && st.failures_not_kept == 1 && c.emitted.empty());
    c.step(s, st, 5000);
    CHECK(c.emitted.empty());
    // The ship arrives: the lantern's record goes again, once.
    c.add(kShip);
    c.step(s, st, 5100);
    CHECK(c.emitted.size() == 1 && c.emitted[0] == kLantern && st.delivered == 1);
    c.step(s, st, 5200);
    CHECK(c.emitted.size() == 1);
    // It is registered: not "created" before kSettleMs after the delivery.
    c.add(kLantern);
    c.step(s, st, 6000);
    CHECK(st.created_after == 0 && s.find(kLantern) != nullptr);
    c.step(s, st, 5100 + kSettleMs);
    CHECK(st.created_after == 1 && st.parts == 1 && s.find(kLantern) == nullptr && c.emitted.size() == 1);
    CHECK(s.find(kCannon) == nullptr && st.removed == 1);  // the cannon, old enough by now
    // Its lamp, still missing, follows after kChildAfterMs; then the glass after the lamp.
    const uint64_t lamp_at = 5100 + kSettleMs + kChildAfterMs;
    c.step(s, st, lamp_at);
    CHECK(c.emitted.size() == 2 && c.emitted[1] == kLamp);
    c.add(kLamp);
    c.step(s, st, lamp_at + kSettleMs);
    CHECK(st.created_after == 2 && st.parts == 2);
    c.step(s, st, lamp_at + kSettleMs + kChildAfterMs);
    CHECK(c.emitted.size() == 3 && c.emitted[2] == kGlass);
    c.add(kGlass);
    c.step(s, st, lamp_at + 2 * kSettleMs + kChildAfterMs);
    CHECK(s.size() == 0 && st.created_after == 3 && st.gave_up == 0);
    // The failure that named nothing kept was never matched to a part of what was being mended.
    CHECK(st.guessed == 0);
}

void test_gives_up_and_keeps_state() {
    Store s;
    Stats st;
    Client c;
    c.add(kShip);
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    const uint32_t failed[] = {kLantern};
    c.step(s, st, 1000, failed);
    c.step(s, st, 1000 + kFirstRetryMs);
    CHECK(c.emitted.size() == 1);
    // The delivered record passes the scan again, at once: the entry keeps its attempts, its bytes and its times.
    s.record_seen(kLantern, 1000 + kFirstRetryMs);
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000 + kFirstRetryMs, st);
    CHECK(s.find(kLantern) && s.find(kLantern)->attempts == 1 && st.replaced == 0);
    c.step(s, st, 1000 + kFirstRetryMs + kRetryGapMs, failed);
    CHECK(c.emitted.size() == 2);
    c.step(s, st, 1000 + kFirstRetryMs + 2 * kRetryGapMs);
    CHECK(c.emitted.size() == 2 && st.gave_up == 1 && s.size() == 0);
}

// The host announces a kept actor again: the client has a record of it now, so the kept copy does not go out right
// behind it.
void test_host_announces_again() {
    Store s;
    Stats st;
    Client c;
    c.add(kShip);
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    const uint32_t failed[] = {kLantern};
    c.step(s, st, 1000, failed);
    // Failed, not delivered yet: it starts over as a waiting one.
    s.record_seen(kLantern, 1200);
    CHECK(s.find(kLantern)->state == State::Waiting && s.find(kLantern)->first_ms == 1200);
    c.step(s, st, 1000 + kFirstRetryMs);
    CHECK(c.emitted.empty());
    // That creation fails too: now it is given again.
    c.step(s, st, 3000, failed);
    c.step(s, st, 3000 + kFirstRetryMs);
    CHECK(c.emitted.size() == 1);
    // Delivered once; the host announces it again just before the second try: a full gap from that record.
    s.record_seen(kLantern, 12000);
    c.step(s, st, 3000 + kFirstRetryMs + kRetryGapMs);
    CHECK(c.emitted.size() == 1);
    c.step(s, st, 12000 + kRetryGapMs);
    CHECK(c.emitted.size() == 2);
    // A key that is not kept: nothing happens.
    s.record_seen(kCannon, 30000);
    CHECK(s.size() == 1);
}

// An actor is registered before its creation can fail, and a failed one is still registered for a moment: the record
// is not dropped as "created" too early, and it is not given again while the old actor is in the table.
void test_registered_is_not_created() {
    Store s;
    Stats st;
    Client c;
    c.add(kShip);
    c.add(kLantern);
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    c.step(s, st, 1100);
    CHECK(s.size() == 1 && st.removed == 0);
    const uint32_t failed[] = {kLantern};
    c.step(s, st, 1400, failed);
    c.step(s, st, 1400 + kFirstRetryMs);
    CHECK(c.emitted.empty() && s.size() == 1);  // still in the table
    c.remove(kLantern);
    c.step(s, st, 1400 + kFirstRetryMs + 300);
    CHECK(c.emitted.size() == 1 && c.emitted[0] == kLantern);
}

// The two questions are different ones: "in the table" for the record's own actor, "ready" for its parent.
void test_two_questions() {
    Store s;
    Stats st;
    Client c;
    s.set_lost_budget(4);
    c.add(kShip);
    // The record's own actor is in the table without a TransformSync: it is there. Neither a failure with no name
    // nor the lost budget gives its record again (that would be a second copy).
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    c.table.insert(kLantern);
    const uint32_t unnamed[] = {0};
    c.step(s, st, 1200, unnamed);
    c.step(s, st, 1200 + kUnknownAfterMs);
    CHECK(c.emitted.empty() && s.size() == 0 && st.removed == 1 && st.guessed == 0 && st.lost == 0);
    // The parent is in the table but not ready: nothing is given to the client until it is.
    c.table.insert(kHull);
    s.keep(kCannon, kHull, record(7, kCannon, kHull), 20000, st);
    const uint32_t failed[] = {kCannon};
    c.step(s, st, 20100, failed);
    c.step(s, st, 20100 + kFirstRetryMs);
    CHECK(c.emitted.empty());
    c.ready.insert(kHull);
    c.step(s, st, 20100 + kFirstRetryMs + 250);
    CHECK(c.emitted.size() == 1 && c.emitted[0] == kCannon);
}

// A failure that names a kept actor which stays alive was something else's: the record seen just before it that
// turns out to be missing takes it.
void test_named_but_alive() {
    Store s;
    Stats st;
    Client c;
    c.add(kShip);
    c.add(kCannon);
    s.keep(kCannon, kShip, record(7, kCannon, kShip), 1000, st);
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    const uint32_t failed[] = {kCannon};
    c.step(s, st, 1300, failed);
    c.step(s, st, 1300 + kFirstRetryMs);
    CHECK(c.emitted.empty() && st.named_alive == 0);
    c.step(s, st, 1300 + kGoneWaitMs);
    CHECK(st.named_alive == 1 && s.find(kCannon) == nullptr && st.guessed == 1);
    CHECK(c.emitted.size() == 1 && c.emitted[0] == kLantern && s.credits() == 0);
}

// A part queued behind a mended fitting that turns up by itself is not given again.
void test_part_appears_by_itself() {
    Store s;
    Stats st;
    Client c;
    c.add(kShip);
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    s.keep(kLamp, kLantern, record(7, kLamp, kLantern), 1000, st);
    const uint32_t failed[] = {kLantern};
    c.step(s, st, 1000, failed);
    c.step(s, st, 1000 + kFirstRetryMs);
    c.add(kLantern);
    c.step(s, st, 1000 + kFirstRetryMs + kSettleMs);
    CHECK(st.created_after == 1 && st.parts == 1 && s.size() == 1);
    c.add(kLamp);
    c.step(s, st, 1000 + kFirstRetryMs + kSettleMs + 500);
    CHECK(s.size() == 0 && c.emitted.size() == 1 && st.removed == 1);
}

void test_caps_and_expiry() {
    Store s;
    Stats st;
    Client c;
    for (uint32_t i = 0; i < kMaxKept + 10; ++i) s.keep(0xB0200000 + i, kShip, record(7, 0xB0200000 + i, kShip), 1000 + i, st);
    CHECK(s.size() == kMaxKept && st.evicted == 10 && s.find(0xB0200000) == nullptr);
    CHECK(s.find(0xB0200000 + kMaxKept + 9) != nullptr);
    // A waiting record takes the newer bytes; dropped keys go; old ones expire.
    s.keep(0xB0200000 + 20, kHull, record(7, 0xB0200000 + 20, kHull, 0x200), 5000, st);
    CHECK(st.replaced == 1 && s.find(0xB0200000 + 20)->parent == kHull && s.find(0xB0200000 + 20)->rec.size() == 0x200);
    CHECK(s.drop(0xB0200000 + 21) && !s.drop(0xB0200000 + 21) && s.size() == kMaxKept - 1);
    c.step(s, st, 1000 + kKeepMs + 2000);
    CHECK(s.size() == 1 && s.find(0xB0200000 + 20) != nullptr);  // the one refreshed at 5000
    s.clear();
    CHECK(s.size() == 0 && s.bytes() == 0);
}

// The cap in bytes, and a record too large to keep at all.
void test_byte_cap() {
    Store s;
    Stats st;
    const size_t big = kMaxKeptBytes / 4;
    for (uint32_t i = 0; i < 4; ++i) s.keep(0xB0600000 + i, kShip, record(7, 0xB0600000 + i, kShip, big), 1000 + i, st);
    CHECK(s.size() == 4 && s.bytes() == kMaxKeptBytes && st.evicted == 0);
    s.keep(0xB0600004, kShip, record(7, 0xB0600004, kShip, big), 2000, st);
    CHECK(s.size() == 4 && st.evicted == 1 && s.find(0xB0600000) == nullptr && s.bytes() == kMaxKeptBytes);
    s.keep(0xB0600005, kShip, record(7, 0xB0600005, kShip, big + 1), 2001, st);
    CHECK(s.find(0xB0600005) == nullptr && s.size() == 4);
}

// When every kept record is being retried there is nothing to evict: a new one is not kept.
void test_full_of_retries() {
    Store s;
    Stats st;
    Client c;
    std::vector<uint32_t> all;
    for (uint32_t i = 0; i < kMaxKept; ++i) {
        s.keep(0xB0200000 + i, kShip, record(7, 0xB0200000 + i, kShip), 1000, st);
        all.push_back(0xB0200000 + i);
    }
    c.step(s, st, 1100, all);
    CHECK(st.failures == kMaxKept && st.failures_not_kept == 0);
    s.keep(0xB0400000, kShip, record(7, 0xB0400000, kShip), 2000, st);
    CHECK(s.size() == kMaxKept && s.find(0xB0400000) == nullptr && st.evicted == 0);
}

// A failed record whose parent never comes does not stay for ever.
void test_failed_one_expires() {
    Store s;
    Stats st;
    Client c;
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    const uint32_t failed[] = {kLantern};
    c.step(s, st, 1000, failed);
    c.step(s, st, 1000 + 2 * kKeepMs);
    CHECK(s.size() == 1);
    c.step(s, st, 1000 + 2 * kKeepMs + 1);
    CHECK(s.size() == 0 && st.expired == 1 && c.emitted.empty());
}

// Under pressure the records whose parent has been announced since (and is not kept itself) go first.
void test_eviction_prefers_announced_parents() {
    Store s;
    Stats st;
    const uint32_t kOther = 0xB0300000;
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);  // the oldest, its parent not announced
    s.keep(kLamp, kLantern, record(7, kLamp, kLantern), 1001, st);  // its parent is kept
    for (uint32_t i = 0; i < kMaxKept - 2; ++i)
        s.keep(0xB0200000 + i, kOther + i, record(7, 0xB0200000 + i, kOther + i), 2000 + i, st);
    CHECK(s.size() == kMaxKept);
    s.announced(kLantern);     // the lamp's parent came, but the lantern is itself kept: the lamp is not "safe"
    s.announced(kOther + 5);   // an ordinary parent arrived
    s.keep(0xB0400000, kShip, record(7, 0xB0400000, kShip), 9000, st);
    CHECK(st.evicted == 1 && s.find(0xB0200000 + 5) == nullptr);
    CHECK(s.find(kLantern) != nullptr && s.find(kLamp) != nullptr);
    // Nothing "safe" left: the oldest waiting one goes.
    s.keep(0xB0400001, kShip, record(7, 0xB0400001, kShip), 9001, st);
    CHECK(st.evicted == 2 && s.find(kLantern) == nullptr && s.find(kLamp) != nullptr);
}

void test_lost_is_only_counted() {
    Store s;
    Stats st;
    Client c;
    c.add(kShip);
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    c.step(s, st, 1000 + kLostAfterMs + 1);
    CHECK(st.lost == 1);
    c.step(s, st, 1000 + kLostAfterMs + 1 + kWaitingCheckMs);  // looked at again: counted once, never given again
    CHECK(st.lost == 1 && st.lost_retried == 0 && c.emitted.empty() && s.size() == 1);
}

// With a budget, a record that never appears although its parent is there is given again without any failure; the
// budget is per record, and a part of something still being mended waits for it.
void test_lost_retry() {
    Store s;
    Stats st;
    Client c;
    c.add(kShip);
    s.set_lost_budget(1);
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    s.keep(kLamp, kLantern, record(7, kLamp, kLantern), 1000, st);
    s.keep(kCannon, kShip, record(7, kCannon, kShip), 1000, st);
    c.step(s, st, 1000 + kLostAfterMs);  // not yet
    CHECK(c.emitted.empty() && st.lost == 0);
    c.step(s, st, 1000 + kLostAfterMs + kWaitingCheckMs);
    CHECK(st.lost == 2 && st.lost_retried == 1 && s.lost_budget() == 0);
    CHECK(c.emitted.size() == 1 && c.emitted[0] == kLantern);  // the cannon is only counted: the budget is spent
    c.add(kLantern);
    c.step(s, st, 1000 + kLostAfterMs + kWaitingCheckMs + kSettleMs);
    CHECK(st.created_after == 1 && st.parts == 1);
    c.step(s, st, 1000 + kLostAfterMs + kWaitingCheckMs + kSettleMs + kChildAfterMs);
    CHECK(c.emitted.size() == 2 && c.emitted[1] == kLamp);
}

// A failure the hook could not name: the kept record seen just before it that is missing, with its parent there,
// takes it. Only that one.
void test_unnamed_failure() {
    Store s;
    Stats st;
    Client c;
    c.add(kShip);
    c.add(kCannon);
    s.keep(kCannon, kShip, record(7, kCannon, kShip), 1000, st);
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    s.keep(kLamp, kLantern, record(7, kLamp, kLantern), 1000, st);  // its parent is not there: never guessed
    const uint32_t unnamed[] = {0};
    c.step(s, st, 1200, unnamed);
    CHECK(st.failures == 1 && st.failures_not_kept == 1 && s.credits() == 1 && c.emitted.empty());
    c.step(s, st, 1200 + kWaitingCheckMs);  // too early: the actor may still be on its way
    CHECK(c.emitted.empty() && st.guessed == 0);
    c.step(s, st, 1200 + kUnknownAfterMs);
    CHECK(st.guessed == 1 && c.emitted.size() == 1 && c.emitted[0] == kLantern && s.credits() == 0);
    // A record that comes AFTER a failure never takes its credit (the lantern's own second try goes out meanwhile).
    c.step(s, st, 8000, unnamed);
    s.keep(0xB01008A5, kShip, record(7, 0xB01008A5, kShip), 9000, st);
    c.step(s, st, 17000);
    CHECK(st.guessed == 1 && c.emitted.size() == 2 && c.emitted[1] == kLantern && s.credits() == 1);
    // Nor one seen long before the failure.
    c.add(0xB01008A5);
    s.keep(0xB01008A1, kShip, record(7, 0xB01008A1, kShip), 20000, st);
    c.step(s, st, 20000 + 2 * kUnknownWindowMs, unnamed);
    c.step(s, st, 20000 + 2 * kUnknownWindowMs + kUnknownAfterMs + kWaitingCheckMs);
    CHECK(st.guessed == 1 && c.emitted.size() == 2);
    // A credit does not outlive kUnknownCreditMs: the record's parent comes too late.
    const uint32_t kLate = 0xB0500001, kLateParent = 0xB0500000;
    s.keep(kLate, kLateParent, record(7, kLate, kLateParent), 100000, st);
    c.step(s, st, 100100, unnamed);
    CHECK(s.credits() == 1);
    c.add(kLateParent);
    c.step(s, st, 100100 + kUnknownCreditMs + 1);
    CHECK(st.guessed == 1 && s.credits() == 0 && s.find(kLate) != nullptr);
}

// A failure that names an actor no record is kept of (the name comes from stack leftovers) counts like an unnamed one.
void test_named_without_record() {
    Store s;
    Stats st;
    Client c;
    c.add(kShip);
    s.keep(kLantern, kShip, record(7, kLantern, kShip), 1000, st);
    const uint32_t failed[] = {0xB0100FFF};
    c.step(s, st, 1300, failed);
    CHECK(st.failures_not_kept == 1 && s.credits() == 1);
    c.step(s, st, 1300 + kUnknownAfterMs);
    CHECK(st.guessed == 1 && c.emitted.size() == 1 && c.emitted[0] == kLantern);
}

// Failures nothing is matched to do not pile up.
void test_max_credits() {
    Store s;
    Stats st;
    Client c;
    const std::vector<uint32_t> unnamed(kMaxCredits + 4, 0);
    c.step(s, st, 1000, unnamed);
    CHECK(st.failures == kMaxCredits + 4 && st.credits == kMaxCredits + 4 && s.credits() == kMaxCredits);
}

void test_single_record_payload() {
    const auto rec = record(7, kLantern, kShip);
    const auto p = single_record_payload(rec);
    CHECK(p.size() == rec.size() + 2 && p[0] == 0x40 && p[1] == 0x01);
    size_t n = 0;
    fam::scan_2005(p, [&](const fam::Record& r) {
        ++n;
        CHECK(r.key == kLantern && r.type == 7 && attach_parent(r) == kShip);
    });
    CHECK(n == 1);
}
}  // namespace

int main() {
    test_attach_parent();
    test_scan_orphans();
    test_retry_and_parts();
    test_gives_up_and_keeps_state();
    test_host_announces_again();
    test_registered_is_not_created();
    test_two_questions();
    test_named_but_alive();
    test_part_appears_by_itself();
    test_caps_and_expiry();
    test_byte_cap();
    test_full_of_retries();
    test_failed_one_expires();
    test_eviction_prefers_announced_parents();
    test_lost_is_only_counted();
    test_lost_retry();
    test_unnamed_failure();
    test_named_without_record();
    test_max_credits();
    test_single_record_payload();
    if (g_failures) {
        std::printf("create_retry: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("create_retry: all passed\n");
    return 0;
}
