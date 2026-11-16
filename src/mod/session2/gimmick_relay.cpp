#include "mod/session2/gimmick_relay.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "common/troc_frame.h"
#include "mod/game/build_profile.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/troc_sniffer.h"
#include "mod/session2/docked_gimmicks_logic.h"
#include "mod/session2/family_order_logic.h"
#include "mod/session2/gimmick_interact_logic.h"
#include "mod/session2/server.h"
#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2 {
namespace {

using u2f::Mode;

constexpr uint16_t kOpUpdateGimmickReq = 2758, kOpUpdateGimmickAck = 2297;  // logical opcodes
constexpr size_t kReqPayload = 4 + 0xF0;  // key | the update (0xF0 bytes)
constexpr size_t kHeader = 5;
// Build 3663 (vehicle_follow.cpp's constants).
constexpr uintptr_t kWorldSlot = 0x72451C0;
constexpr uint32_t kWorld_Mgr = 0x30, kMgr_Table = 0x08, kMgr_User = 0x58, kUser_Key = 0x60;
constexpr uint32_t kTable_Buckets = 0x98, kTable_Nodes = 0xA0, kTable_Count = 0x88, kBucketStride = 0x100;
constexpr uint32_t kActor_Comps = 0x68, kActor_Info = 0x88, kActor_Controller = 0x90, kActor_Key = 0x60;
constexpr uint32_t kComps_Hierarchy = 0x1B8, kHier_Parent = 0x1C;
// The branch state a gimmick is in (3663): four bytes of its gimmick component, as tools/porting/livetest/sailstate.py
// reads them. For the trace lines only.
constexpr uint32_t kComps_Gimmick = 0x30, kGimmick_Branch = 0x270;
constexpr uint32_t kTraceLines = 400;
constexpr uint8_t kTypeGimmick = 7, kTypeVehicle = 5;
constexpr size_t kMaxKeys = 512, kRing = 32;
constexpr size_t kMaxNodes = 1u << 15;  // gimmicks and vehicles in one snapshot; more: the snapshot is not trusted
// A vehicle is taken for a ship (and not for a mount or a cart) when at least this many gimmicks hang directly from
// it and one of them carries parts itself (the wheel's mount, a lantern). The ship of 2026-11-14 had 44 actors.
constexpr size_t kShipMinParts = 6;
// For a SAIL's states only: the AI's ships are smaller than the player's. Read live on 2026-11-16 13:50: two of them
// (0xB0301B67 and 0xB030166F, 5100 of health, the host's user their controller) had FOUR gimmicks hanging directly
// from the vehicle, two of them carrying others, where the player's ship has fourteen; their sails (0xB0301BAF,
// 0xB030166A) were in first gear on the host and furled on the guest, and with the six-part rule neither half of this
// module took them for ships: the host never listed their sails, and the guest answered "its vehicle is no ship" to
// the ones of a bigger ship that did get relayed. A state of the sail's own list (kSailStates) says by itself that
// the gimmick is a sail, so for those a vehicle with two parts, one of them a carrier, is enough.
constexpr size_t kSailShipMinParts = 2;
constexpr DWORD kScanMs = 2000;
constexpr uint32_t kMaxLines = 60;

std::atomic<uint8_t> g_mode{0};
uintptr_t g_base = 0;
HANDLE g_wake = nullptr;
// The relayed gimmicks of the host's own ships (the worker writes, the probe reads).
std::array<std::atomic<uint32_t>, kMaxKeys> g_keys;
std::atomic<uint32_t> g_key_count{0};
// The probe's copies (under g_ring_lock; the probe only ever try-locks).
struct Slot {
    uint16_t len = 0;
    uint8_t data[kReqPayload] = {};
};
SRWLOCK g_ring_lock = SRWLOCK_INIT;
std::array<Slot, kRing> g_ring;
size_t g_ring_n = 0;
std::atomic<uint64_t> c_seen{0}, c_busy{0}, c_full{0}, c_queued{0}, c_refused{0}, c_no_user2{0}, c_scans{0},
    c_repeats{0}, c_ships{0}, c_unlisted{0};
// The STATES relayed: the update's parameter, the u32 at +8 of the request. Written before the probe is set.
// The host's capture of 2026-11-14 (tools/porting/boat/gimevents.py): every gimmick's updates use the same events,
// 0x003ECC59 "enter the state in the parameter", 0x92786A19 "leave it", and 0x128B06E9 with parameter 0 once it has
// settled (and once at creation). The sail's states: 0x29A56175 furled, 0xC3738DA8 first gear, 0x0FE0996E second,
// 0x8DC1B584 third. The cannons' are others (0x1F72B718, 0x46A85472), so a shot does not make the guest that
// cannon's controller. A "settled" is relayed only for a gimmick one of whose listed states went before.
constexpr uint32_t kSailStates[] = {0x29A56175, 0xC3738DA8, 0x0FE0996E, 0x8DC1B584};
constexpr uint32_t kEventSettled = 0x128B06E9;
gil::Events g_states;
bool g_all_states = false;
std::array<uint32_t, 64> g_sails{};  // worker only: the gimmicks a listed state was relayed for
size_t g_sails_next = 0;
// The worker's memory of what it relayed last. A periodic update (the same gimmick, event and parameter again and
// again, each less than kRepeatMs after the one before) goes twice and is then left out until it has been silent for
// kRepeatMs; a change made twice in a row by hand still gets through both times.
constexpr ULONGLONG kRepeatMs = 1000;
constexpr uint8_t kRepeatsLetThrough = 1;
struct Last {
    uint32_t key = 0, event = 0, param = 0;
    ULONGLONG ms = 0;
    uint8_t streak = 0;
};
std::array<Last, 48> g_last;  // worker only
size_t g_last_next = 0;

// True: this (gimmick, event, parameter) is a periodic repeat to leave out.
bool repeated(uint32_t key, uint32_t event, uint32_t param, ULONGLONG now) {
    for (Last& l : g_last) {
        if (l.key != key || l.event != event || l.param != param) continue;
        const bool fast = now - l.ms < kRepeatMs;
        l.ms = now;
        if (!fast) {
            l.streak = 0;
            return false;
        }
        if (l.streak < 0xFF) ++l.streak;
        return l.streak > kRepeatsLetThrough;
    }
    g_last[g_last_next] = {key, event, param, now, 0};
    g_last_next = (g_last_next + 1) % g_last.size();
    return false;
}

Mode mode() { return static_cast<Mode>(g_mode.load(std::memory_order_relaxed)); }

template <class T>
T rd(uintptr_t a, T fallback = T{}) noexcept {
    T v{};
    return a > 0x10000 && game::fast_read(a, &v, sizeof v) ? v : fallback;
}

// The last "enter this state" update of a sail that this client sent, whether the gimmick was listed then or not.
// Seen on 2026-11-16 10:36 (the owner sailing next to an AI ship): its sail was in second gear on the host and furled
// on the guest, and nothing had been relayed for it. The host's client had set it (9 UpdateGimmickReq) before the
// ship was on the list (a ship is listed once six of its parts exist), and a state is only relayed when it changes.
// So the last one is kept, and sent again when its gimmick comes onto the list (also: every sail, when user 2
// arrives), a few times, because the guest needs its own copy of the ship whole before it takes the update.
constexpr uint32_t kEventEnter = 0x003ECC59;
constexpr ULONGLONG kReplayAfterMs[] = {2000, 8000, 20000};
// And again every kReassertMs while the gimmick stays listed and nothing newer went out: the guest makes its copy of a
// ship when the ship comes into ITS range (later than here, or again after it sailed away and back), and a copy
// starts furled. The host cannot see that moment, so the state is simply said again.
constexpr ULONGLONG kReassertMs = 30000;
struct Kept {
    uint32_t key = 0;
    ULONGLONG ms = 0;
    uint8_t data[kReqPayload] = {};
};
std::atomic_flag g_kept_busy;  // held for microseconds; nobody sleeps on it (sea_sync.cpp, TryLock, says why)
std::array<Kept, 256> g_kept;  // by key; the oldest is replaced
struct Replay {
    uint32_t key = 0;
    ULONGLONG due[std::size(kReplayAfterMs)] = {};
};
std::array<Replay, 48> g_replays;  // worker only
std::atomic<uint64_t> c_kept{0}, c_replayed{0}, c_reasserted{0};

bool sail_state(uint32_t param) noexcept {
    return std::find(std::begin(kSailStates), std::end(kSailStates), param) != std::end(kSailStates);
}
// The gimmicks seen to take one of the sail's own states: sails, whatever else they do. An AI ship's sail also takes
// states that are not on the list (live, 16-nov 14:01: 0xB0301BAF and 0xB030166A went from first gear through a
// branch 853c63b7 to third gear on the host, and the guest's copies, which this module had made the guest's to
// change, stayed in first gear: what took them out of first gear had not been relayed, and a sail does not go from
// first to third at one update). So everything a sail's controller says about it is relayed, not only the listed
// states. (The client's sending thread adds, the worker reads.)
std::array<std::atomic<uint32_t>, 256> g_sail_keys{};
std::atomic<uint32_t> g_sail_key_n{0};
bool is_sail(uint32_t key) noexcept {
    const uint32_t n = std::min<uint32_t>(g_sail_key_n.load(std::memory_order_acquire), static_cast<uint32_t>(g_sail_keys.size()));
    for (uint32_t i = 0; i < n; ++i)
        if (g_sail_keys[i].load(std::memory_order_relaxed) == key) return true;
    return false;
}
void sail_mark(uint32_t key) noexcept {
    if (is_sail(key)) return;
    const uint32_t i = g_sail_key_n.fetch_add(1, std::memory_order_acq_rel);
    g_sail_keys[i % g_sail_keys.size()].store(key, std::memory_order_release);
}
bool kept_take(int spins) noexcept {
    for (int i = 0; i < spins; ++i) {
        if (!g_kept_busy.test_and_set(std::memory_order_acquire)) return true;
        YieldProcessor();
    }
    return false;
}
// The client's sending thread: never sleeps (the worker holds the table for the time of one look through it).
void keep(uint32_t key, const uint8_t* data) noexcept {
    if (!kept_take(20000)) return;
    Kept* slot = &g_kept[0];
    for (Kept& k : g_kept) {
        if (k.key == key) {
            slot = &k;
            break;
        }
        if (k.ms < slot->ms) slot = &k;
    }
    slot->key = key;
    slot->ms = GetTickCount64();
    std::memcpy(slot->data, data, kReqPayload);
    g_kept_busy.clear(std::memory_order_release);
    c_kept.fetch_add(1, std::memory_order_relaxed);
}
bool kept(uint32_t key, uint8_t* data) noexcept {
    bool have = false;
    while (!kept_take(20000)) SwitchToThread();  // the worker's own thread
    for (const Kept& k : g_kept) {
        if (k.key == key && k.ms) {
            std::memcpy(data, k.data, kReqPayload);
            have = true;
            break;
        }
    }
    g_kept_busy.clear(std::memory_order_release);
    return have;
}

bool listed(uint32_t key) noexcept {
    const uint32_t n = g_key_count.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n && i < kMaxKeys; ++i)
        if (g_keys[i].load(std::memory_order_relaxed) == key) return true;
    return false;
}

// The client's sending thread: an UpdateGimmickReq of a listed gimmick is copied for the worker.
void on_post(uint16_t logical, const uint8_t* packet, uint16_t size, uint64_t) noexcept {
    if (logical != kOpUpdateGimmickReq || size != kHeader + kReqPayload) return;
    uint8_t buf[kReqPayload];
    if (!game::fast_read(reinterpret_cast<uintptr_t>(packet) + kHeader, buf, sizeof buf)) return;
    uint32_t key = 0, event = 0, param = 0;
    std::memcpy(&key, buf, 4);
    std::memcpy(&event, buf + 4, 4);
    std::memcpy(&param, buf + 8, 4);
    const bool sail = sail_state(param) ? (sail_mark(key), true) : is_sail(key);
    if (event == kEventEnter && sail) keep(key, buf);  // the last state it entered, of any kind
    if (!listed(key)) return;
    c_seen.fetch_add(1, std::memory_order_relaxed);
    if (!TryAcquireSRWLockExclusive(&g_ring_lock)) {
        c_busy.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (g_ring_n < kRing) {
        g_ring[g_ring_n].len = static_cast<uint16_t>(sizeof buf);
        std::memcpy(g_ring[g_ring_n].data, buf, sizeof buf);
        ++g_ring_n;
    } else {
        c_full.fetch_add(1, std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&g_ring_lock);
    if (g_wake) SetEvent(g_wake);
}

uintptr_t actor_by_key(uintptr_t table, uint32_t key) noexcept {
    const uint32_t n = rd<uint32_t>(table + kTable_Count);
    const uintptr_t buckets = rd<uintptr_t>(table + kTable_Buckets), nodes = rd<uintptr_t>(table + kTable_Nodes);
    if (!n || n > (1u << 24) || !buckets || !nodes) return 0;
    const uintptr_t b = buckets + static_cast<uintptr_t>(key % n) * kBucketStride;
    const uint32_t c = rd<uint32_t>(b);
    for (uint32_t i = 0; i < c && i < 31; ++i) {
        if (rd<uint32_t>(b + 8 + i * 8) != key) continue;
        const uintptr_t node = rd<uintptr_t>(nodes + static_cast<uintptr_t>(rd<uint32_t>(b + 0xC + i * 8)) * 8);
        const uintptr_t a = node ? rd<uintptr_t>(node + 8) : 0;
        return a && rd<uint32_t>(a + kActor_Key) == key ? a : 0;
    }
    return 0;
}

// ---------------------------------------------------------------- what hangs from what (both halves)
struct Node {
    uint32_t key = 0, parent = 0, ctl = 0;
    uint8_t type = 0;
};
struct Snapshot {
    std::vector<Node> nodes;        // the table's gimmicks and vehicles, by key
    std::vector<uint32_t> parents;  // sorted: the hierarchy parent of every actor of the table that has one
    const Node* find(uint32_t key) const {
        const auto it = std::lower_bound(nodes.begin(), nodes.end(), key,
                                         [](const Node& n, uint32_t k) { return n.key < k; });
        return it != nodes.end() && it->key == key ? &*it : nullptr;
    }
    bool has_children(uint32_t key) const { return std::binary_search(parents.begin(), parents.end(), key); }
    // How many gimmicks hang directly from the vehicle; 0 when none of them carries anything (no ship at all).
    size_t ship_parts(uint32_t vehicle) const {
        size_t direct = 0;
        bool carrier = false;
        for (const Node& n : nodes) {
            if (n.parent != vehicle || n.type != kTypeGimmick) continue;
            ++direct;
            carrier = carrier || has_children(n.key);
        }
        return carrier ? direct : 0;
    }
    bool ship_like(uint32_t vehicle, size_t min_parts = kShipMinParts) const { return ship_parts(vehicle) >= min_parts; }
    // The gimmick this experiment is about: nothing hangs from it, and it hangs directly from a ship (the sail, a
    // cannon; not the hull, the wheel's mount, a lantern with its lamp, nor anything of a mount). vehicle: its ship.
    bool sail_like(uint32_t key, const Node** vehicle = nullptr) const {
        const Node* g = find(key);
        const Node* v = g && g->type == kTypeGimmick ? find(g->parent) : nullptr;
        if (!v || v->type != kTypeVehicle || has_children(key) || !ship_like(v->key)) return false;
        if (vehicle) *vehicle = v;
        return true;
    }
    // The parts of the vehicle a leaf gimmick hangs directly from (ship_parts); 0: no such leaf, or no ship.
    size_t leaf_ship_parts(uint32_t key) const {
        const Node* g = find(key);
        const Node* v = g && g->type == kTypeGimmick ? find(g->parent) : nullptr;
        return v && v->type == kTypeVehicle && !has_children(key) ? ship_parts(v->key) : 0;
    }
};

// One pass over the client's actor table (guarded reads). False: unreadable, or more than kMaxNodes.
bool take_snapshot(uintptr_t table, Snapshot& s) {
    s.nodes.clear();
    s.parents.clear();
    const uint32_t n = table ? rd<uint32_t>(table + kTable_Count) : 0;
    const uintptr_t buckets = table ? rd<uintptr_t>(table + kTable_Buckets) : 0;
    const uintptr_t nodes = table ? rd<uintptr_t>(table + kTable_Nodes) : 0;
    if (!n || n > (1u << 20) || !buckets || !nodes) return false;
    for (uint32_t b = 0; b < n; ++b) {
        const uintptr_t base = buckets + static_cast<uintptr_t>(b) * kBucketStride;
        const uint32_t c = rd<uint32_t>(base);
        for (uint32_t i = 0; i < c && i < 31; ++i) {
            const uint32_t key = rd<uint32_t>(base + 8 + i * 8);
            const uintptr_t node = rd<uintptr_t>(nodes + static_cast<uintptr_t>(rd<uint32_t>(base + 0xC + i * 8)) * 8);
            const uintptr_t a = node ? rd<uintptr_t>(node + 8) : 0;
            if (!a || rd<uint32_t>(a + kActor_Key) != key) continue;
            const uintptr_t comps = rd<uintptr_t>(a + kActor_Comps);
            const uintptr_t hier = comps ? rd<uintptr_t>(comps + kComps_Hierarchy) : 0;
            const uint32_t parent = hier ? rd<uint32_t>(hier + kHier_Parent) : 0;
            if ((parent >> 28) == 0xB) s.parents.push_back(parent);
            if ((key >> 28) != 0xB) continue;
            const uintptr_t info = rd<uintptr_t>(a + kActor_Info);
            const uint8_t type = info ? rd<uint8_t>(info + 1) : 0;
            if (type != kTypeGimmick && type != kTypeVehicle) continue;
            if (s.nodes.size() >= kMaxNodes) return false;
            s.nodes.push_back({key, parent, rd<uint32_t>(a + kActor_Controller), type});
        }
    }
    std::sort(s.nodes.begin(), s.nodes.end(), [](const Node& x, const Node& y) { return x.key < y.key; });
    std::sort(s.parents.begin(), s.parents.end());
    return true;
}

// ---------------------------------------------------------------- the host's half
// The sail-like gimmicks of the ships this client's own user controls, themselves under its control.
void scan(Snapshot& snap) {
    const uintptr_t world = rd<uintptr_t>(g_base + kWorldSlot);
    const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
    const uintptr_t table = mgr ? rd<uintptr_t>(mgr + kMgr_Table) : 0;
    const uintptr_t user = mgr ? rd<uintptr_t>(mgr + kMgr_User) : 0;
    const uint32_t own = user ? rd<uint32_t>(user + kUser_Key) : 0;
    std::vector<uint32_t> found;
    std::vector<std::pair<uint32_t, bool>> ships;  // an own vehicle, and whether it is a ship
    if (own && take_snapshot(table, snap)) {
        for (const Node& g : snap.nodes) {
            if (found.size() >= kMaxKeys) break;
            if (g.type != kTypeGimmick || g.ctl != own) continue;
            const Node* v = snap.find(g.parent);
            if (!v || v->type != kTypeVehicle || v->ctl != own) continue;
            auto it = std::find_if(ships.begin(), ships.end(), [&](const auto& p) { return p.first == v->key; });
            if (it == ships.end()) {
                // Every state ("*"): the player's kind of ship only. The sail's states: any ship (kSailShipMinParts).
                it = ships.insert(ships.end(),
                                  {v->key, snap.ship_like(v->key, g_all_states ? kShipMinParts : kSailShipMinParts)});
            }
            if (it->second && !snap.has_children(g.key)) found.push_back(g.key);
        }
    }
    const uint32_t before = g_key_count.load(std::memory_order_relaxed);
    g_key_count.store(0, std::memory_order_release);
    for (size_t i = 0; i < found.size(); ++i) g_keys[i].store(found[i], std::memory_order_relaxed);
    g_key_count.store(static_cast<uint32_t>(found.size()), std::memory_order_release);
    const auto ship_count = static_cast<uint64_t>(
        std::count_if(ships.begin(), ships.end(), [](const auto& p) { return p.second; }));
    c_ships.store(ship_count, std::memory_order_relaxed);
    c_scans.fetch_add(1, std::memory_order_relaxed);
    if (before != found.size())
        log::info("GimmickRelay: {} leaf gimmick(s) hang directly from this client's own ship(s) now (user 0x{:08X}; "
                  "own vehicles with gimmicks {}, of which ships {}){}",
                  found.size(), own, ships.size(), ship_count, found.size() >= kMaxKeys ? "; the list is full" : "");
}

// The four bytes of a gimmick's branch state on this client, as sailstate.py prints them; "none": no such gimmick here.
std::string branch_text(uint32_t key) {
    const uintptr_t world = rd<uintptr_t>(g_base + kWorldSlot);
    const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
    const uintptr_t table = mgr ? rd<uintptr_t>(mgr + kMgr_Table) : 0;
    const uintptr_t a = table ? actor_by_key(table, key) : 0;
    const uintptr_t comps = a ? rd<uintptr_t>(a + kActor_Comps) : 0;
    const uintptr_t g = comps ? rd<uintptr_t>(comps + kComps_Gimmick) : 0;
    uint8_t b[4]{};
    if (!g || !game::fast_read(g + kGimmick_Branch, b, sizeof b)) return "none";
    return std::format("{:02x}{:02x}{:02x}{:02x}", b[0], b[1], b[2], b[3]);
}

DWORD WINAPI worker(LPVOID) {
    try {
        uint32_t lines = 0, trace_lines = 0;
        ULONGLONG next_scan = 0;
        Snapshot snap;
        gil::Seen<128> unlisted;  // (gimmick, event and parameter) said once
        std::vector<uint32_t> before;  // the list at the scan before (empty while there is no user 2)
        std::vector<std::pair<uint32_t, ULONGLONG>> scheduled;  // when a gimmick's replays were last set up
        std::vector<std::pair<uint32_t, ULONGLONG>> said;  // when a gimmick's state last went to user 2 (any way)
        const auto note_said = [&said](uint32_t key, ULONGLONG at) {
            auto it = std::find_if(said.begin(), said.end(), [&](const auto& x) { return x.first == key; });
            if (it != said.end()) {
                it->second = at;
            } else {
                if (said.size() >= 256) said.erase(said.begin());
                said.emplace_back(key, at);
            }
        };
        const auto put_kept = [](const uint8_t* data) {  // onto the ring, as one of the client's own updates
            bool put = false;
            AcquireSRWLockExclusive(&g_ring_lock);
            if (g_ring_n < kRing) {
                g_ring[g_ring_n].len = static_cast<uint16_t>(kReqPayload);
                std::memcpy(g_ring[g_ring_n].data, data, kReqPayload);
                ++g_ring_n;
                put = true;
            }
            ReleaseSRWLockExclusive(&g_ring_lock);
            return put;
        };
        uint32_t reassert_lines = 0;
        for (;;) {
            WaitForSingleObject(g_wake, 250);
            const ULONGLONG now = GetTickCount64();
            const bool user2 = server_live_session2() != 0;
            if (now >= next_scan) {
                next_scan = now + kScanMs;
                if (user2) {
                    scan(snap);
                    // What came onto the list: its last state goes out again (see Kept).
                    std::vector<uint32_t> listed_now;
                    const uint32_t count = g_key_count.load(std::memory_order_acquire);
                    for (uint32_t i = 0; i < count && i < kMaxKeys; ++i) {
                        listed_now.push_back(g_keys[i].load(std::memory_order_relaxed));
                    }
                    for (const uint32_t key : listed_now) {
                        if (std::find(before.begin(), before.end(), key) != before.end()) continue;
                        uint8_t data[kReqPayload];
                        if (!kept(key, data)) continue;
                        // Not again within a minute (parts come and go from the list while a ship loads).
                        auto was = std::find_if(scheduled.begin(), scheduled.end(),
                                                [&](const auto& x) { return x.first == key; });
                        if (was != scheduled.end() && now - was->second < 60000) continue;
                        if (was == scheduled.end()) {
                            if (scheduled.size() >= 256) scheduled.erase(scheduled.begin());
                            scheduled.emplace_back(key, now);
                        } else {
                            was->second = now;
                        }
                        Replay* slot = &g_replays[0];
                        for (Replay& r : g_replays) {
                            if (r.key == key || !r.key) {
                                slot = &r;
                                break;
                            }
                        }
                        slot->key = key;
                        for (size_t i = 0; i < std::size(kReplayAfterMs); ++i) slot->due[i] = now + kReplayAfterMs[i];
                        note_said(key, now + kReplayAfterMs[std::size(kReplayAfterMs) - 1]);  // the replays first
                    }
                    // Said again every kReassertMs (see there).
                    for (const uint32_t key : listed_now) {
                        const auto it = std::find_if(said.begin(), said.end(), [&](const auto& x) { return x.first == key; });
                        if (it != said.end() && (now < it->second || now - it->second < kReassertMs)) continue;
                        uint8_t data[kReqPayload];
                        if (!kept(key, data)) continue;
                        note_said(key, now);
                        if (!put_kept(data)) continue;
                        c_reasserted.fetch_add(1, std::memory_order_relaxed);
                        if (reassert_lines < 12) {
                            ++reassert_lines;
                            uint32_t param = 0;
                            std::memcpy(&param, data + 8, 4);
                            log::info("GimmickRelay: the state of 0x{:08X} (parameter 0x{:08X}) is said to user 2 again "
                                      "({} s without news of it; {} such so far)",
                                      key, param, kReassertMs / 1000, c_reasserted.load());
                        }
                    }
                    before = std::move(listed_now);
                } else {
                    if (g_key_count.load(std::memory_order_relaxed)) {
                        g_key_count.store(0, std::memory_order_release);  // nobody to relay to: the probe matches nothing
                        g_sails.fill(0);
                    }
                    before.clear();
                    scheduled.clear();
                    said.clear();
                    for (Replay& r : g_replays) r = Replay{};
                }
            }
            // The replays that are due go through the ring like the client's own updates (with the state as it is
            // kept now: a newer one of the client's replaces an older one).
            for (Replay& r : g_replays) {
                if (!r.key) continue;
                bool left = false;
                for (ULONGLONG& due : r.due) {
                    if (!due) continue;
                    if (now < due) {
                        left = true;
                        continue;
                    }
                    due = 0;
                    uint8_t data[kReqPayload];
                    if (!listed(r.key) || !kept(r.key, data)) continue;
                    if (put_kept(data)) c_replayed.fetch_add(1, std::memory_order_relaxed);
                    if (lines < kMaxLines) {
                        ++lines;
                        uint32_t param = 0;
                        std::memcpy(&param, data + 8, 4);
                        log::info("GimmickRelay: the last state of 0x{:08X} (parameter 0x{:08X}) goes to user 2 again: its "
                                  "gimmick came onto the list (a ship that appeared, or user 2 that arrived)", r.key, param);
                    }
                }
                if (!left) r = Replay{};
            }
            std::array<Slot, kRing> take;
            size_t n = 0;
            AcquireSRWLockExclusive(&g_ring_lock);
            n = g_ring_n;
            for (size_t i = 0; i < n; ++i) take[i] = g_ring[i];
            g_ring_n = 0;
            ReleaseSRWLockExclusive(&g_ring_lock);
            for (size_t i = 0; i < n; ++i) {
                const Slot& s = take[i];
                uint32_t key = 0, event = 0, param = 0;
                std::memcpy(&key, s.data, 4);
                std::memcpy(&event, s.data + 4, 4);
                std::memcpy(&param, s.data + 8, 4);
                const bool known = is_sail(key);
                const bool listed_state = g_all_states || g_states.contains(param);
                if (!listed_state && !known) {
                    c_unlisted.fetch_add(1, std::memory_order_relaxed);
                    if (trace_lines < kTraceLines) {
                        ++trace_lines;
                        log::info("GimmickRelay trace: 0x{:08X} event 0x{:08X} parameter 0x{:08X} | branch here {} | not "
                                  "relayed (no sail)", key, event, param, branch_text(key));
                    }
                    if (unlisted.note(gil::pair_key(key, event ^ (param * 0x9E3779B9u))) == 0 && lines < kMaxLines) {
                        ++lines;
                        log::info("GimmickRelay [{}]: this client's UpdateGimmickReq for 0x{:08X} (event 0x{:08X}, "
                                  "parameter 0x{:08X}) is not of a state in gimmick_relay_states: not relayed",
                                  u2f::mode_name(mode()), key, event, param);
                    }
                    continue;
                }
                if (listed_state && !known) {
                    g_sails[g_sails_next] = key;
                    g_sails_next = (g_sails_next + 1) % g_sails.size();
                }
                // News of this gimmick goes out now: no reassertion before kReassertMs after it (a replay that is
                // still due keeps its later time).
                if (listed_state) {
                    const auto it = std::find_if(said.begin(), said.end(), [&](const auto& x) { return x.first == key; });
                    if (it == said.end() || it->second < now) note_said(key, now);
                }
                // The repeat filter is for "every state" (the hull's update comes twice a second); with a list of
                // states nothing periodic gets this far, and three quick identical updates are three real ones.
                if (g_all_states && repeated(key, event, param, now)) {
                    c_repeats.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                // UpdateGimmickAck: u32 key | the Req's own payload (key | 0xF0 bytes), as the server writes it.
                std::array<uint8_t, 4 + kReqPayload> ack{};
                std::memcpy(ack.data(), &key, 4);
                std::memcpy(ack.data() + 4, s.data, kReqPayload);
                bool queued = false;
                const char* what = "probe: not queued";
                if (!user2) {
                    c_no_user2.fetch_add(1, std::memory_order_relaxed);
                    what = "no user 2: dropped";
                } else if (mode() == Mode::On) {
                    queued = server_push_extra(troc::make(kOpUpdateGimmickAck, -1, ack));
                    (queued ? c_queued : c_refused).fetch_add(1, std::memory_order_relaxed);
                    what = queued ? "queued for user 2 as an UpdateGimmickAck" : "NOT queued (user 2's queue is busy)";
                }
                if (lines < kMaxLines) {
                    ++lines;
                    log::info("GimmickRelay [{}]: this client's UpdateGimmickReq for 0x{:08X} (event 0x{:08X}, parameter "
                              "0x{:08X}): {}", u2f::mode_name(mode()), key, event, param, what);
                }
                if (trace_lines < kTraceLines) {
                    ++trace_lines;
                    log::info("GimmickRelay trace: 0x{:08X} event 0x{:08X} parameter 0x{:08X} | branch here {} | {}{}", key,
                              event, param, branch_text(key), what, listed_state ? "" : " (a sail's other state)");
                }
            }
        }
    } catch (...) {
        log::error("GimmickRelay: the worker stopped (exception)");
    }
    return 0;
}

// ---------------------------------------------------------------- the guest's half
std::atomic<uint8_t> g_take_mode{0};
std::atomic<uint64_t> c_take_acks{0}, c_take_written{0}, c_take_would{0}, c_take_own{0}, c_take_other{0},
    c_take_failed{0}, c_take_snapshots{0}, c_take_withheld{0};
// The gimmicks this client's user was written into (0: free). The network thread writes; the client's sending thread
// reads (gimmick_take_withhold).
std::array<std::atomic<uint32_t>, 128> g_taken{};
std::atomic<uint32_t> g_taken_next{0}, g_withheld_lines{0};

bool taken_has(uint32_t key) noexcept {
    if (!key) return false;  // a free slot holds 0
    for (const auto& t : g_taken)
        if (t.load(std::memory_order_relaxed) == key) return true;
    return false;
}
void taken_add(uint32_t key) noexcept {
    if (taken_has(key)) return;
    for (auto& t : g_taken) {
        uint32_t free = 0;
        if (t.compare_exchange_strong(free, key, std::memory_order_relaxed)) return;
    }
    g_taken[g_taken_next.fetch_add(1, std::memory_order_relaxed) % g_taken.size()].store(key, std::memory_order_relaxed);
}
void taken_remove(uint32_t key) noexcept {
    for (auto& t : g_taken) {
        uint32_t k = key;
        t.compare_exchange_strong(k, 0, std::memory_order_relaxed);
    }
}
void taken_clear() noexcept {
    for (auto& t : g_taken) t.store(0, std::memory_order_relaxed);
}
// Giving it back ([session2] gimmick_take_hold_ms; 0: never). Live, 14-nov: the sail's states also change by
// themselves on the client that controls it (wind, speed: the host's went from one to another and back with nobody
// touching anything), and a guest that stays the controller of its copy makes transitions of its OWN (two cycles the
// host never had; each update of the host puts it right again). So, this long after the host's last update of a taken
// gimmick, its controller on this client is written back: the copy then only changes when the host says so. The
// network thread only (take_on_ack notes, gimmick_take_tick gives back).
struct Held {
    uint32_t key = 0, controller = 0;
    ULONGLONG ack_ms = 0;
};
std::array<Held, 64> g_held{};
std::atomic<uint32_t> g_hold_ms{0};
std::atomic<uint64_t> c_take_given_back{0};
ULONGLONG g_hold_tick_ms = 0;
uint32_t g_back_lines = 0;

void held_note(uint32_t key, uint32_t controller, ULONGLONG now) {
    Held* unused = nullptr;
    for (Held& h : g_held) {
        if (h.key == key) {
            h.ack_ms = now;
            return;
        }
        if (!h.key && !unused) unused = &h;
    }
    if (unused) *unused = {key, controller, now};
}

// The mod's network thread only. What a snapshot said about a gimmick's place in its ship, for kVerdictMs (the
// controllers are read again at every Ack: they are what changes).
constexpr ULONGLONG kVerdictMs = 30000;
struct Verdict {
    uint32_t key = 0;
    ULONGLONG ms = 0;
    size_t parts = 0;  // Snapshot::leaf_ship_parts
};
std::array<Verdict, 96> g_verdicts{};
size_t g_verdict_next = 0;
Snapshot g_take_snap;
struct Said {
    uint32_t key = 0;
    const char* why = nullptr;
};
std::array<Said, 16> g_said{};  // the last lines written: one per (gimmick, outcome)
size_t g_said_next = 0;
uint32_t g_take_lines = 0, g_take_trace = 0;

bool write_u32(uintptr_t a, uint32_t v) noexcept {
    __try {
        *reinterpret_cast<volatile uint32_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Is `key` a leaf that hangs directly from a ship on this client? (a pass over the table at most every kVerdictMs
// per gimmick)
bool sail_verdict(uintptr_t table, uint32_t key, ULONGLONG now, size_t min_parts) {
    for (const Verdict& v : g_verdicts)
        if (v.key == key && now - v.ms < kVerdictMs) return v.parts >= min_parts;
    c_take_snapshots.fetch_add(1, std::memory_order_relaxed);
    const size_t parts = take_snapshot(table, g_take_snap) ? g_take_snap.leaf_ship_parts(key) : 0;
    g_verdicts[g_verdict_next] = {key, now, parts};
    g_verdict_next = (g_verdict_next + 1) % g_verdicts.size();
    return parts >= min_parts;
}

void take_on_ack(Mode m, std::span<const uint8_t> payload) {
    uint32_t key = 0, key2 = 0, param = 0;
    std::memcpy(&key, payload.data(), 4);
    std::memcpy(&key2, payload.data() + 4, 4);
    if (key != key2 || (key >> 28) != 0xB) return;
    // The update's parameter (key | key | event | parameter): one of the sail's states makes a smaller ship enough.
    if (payload.size() >= 16) std::memcpy(&param, payload.data() + 12, 4);
    // (A gimmick that took one of them once is a sail for good here too: its other states come with the smaller rule.)
    if (sail_state(param)) sail_mark(key);
    const size_t min_parts = is_sail(key) ? kSailShipMinParts : kShipMinParts;
    c_take_acks.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t world = rd<uintptr_t>(g_base + kWorldSlot);
    const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
    const uintptr_t table = mgr ? rd<uintptr_t>(mgr + kMgr_Table) : 0;
    const uintptr_t user = mgr ? rd<uintptr_t>(mgr + kMgr_User) : 0;
    const uint32_t own = user ? rd<uint32_t>(user + kUser_Key) : 0;
    const uintptr_t a = table ? actor_by_key(table, key) : 0;
    if (!own || (own >> 28) != 0x9 || !a) return;  // not here yet: asked again at its next update
    const uintptr_t info = rd<uintptr_t>(a + kActor_Info);
    const uint32_t ctl = rd<uint32_t>(a + kActor_Controller);
    const uintptr_t comps = rd<uintptr_t>(a + kActor_Comps);
    const uintptr_t hier = comps ? rd<uintptr_t>(comps + kComps_Hierarchy) : 0;
    const uint32_t parent = hier ? rd<uint32_t>(hier + kHier_Parent) : 0;
    const uintptr_t p = (parent >> 28) == 0xB ? actor_by_key(table, parent) : 0;
    const uintptr_t pinfo = p ? rd<uintptr_t>(p + kActor_Info) : 0;
    const char* why = nullptr;
    if (!info || rd<uint8_t>(info + 1) != kTypeGimmick) why = "not a gimmick";
    else if (ctl == own) {
        why = "already this client's";
        // Taken before (its vehicle is another user's): its requests stay withheld.
        const uint32_t vctl = pinfo && rd<uint8_t>(pinfo + 1) == kTypeVehicle ? rd<uint32_t>(p + kActor_Controller) : 0;
        if (m == Mode::On && (vctl >> 28) == 0x9 && vctl != own) {
            taken_add(key);
            held_note(key, vctl, GetTickCount64());  // it was its vehicle's controller that had it
        }
    }
    else if ((ctl >> 28) != 0x9) why = "its controller is no user";
    else if (!pinfo || rd<uint8_t>(pinfo + 1) != kTypeVehicle) why = "it does not hang directly from a vehicle";
    else if (rd<uint32_t>(p + kActor_Controller) != ctl) why = "its vehicle has another controller";
    else if (!sail_verdict(table, key, GetTickCount64(), min_parts))
        why = "not a leaf of a ship (something hangs from it, or its vehicle is no ship)";
    bool written = false;
    if (!why) {
        if (m == Mode::On) {
            // The same actor, still under that controller, right before the write.
            taken_add(key);  // first: the client can send a request for it as soon as it controls it
            written = actor_by_key(table, key) == a && rd<uint32_t>(a + kActor_Controller) == ctl &&
                      write_u32(a + kActor_Controller, own);
            if (!written) taken_remove(key);
            if (written) held_note(key, ctl, GetTickCount64());
            (written ? c_take_written : c_take_failed).fetch_add(1, std::memory_order_relaxed);
        } else {
            c_take_would.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        (ctl == own ? c_take_own : c_take_other).fetch_add(1, std::memory_order_relaxed);
    }
    const char* outcome = why ? why : m != Mode::On ? "would become this client's (probe)"
                                : written ? "its controller on this client is now this client's user"
                                          : "the write FAILED (or the actor changed under it)";
    if (g_take_trace < kTraceLines) {
        ++g_take_trace;
        uint32_t event = 0;
        if (payload.size() >= 12) std::memcpy(&event, payload.data() + 8, 4);
        uint8_t b[4]{};
        const uintptr_t g = comps ? rd<uintptr_t>(comps + kComps_Gimmick) : 0;
        const bool have = g && game::fast_read(g + kGimmick_Branch, b, sizeof b);
        log::info("GimmickTake trace: Ack for 0x{:08X} event 0x{:08X} parameter 0x{:08X} | branch here {} | {}{}", key, event,
                  param, have ? std::format("{:02x}{:02x}{:02x}{:02x}", b[0], b[1], b[2], b[3]) : std::string("none"),
                  why ? "left alone: " : "", outcome);
    }
    for (const Said& s : g_said)
        if (s.key == key && s.why == outcome) return;  // said before
    g_said[g_said_next] = {key, outcome};
    g_said_next = (g_said_next + 1) % g_said.size();
    if (g_take_lines >= kMaxLines) return;
    ++g_take_lines;
    log::info("GimmickTake [{}]: UpdateGimmickAck for 0x{:08X} (controller 0x{:08X}, parent 0x{:08X}, this client's "
              "user 0x{:08X}): {}{}", u2f::mode_name(m), key, ctl, parent, own, why ? "left alone: " : "", outcome);
}

}  // namespace

void gimmick_take_tick() noexcept {
    const uint32_t hold = g_hold_ms.load(std::memory_order_relaxed);
    if (!hold || static_cast<Mode>(g_take_mode.load(std::memory_order_relaxed)) != Mode::On) return;
    try {
        const ULONGLONG now = GetTickCount64();
        if (now - g_hold_tick_ms < 250) return;
        g_hold_tick_ms = now;
        uintptr_t table = 0;
        uint32_t own = 0;
        for (Held& h : g_held) {
            if (!h.key || now - h.ack_ms < hold) continue;
            if (!table) {
                const uintptr_t world = rd<uintptr_t>(g_base + kWorldSlot);
                const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
                const uintptr_t user = mgr ? rd<uintptr_t>(mgr + kMgr_User) : 0;
                table = mgr ? rd<uintptr_t>(mgr + kMgr_Table) : 0;
                own = user ? rd<uint32_t>(user + kUser_Key) : 0;
                if (!table || !own) return;  // a load: at the next tick
            }
            const uint32_t key = h.key, controller = h.controller;
            h = {};
            const uintptr_t a = actor_by_key(table, key);
            // Still this client's (nobody else changed it), and the one it goes back to is another user.
            if (!a || rd<uint32_t>(a + kActor_Controller) != own || (controller >> 28) != 0x9 || controller == own) continue;
            if (!write_u32(a + kActor_Controller, controller)) continue;
            c_take_given_back.fetch_add(1, std::memory_order_relaxed);
            g_said.fill({});  // its next take is said again
            if (g_take_trace < kTraceLines) {
                ++g_take_trace;
                uint8_t b[4]{};
                const uintptr_t comps = rd<uintptr_t>(a + kActor_Comps);
                const uintptr_t g = comps ? rd<uintptr_t>(comps + kComps_Gimmick) : 0;
                const bool have = g && game::fast_read(g + kGimmick_Branch, b, sizeof b);
                log::info("GimmickTake trace: 0x{:08X} given back; branch here now {}", key,
                          have ? std::format("{:02x}{:02x}{:02x}{:02x}", b[0], b[1], b[2], b[3]) : std::string("none"));
            }
            if (g_back_lines < 12) {
                ++g_back_lines;
                log::info("GimmickTake: 0x{:08X} goes back to its controller 0x{:08X} on this client, {} ms after the "
                          "host's last update of it (so far {} given back)", key, controller, hold,
                          c_take_given_back.load());
            }
        }
    } catch (...) {
    }
}

void gimmick_take_configure(const std::string& raw, uint32_t hold_ms) {
    const auto p = u2f::parse_mode(raw.empty() ? std::string_view("0") : std::string_view(raw));
    if (!p.known) {
        log::warn("GimmickTake: gimmick_take='{}' is not 0, probe or 1; it stays off", raw);
        return;
    }
    if (p.mode == Mode::Off) return;
    if (game::current_build() != game::Build::v3663) {
        log::info("GimmickTake: only for build 3663; off");
        return;
    }
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_hold_ms.store(std::min<uint32_t>(hold_ms, 600000));
    g_take_mode.store(static_cast<uint8_t>(p.mode));
    log::info("GimmickTake ON ({}): an UpdateGimmickAck for a leaf gimmick that hangs directly from "
              "another user's ship (the host's sail) makes this client's user that gimmick's controller on this "
              "client{}{}", u2f::mode_name(p.mode), p.mode == Mode::On ? "" : " (probe: logged, not written)",
              hold_ms ? std::format("; it goes back to its controller {} ms after the host's last update of it", hold_ms)
                      : std::string("; it stays this client's (gimmick_take_hold_ms=0)"));
}

void gimmick_take_on_in(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    const auto m = static_cast<Mode>(g_take_mode.load(std::memory_order_relaxed));
    if (m == Mode::Off) return;
    try {
        if (opcode == fam::kOpMoveField) {
            g_verdicts.fill({});  // another field: none of those actors
            g_said.fill({});
            g_held.fill({});
            taken_clear();
            return;
        }
        if (opcode == fam::kOpRemove) {
            // u8 | u32 | u8 reason | n x u32 key (keys at +6): a key the server takes away is no longer a taken one.
            for (size_t i = 6; i + 4 <= payload.size(); i += 4) taken_remove(fam::le32(&payload[i]));
            return;
        }
        if (opcode != kOpUpdateGimmickAck || payload.size() != 4 + kReqPayload) return;
        take_on_ack(m, payload);
    } catch (...) {
    }
}

bool gimmick_take_withhold(const uint8_t* packet, size_t size) noexcept {
    if (static_cast<Mode>(g_take_mode.load(std::memory_order_relaxed)) != Mode::On || size < kHeader + 4) return false;
    uint16_t op = 0;
    uint32_t key = 0;
    std::memcpy(&op, packet, 2);
    std::memcpy(&key, packet + kHeader, 4);
    // The state requests docked_gimmicks withholds, and the gimmick's own movement (2556 MoveActorReq, 2828
    // RotateActorReq, 3067 RegionInOutReq, all keyed by the actor at +0): the host stays the only one who tells the
    // server anything about it.
    const bool listed = dgl::withheld_op_index(op) >= 0 || op == 2556 || op == 2828 || op == 3067;
    if (!listed || (key >> 28) != 0xB || !taken_has(key)) return false;
    c_take_withheld.fetch_add(1, std::memory_order_relaxed);
    if (g_withheld_lines.fetch_add(1, std::memory_order_relaxed) < 24) {
        try {
            log::info("GimmickTake: this client's request {} for 0x{:08X}, a gimmick taken on this client only, is "
                      "WITHHELD (never sent to the host)", op, key);
        } catch (...) {
        }
    }
    return true;
}

void gimmick_relay_start(const std::string& raw, const std::string& states) {
    const auto p = u2f::parse_mode(raw.empty() ? std::string_view("0") : std::string_view(raw));
    if (!p.known) {
        log::warn("GimmickRelay: gimmick_relay='{}' is not 0, probe or 1; it stays off", raw);
        return;
    }
    if (p.mode == Mode::Off) return;
    if (game::current_build() != game::Build::v3663) {
        log::info("GimmickRelay: only for build 3663; off");
        return;
    }
    if (g_mode.load() != 0) return;
    std::string_view list = states;
    while (!list.empty() && (list.front() == ' ' || list.front() == '\t')) list.remove_prefix(1);
    while (!list.empty() && (list.back() == ' ' || list.back() == '\t')) list.remove_suffix(1);
    g_all_states = list == "*";
    if (!g_all_states) {
        const auto parsed = gil::parse_events(list);
        if (!parsed) {
            log::warn("GimmickRelay: gimmick_relay_states='{}' is not a list of hex state hashes (or '*'); off", states);
            return;
        }
        g_states = *parsed;
        if (!g_states.n)
            for (const uint32_t e : kSailStates) g_states.hash[g_states.n++] = e;
    }
    std::string listed = g_all_states ? std::string(" every state") : std::string();
    for (size_t i = 0; i < g_states.n; ++i) listed += std::format(" 0x{:08X}", g_states.hash[i]);
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_wake || !net::sniffer_add_post_probe(&on_post)) {
        log::error("GimmickRelay: no event or no room among the sniffer's post probes; off");
        return;
    }
    g_mode.store(static_cast<uint8_t>(p.mode));
    log::info("GimmickRelay ON ({}; EXPERIMENT): each UpdateGimmickReq this client sends for a leaf gimmick that hangs "
              "directly from one of its own ships (a sail) is {} as the UpdateGimmickAck the server would write; the "
              "guest's client applies it only to a gimmick it controls on its own copy | states relayed (the "
              "update's parameter; and the 'settled' that follows one):{}",
              u2f::mode_name(p.mode), p.mode == Mode::On ? "given to user 2" : "logged (probe: nothing queued)",
              listed);
    CloseHandle(CreateThread(nullptr, 0, worker, nullptr, 0, nullptr));
}

std::string gimmick_relay_describe() {
    if (const auto tm = static_cast<Mode>(g_take_mode.load()); tm != Mode::Off) {
        return std::format(" | gimmick take [{}]: UpdateGimmickAck seen {} | taken {} (write failed {}, would {}) | "
                           "left alone {} (already this client's {}) | table passes {} | own requests for taken "
                           "gimmicks withheld {}",
                           u2f::mode_name(tm), c_take_acks.load(), c_take_written.load(), c_take_failed.load(),
                           c_take_would.load(), c_take_other.load(), c_take_own.load(), c_take_snapshots.load(),
                           c_take_withheld.load());
    }
    if (mode() == Mode::Off) return {};
    return std::format(" | gimmick relay [{}]: own ships {}, their leaf gimmicks {} (scans {}) | their UpdateGimmickReq "
                       "seen {} (probe busy {}, ring full {}, state not listed {}, periodic repeats left out {}) | "
                       "queued for user 2 {} (refused {}, no user 2 {})",
                       u2f::mode_name(mode()), c_ships.load(), g_key_count.load(), c_scans.load(), c_seen.load(),
                       c_busy.load(), c_full.load(), c_unlisted.load(), c_repeats.load(), c_queued.load(),
                       c_refused.load(), c_no_user2.load());
}

}  // namespace cdmp::session2
