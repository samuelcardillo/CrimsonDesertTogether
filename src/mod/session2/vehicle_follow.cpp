#include "mod/session2/vehicle_follow.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <vector>

#include "common/clock_mapper.h"
#include "common/movement_layout.h"
#include "mod/game/build_profile.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/gimmick_interact_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/vehicle_follow_logic.h"

namespace cdmp::session2 {
namespace {

// The build's addresses: the two TransformSync request setters (TS*, const Pos* / const Quat*, u8 type, ...), the world
// slot, pa::ClientChildOnlyInGameActor's vtable, and the field-change controller's load state ([world+0x60]+state_off,
// in_world_state once a load has ended).
// The pending request's type at TS+0x5BC when there is none; also "no such type known" in Addrs.
constexpr uint8_t kRequestNone = 0xF;
struct Addrs {
    uintptr_t set_position, set_rotation, world_slot, vt_child_only;
    uint32_t state_off;
    uint8_t in_world_state;
    uint8_t request_type;      // the per-frame request of a vehicle drawn from its gimmick, this client NEAR it
    uint8_t request_type_far;  // the low-rate one while this client is far from it (kRequestNone: not known)
    // The request a character's movement gives its own TransformSync every frame (passengers below; kRequestNone:
    // not looked at for the build, no passenger is ever placed).
    uint8_t char_request_type;
    // The parts' scene objects (parts_pass below): the scene object's vtable and the actor component (comps+0x30) that
    // points into it. 0: not derived for the build (no part is ever touched).
    uintptr_t vt_scene_object, vt_scene_comp;
    // The vtable of an ordinary actor (pa::ClientNormalInGameActor): what the vehicle actor of an NPC's ship is (a
    // player's is a ChildOnly one, a child of its user). 0: not known for the build (only players' vehicles listed).
    uintptr_t vt_normal;
};
// Build 1.0.0.3576 (capstone on the installed PE and hardware write watches on the guest copy, 2026-11-13; see
// vehicle_follow.h and docs/ESTADO-3527.md "13-nov: BARCO").
constexpr Addrs kAddrs3576{0xB03BB0, 0xB03FB0, 0x71C0100, 0x5914D58, 0x15C, 0x18, 7, kRequestNone, kRequestNone, 0, 0, 0};
// Build 1.0.0.3663 (static, 2026-11-13; never run; docs/research/port-3663.md): both setters found by their whole
// entry bytes below (the position one is unique; the rotation one also matches 0x24A3A50, another function, and is
// the one 0x400 after the position setter as in 3576), and the position setter still stores the request as in 3576
// (pose at TS+0x5AC, type at +0x5BC, flags +0x5BD/+0x5BE: 0xB39EE7..0xB39F19); world slot by two AOB patterns
// (core_globals.py); the vtable by RTTI name (vtable_map_3576_3663.json). Checked in 3663's code: comps+0x1A0 is still
// the TransformSync (all 24 callers of the position setter load rcx = [x+0x1A0], e.g. 0x7A47B8, 0x8847FD) and the network
// pose is still used as before (0xB35084 / 0xB358C1 subtract [ts+0x550] from the current position, with ts+0x55C taken
// next to it; 0xB42A0D writes +0x550). The other object offsets below (actor table, components +0x48/+0x1B8, hierarchy
// +0x18/+0x1C) are 3576's, not re-derived (a wrong one only means no vehicle is found). The load controller grew by
// 0xF0 and gained a state (port group 1, guest.cpp: the loading state machine 0x8825B0 and five sites reading
// [[world]+0x60]+0x24C with dec al; cmp al,0x17): state at +0x24C, in the world at 0x19.
// Request types on 3663 (live, two copies, 2026-11-13 and 14): with this client NEAR the ship its vehicle gets a type-7
// request every frame, as on 3576 (14-nov 02:57, guest standing on the host's ship: {15: 303865, 7: 1842} in 2 s of
// polling TS+0x5BC); with this client FAR from it (13-nov 21:58 and 14-nov 02:23, guest at its join point 6 km away)
// it gets type 12 a few times a second (1029 in the first two minutes, none after the guest's fast travel to the
// quay), and the pose of that request already is the network one (error 0.0-0.1 m). With only 12 (the first 3663
// table, 13-nov night) nothing was followed near the ship: the guest's copy stayed at the mooring while the host
// sailed 180 m away (14-nov 02:55). The setter stores the type it is given (0xB39F01).
// Scene objects (14-nov, hardware write watch on the wheel's TS on both copies): the TransformSync tick 0xB3A3B0 (TS
// vf59) is called by the scene object's transform notification (0x39645A0 from 0x3964D30) with the scene object in
// rdx; the object's world transform comes from 0x29657F0 (parent's, through the link at +0x80, composed with the local
// one at +0x1D0); [[comps+0x30]+0x18]-0x28 is that object for all 24 parts of the ship on both copies (scene object
// vtable 0x5989550, the component's 0x59C3768).
// Characters (14-nov 03:55, both copies, 3 s of polling TS+0x5BC): the other player's character and this client's own
// get type-12 requests every frame ({15: 445412, 12: 1432} on the host for the guest's, {12: 3562} on the guest for the
// host's); the same number as the far vehicle's, on another TransformSync.
// NPC ships (14-nov 16:20, the user's battle with a 'Small Pirate Ship', 0xB010157F): its vehicle actor is a
// ClientNormalInGameActor (vtable 0x5991108, the one game_errors.cpp reads failed actors by), controlled by the host's
// user like everything the host's client simulates. The guest got all its records and a MoveActorAck a second with its
// real position, but drew it from its OWN simulation of that ship, as it did the player's before this module: "the
// guest couldn't see it". Listed now, like any ordinary vehicle actor of another user a vehicle gimmick hangs from.
constexpr Addrs kAddrs3663{0xB39E40, 0xB3A240, 0x72451C0, 0x5990BF0, 0x24C, 0x19, 7, 12, 12, 0x5989550, 0x59C3768,
                           0x5991108};
const Addrs* g_addrs = nullptr;
// Both entries start with the same three register saves; the bytes after them tell the two apart.
constexpr uint8_t kSetPositionHead[24] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                                          0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0xBF, 0x42, 0x0E};
constexpr uint8_t kSetRotationHead[23] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                                          0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0xD9};
constexpr uint32_t kPatch = 5;  // the first register save, a whole instruction (safetyhook patches 5 bytes)

constexpr uint32_t kWorld_Mgr = 0x30, kMgr_Table = 0x08, kMgr_OwnActor = 0x50, kMgr_User = 0x58, kUser_Key = 0x60;
constexpr uint32_t kTable_Buckets = 0x98, kTable_Nodes = 0xA0, kTable_Count = 0x88, kBucketStride = 0x100;
constexpr uint32_t kActor_Comps = 0x68, kActor_Info = 0x88, kActor_Controller = 0x90, kActor_Key = 0x60;
constexpr uint32_t kComps_Vehicle = 0x48, kComps_Hierarchy = 0x1B8, kComps_TS = 0x1A0;
constexpr uint32_t kHier_AttachType = 0x18, kHier_Parent = 0x1C;
constexpr uint32_t kTS_Actor = 0x08, kTS_NetPos = 0x550, kTS_NetQuat = 0x55C;
// The pending request's type (0xF = none), from the setter until the TransformSync tick consumes it (same frame).
constexpr uint32_t kTS_RequestType = 0x5BC;
constexpr uint32_t kTS_LocalQuat = 0xA4, kTS_LocalPos = 0xB4, kTS_LocalSector = 0xC0;
constexpr uint8_t kTypeGimmick = 7, kTypeVehicle = 5, kAttachVehicle = 7;
// Every vehicle actor of the other user with a vehicle gimmick hanging from it is listed, not only the one with a ship
// out: the host's stabled horses carry ten-odd each (13-nov, both copies: 0xB0100004/5/7, 9-12 gimmicks) and never get
// a vehicle request (type 7; on 3663 also 12 from far away). One entry per vehicle actor.
// NPC vehicles (ordinary actors): every one a vehicle gimmick hangs from, the nearest to this client's character
// first, after the players' own. With "ships only" (6 or more vehicle gimmicks, the first version, played by the user
// on 14-nov 16:35) the big NPC ships were followed and a small boat was not: 0xB0101080, 4 parts, drawn 25 m from
// where the host had it. What is listed is still followed only while it gets NEAR vehicle requests (position_mid).
constexpr size_t kMaxVehicles = 24, kMaxFound = 96;
// Vehicle poses over the mod's own channel (2026-11-14, the user: an NPC ship "is very laggy" on the guest). The game
// gives the other client a vehicle's pose about once a second (measured for the pirate ship: 1.0, 1.0, 1.7 s, 84 of
// 218 gaps over 1.5 s, while it went from 4 to 18 m/s and back), and the track, 300 ms behind, guesses past its newest
// sample for most of every second and stops dead once the guess is a second old. So each mod sends where its client has
// the vehicles it controls that a vehicle gimmick hangs from, the kMaxVehiclePoses nearest to a player within
// kPoseRangeM, ten times a second while they move (twice a second while they do not), and the other mod's track of a
// listed vehicle takes those samples instead.
constexpr double kPoseRangeM = 800.0;
constexpr uint64_t kVehicleChannelFreshMs = 1500, kPoseKeepaliveMs = 500;
// For this long after the other mod's last sample of a vehicle the game's own network pose of it is NOT taken: the
// track holds its last pose. The other mod sends one at least every kPoseKeepaliveMs, so a gap is this client's own
// network thread standing still (2.8 s seen with its copy behind), and the game's pose is no fresher then: its
// packets come through the same thread. With 1.5 s (until 16-nov) one such gap put the game's pose of the host's
// re-fetched ship into the track: 318 m off and 43890 m down (vfl::Track::push says what that did).
constexpr uint64_t kVehicleChannelGoneMs = 6000;
struct OwnVehicle {
    std::atomic<uint32_t> key{0};
    std::atomic<uintptr_t> ts{0};
    // The network thread only: the last pose sent and when; the pose at the last look and when it appeared (the game
    // moves a vehicle once a frame; the thread looks every few milliseconds, vehicle_follow_observe).
    vfl::Pose sent;
    uint64_t sent_ms = 0;
    vfl::Pose seen;
    uint64_t seen_ms = 0;
    uint32_t seen_key = 0;  // whose pose `seen` is (a slot can get another vehicle between two looks)
    // A look that found it far from `seen` (vfl::Track::far): taken only when the next look finds it there too. One
    // such look alone is not the vehicle (16-nov: the guest twice got one pose of the host's ship kilometres off).
    bool jump = false;
    vfl::Pose jump_pose;
    uint64_t read_ms = 0;   // the last look that could read it (a vehicle that cannot be read is not sent, moved or not)
};
constexpr uint64_t kPoseReadFreshMs = 250;
std::atomic<uint64_t> g_own_lone_jumps{0};  // looks at an own vehicle that found it far away and were not taken at once
// The other mod's clock against this one's, from its timed pose events (the network thread only).
vfl::SampleClock g_vehicle_clock;
std::atomic<uint64_t> g_vehicle_timed_events{0}, g_vehicle_clock_resets{0};
std::atomic<int64_t> g_vehicle_stamp_behind_ms{0};  // the newest sample's time behind its arrival, last event
std::array<OwnVehicle, vfl::kMaxVehiclePoses> g_own_vehicles;
std::atomic<uint32_t> g_own_vehicle_count{0};
std::atomic<uint64_t> g_vehicles_sent{0}, g_vehicle_events{0}, g_vehicle_poses_unlisted{0};
constexpr ULONGLONG kProbeMs = 100;  // ~6 frames at 60 fps
constexpr ULONGLONG kScanMs = 3000;  // between two scans of the actor table
// Set when the other player's character changes what it stands on: the worker rebuilds its chain at once instead of at
// its next 250 ms tick (auto-reset; null: the worker only sleeps).
HANDLE g_wake = nullptr;

struct Vehicle {
    std::atomic<uintptr_t> ts{0};
    std::atomic<uint32_t> key{0};
    uint32_t gimmick = 0, gimmicks = 0;  // one of the vehicle gimmicks hanging from it and their number (log only)
    bool following = false;              // worker only: a vehicle request seen (it is drawn from its gimmick)
    // A NEAR vehicle request seen for it (hook, or the worker's probe): only then are its far requests followed too.
    // The far type is also the type of a character's own per-frame request (3663: 12), and the list holds the other
    // user's mounts as well: one that moves like a character must be left to the game (never seen with the near type).
    std::atomic<bool> near_seen{false};
    std::atomic<uint64_t> far_only{0};   // far requests passed through because of that (hook)
    bool far_logged = false;             // worker only
    // Of those far requests, the ones that came while the other mod's samples of it were fresh: how far the pose
    // the game asked for was from where the other mod has it (measured only: the game's pose is left as it is).
    std::atomic<uint64_t> far_measured{0}, far_err_sum_cm{0};
    std::atomic<uint32_t> far_err_max_cm{0}, far_err_last_cm{0};
    SRWLOCK lock = SRWLOCK_INIT;
    vfl::Track track;        // under lock
    vfl::Pose drawn;         // under lock: the pose given to the last position request
    uint64_t drawn_ms = 0;   // under lock
    std::atomic<uint64_t> requests{0}, positions{0}, rotations{0}, samples{0};
    // The other mod's own samples of it (proto::EventKind::Vehicles, ten a second while it moves): when the last one
    // came, and how many. While they come, the game's ~1 Hz network pose is not put into the track.
    std::atomic<uint64_t> channel_ms{0}, channel_samples{0};
    std::atomic<uint32_t> last_err_cm{0}, max_err_cm{0};
};
std::array<Vehicle, kMaxVehicles> g_vehicles;

uintptr_t g_base = 0;
std::atomic<bool> g_started{false};
std::atomic<uint8_t> g_mode{0};
std::atomic<bool> g_guest{false};
SafetyHookMid g_pos_hook, g_rot_hook;
std::atomic<uint64_t> g_unreadable{0};
std::atomic<uint32_t> g_delay_ms{static_cast<uint32_t>(vfl::Track::kDefaultDelayMs)};

// Milliseconds on the performance counter (g_qpc_freq is set before the worker starts). GetTickCount64 steps by 10-16
// ms: above ~64 frames a second two frames in a row would get the same ship pose and the next one a double step.
int64_t g_qpc_freq = 0;
uint64_t now_ms() noexcept {
    LARGE_INTEGER c;
    if (g_qpc_freq <= 0 || !QueryPerformanceCounter(&c)) return GetTickCount64();
    const auto q = static_cast<uint64_t>(c.QuadPart), f = static_cast<uint64_t>(g_qpc_freq);
    return q / f * 1000 + q % f * 1000 / f;
}

// SEH-guarded reads without VirtualQuery (game::fast_read): the scan reads thousands of heap fields, and a
// VirtualQuery per read contends with the game's own allocations (it is also never run during a load: in_world()).
template <class T>
T rd(uintptr_t a, T fallback = T{}) noexcept {
    T v{};
    return a > 0x10000 && game::fast_read(a, &v, sizeof v) ? v : fallback;
}
bool write_bytes(uintptr_t a, const void* src, size_t n) noexcept {
    if (a < 0x10000) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(a), src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool on() noexcept { return static_cast<gil::Mode>(g_mode.load(std::memory_order_relaxed)) == gil::Mode::On; }
// A request of a vehicle drawn from its gimmick (near or far).
bool vehicle_request(uint8_t type) noexcept {
    return type == g_addrs->request_type || (g_addrs->request_type_far != kRequestNone && type == g_addrs->request_type_far);
}

// The client is in the world, not loading: the field-change controller's state is the build's last one (3576: every
// load passes 0x4..0x17 and ends at 0x18 at [[world+0x60]+0x15C], guest.cpp's 'Session2 guest load' lines).
constexpr uint32_t kWorld_FieldChange = 0x60;
bool in_world() noexcept {
    const uintptr_t world = rd<uintptr_t>(g_base + g_addrs->world_slot);
    const uintptr_t fc = world ? rd<uintptr_t>(world + kWorld_FieldChange) : 0;
    return fc && rd<uint8_t>(fc + g_addrs->state_off) == g_addrs->in_world_state;
}

Vehicle* find(uintptr_t ts) noexcept {
    if (!ts) return nullptr;
    for (auto& v : g_vehicles)
        if (v.ts.load(std::memory_order_acquire) == ts) return &v;
    return nullptr;
}

// ---------------------------------------------------------------- passengers
// The other player's character standing on a ship part (2026-11-14, 3663, two copies, host at the wheel and guest on the
// bow deck). The client draws another player's character at the WORLD pose of its Moves (its parent's world transform
// as the sender had it, composed with the local pose); it is never attached to this client's copy of the parent. So:
//  - a passenger that stands still sends no Move, and stays where its last one left it: the host saw the guest 13 m
//    astern at 4.8 m/s and 107 m astern at 12.7 m/s, in the water, until the guest's next Move;
//  - the helmsman on the guest sits 0.3-1.9 m (4 m at a sudden stop) from the wheel: its samples and the ship's are
//    drawn with different delays.
// Here: when the other player's last Move names a gimmick (type 7) that hangs from a vehicle actor (type 5), its
// character's own per-frame position and rotation requests (type 12 on 3663) get the pose of THIS client's copy of that
// gimmick (its TransformSync local poses composed up to the vehicle) composed with the Move's local pose, eased over
// kPassengerEaseMs. On foot, or on a horse (a type-5 parent), nothing is touched. Both PCs; needs the build's character
// request type (3663). probe: measured, not written.
// A passenger standing still gets few requests of its own (the helmsman on the guest: ~2 a second, 14-nov 04:35: 0.4-0.6
// m off the wheel at 3.4 m/s, 2-3 m at 13 m/s). So, with 1, every time THIS client's own character gets its per-frame
// request (the same kind, in the same part of the frame), each passenger whose own request is more than
// kPassengerIdleMs old is given one through the game's own setters, called as its movement calls them (0xB39E40:
// TS*, pos*, type, has-velocity 0, velocity null, the two flag bytes of its last own request; 0xB3A240: TS*, quat*,
// type, flag): they store the request (TS+0x5AC.. / +0x5C0..) and set bit 0 of the actor's byte +0x100 (its vf54),
// nothing else. The TransformSync is checked against its actor and key right before, and the call is SEH-guarded.
// Riders (2026-11-14, 3663): the ship's crew, ten NPC actors (type 4, 0xB keys) docked to a part of the hull. The
// server gives the other player BLANK MoveActorAcks for them too (identity transforms, no parent: guest capture of
// 08:33, one a second each), so the other client never learns where they stand: they stay where the ship was when they
// were created, over the water once it sails (that client even reports them as drowning). So the mod of the ship's
// controller sends, ten times a second, each rider's hierarchy parent and local pose (proto::EventKind::Riders:
// vehicle_follow_own_riders -> vehicle_follow_on_peer_riders), and the receiver treats each one as a passenger: the
// slots after the players' ones. A rider differs from a player's character in two things: its own requests are
// answered whatever their type (an NPC's movement is not a player's), and it has no Moves to fall back on.
constexpr size_t kMaxPlayers = 4, kMaxPassengers = kMaxPlayers + vfl::kMaxRiders, kMaxChain = 4;
constexpr ULONGLONG kRiderGoneMs = 3000;  // no state of a rider for this long: its slot is free again
// kPassengerIdleMs: shorter than any frame. A still passenger makes about two requests of its own a second; with the
// 30 ms of kit v23 each of them kept the module's request away for the next one to four frames (a hitch of 0.2 m at
// 14 m/s). A second request in the same frame carries the same pose.
constexpr ULONGLONG kPassengerEaseMs = 120, kPassengerIdleMs = 4, kPassengerIdleGapMs = 2, kChannelFreshMs = 2000;
constexpr float kPassengerSnapM = 3.0f;
using SetPositionFn = void (*)(uintptr_t ts, const void* pos, uint8_t type, uint8_t has_velocity, const void* velocity,
                               uint8_t flag_a, uint8_t flag_b);
using SetRotationFn = void (*)(uintptr_t ts, const float* quat, uint8_t type, uint8_t flag);
thread_local bool t_own_call = false;  // inside one of this module's own setter calls: the hooks pass it through
std::atomic<uintptr_t> g_own_ts{0};    // this client's own character's TransformSync (worker)

struct Passenger {
    std::atomic<uint32_t> key{0};   // the other player's character; 0: free (worker)
    std::atomic<uintptr_t> ts{0};   // its TransformSync here (worker; the hooks compare pointers)
    SRWLOCK lock = SRWLOCK_INIT;
    // Its controller's last Move: the mod's network thread writes, the hooks read (under lock).
    uint32_t parent = 0;            // 0: no parent (on foot)
    float pos[3]{}, q[4]{0, 0, 0, 1};
    // The parent's TransformSyncs on this client, from the parent up to the root vehicle, with the key of each one's
    // actor (checked at every use: a pointer can outlive its actor for a worker tick). Two slots: the worker fills the
    // one not in use and then publishes chain_state = parent key << 32 | slot << 8 | length (0: none), so a hook
    // never sees a half-written chain nor a frame without one while the other player steps from part to part.
    struct Chain {
        std::array<std::atomic<uintptr_t>, kMaxChain> ts{};
        std::array<std::atomic<uint32_t>, kMaxChain> key{};
    };
    std::array<Chain, 2> chains;
    std::atomic<uint64_t> chain_state{0};
    std::atomic<uint32_t> vehicle{0};
    // Hook side, under lock: the eased local pose and the world rotation of the last position request.
    float eased_pos[3]{}, eased_q[4]{0, 0, 0, 1}, drawn_q[4]{0, 0, 0, 1};
    ULONGLONG eased_ms = 0, drawn_ms = 0;
    uint32_t eased_for = 0;
    std::atomic<uint64_t> moves{0}, requests{0}, placed{0}, unreadable{0}, idle_placed{0}, idle_failed{0}, held{0};
    std::atomic<uint32_t> last_off_cm{0}, max_off_cm{0};
    // Its own last request (the game's) and the last one this module gave it while it stood still: when (hooks).
    std::atomic<ULONGLONG> request_ms{0}, idle_ms{0};
    // A setter call for it faulted: no more calls for this character (until the slot gets another one).
    std::atomic<bool> faulted{false};
    // The thread of its own requests, and how many of this module's calls for it ran on another one (if the game
    // updates characters on several threads, those calls race its own update: the summary says).
    std::atomic<uint32_t> own_tid{0};
    std::atomic<uint64_t> cross_thread{0};
    // The last state of it from the mod's own channel (vehicle_follow_on_peer_state): while those arrive, the game's
    // Moves of it are not used (the server blanks them for a character on a ship's deck).
    std::atomic<ULONGLONG> channel_ms{0};
    std::atomic<uint64_t> channel_states{0};
    std::atomic<uint8_t> flag_a{0}, flag_b{0}, rot_type{kRequestNone}, rot_flag{0};
    std::atomic<bool> busy{false};
    uint32_t logged_parent = 0;     // worker only
    // A rider (the other player's ship's crew), not a player's character: slots kMaxPlayers.. (the network thread
    // fills them, the worker frees them). The types of its own position requests, as bits (the summary says).
    std::atomic<bool> rider{false};
    std::atomic<uint32_t> types_seen{0};
    // A rider that hangs from something on this client too (worker; the summary counts them). It is placed like the
    // others: live, 14-nov 15:00, with the ship at 2 m/s, the guest had most of the crew with no parent (left behind,
    // 12-28 m off) and the ones it kept attached at the local pose of their creation (the lookout 30 m below its
    // mast-top, another 12 m from its station). A position request is in WORLD coordinates for an attached actor too
    // (the guest's own character on the deck, parent = the hull: its type-12 requests carry its world position;
    // boat/reqsample.py), so one request serves both.
    std::atomic<bool> attached_here{false};
};
std::array<Passenger, kMaxPassengers> g_passengers;
std::atomic<uint32_t> g_passenger_count{0}, g_rider_count{0};
std::atomic<uint64_t> g_rider_events{0}, g_rider_dropped{0}, g_riders_sent{0};
// This client's own riders (the worker lists them, the network thread reads their poses).
struct OwnRider {
    std::atomic<uint32_t> key{0};
    std::atomic<uintptr_t> ts{0};
};
std::array<OwnRider, vfl::kMaxRiders> g_own_riders;
std::atomic<uint32_t> g_own_rider_count{0};

// A slot before it gets a new character (its key is 0: nothing else looks at it).
void reset_slot(Passenger& p) noexcept {
    AcquireSRWLockExclusive(&p.lock);
    p.parent = 0;
    p.eased_for = 0;
    p.drawn_ms = 0;
    ReleaseSRWLockExclusive(&p.lock);
    p.chain_state = 0;
    p.vehicle = 0;
    p.moves = p.requests = p.placed = p.unreadable = p.idle_placed = p.idle_failed = p.held = 0;
    p.last_off_cm = p.max_off_cm = 0;
    p.request_ms = 0;
    p.idle_ms = 0;
    p.faulted = false;
    p.own_tid = 0;
    p.cross_thread = 0;
    p.channel_ms = 0;
    p.channel_states = 0;
    p.flag_a = p.flag_b = p.rot_flag = 0;
    p.rot_type = kRequestNone;
    p.logged_parent = 0;
    p.rider = false;
    p.types_seen = 0;
    p.attached_here = false;
    p.ts.store(0);
}

uintptr_t actor_by_key(uintptr_t table, uint32_t key) noexcept;
uintptr_t actor_table() noexcept;
inline uint32_t chain_parent(uint64_t state) noexcept { return static_cast<uint32_t>(state >> 32); }

// The listed character whose TransformSync this is: the pointer, and the key of the actor it belongs to now (the
// address of a character that left can be given to another actor before the worker's next tick).
Passenger* find_passenger(uintptr_t ts) noexcept {
    if (!ts || !g_passenger_count.load(std::memory_order_relaxed)) return nullptr;
    for (auto& p : g_passengers) {
        if (p.ts.load(std::memory_order_acquire) != ts) continue;
        const uintptr_t actor = rd<uintptr_t>(ts + kTS_Actor);
        const uint32_t key = p.key.load(std::memory_order_relaxed);
        return key && actor && rd<uint32_t>(actor + kActor_Key) == key ? &p : nullptr;
    }
    return nullptr;
}

// A TransformSync's local pose (world for a root: the sector added).
bool ts_pose(uintptr_t ts, bool root, vfl::Pose* out, float local_pos[3]) noexcept {
    float pos[3], q[4];
    int16_t sector[2];
    if (!game::fast_read(ts + kTS_LocalPos, pos, sizeof pos) || !game::fast_read(ts + kTS_LocalQuat, q, sizeof q) ||
        !game::fast_read(ts + kTS_LocalSector, sector, sizeof sector))
        return false;
    if (!vfl::unit_quat(q) || !std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2])) return false;
    if (root) {
        out->x = pos[0] + sector[0] * 1000.0;
        out->y = pos[1];
        out->z = pos[2] + sector[1] * 1000.0;
    } else {
        if (sector[0] || sector[1]) return false;
        std::memcpy(local_pos, pos, sizeof pos);
    }
    std::memcpy(out->q, q, sizeof q);
    return true;
}

// The world pose of the passenger's parent on this client: the chain's poses composed from the root down.
bool chain_world(const Passenger& p, uint64_t state, vfl::Pose* out) noexcept {
    const uint32_t n = static_cast<uint32_t>(state & 0xFF);
    if (!n || n > kMaxChain) return false;
    const Passenger::Chain& c = p.chains[(state >> 8) & 1];
    std::array<uintptr_t, kMaxChain> ts{};
    for (uint32_t i = 0; i < n; ++i) {
        // Still the TransformSync of the actor it was taken from.
        ts[i] = c.ts[i].load(std::memory_order_relaxed);
        const uintptr_t actor = ts[i] ? rd<uintptr_t>(ts[i] + kTS_Actor) : 0;
        const uint32_t key = c.key[i].load(std::memory_order_relaxed);
        if (!actor || !key || rd<uint32_t>(actor + kActor_Key) != key) return false;
    }
    vfl::Pose w;
    float unused[3];
    if (!ts_pose(ts[n - 1], true, &w, unused)) return false;
    for (uint32_t i = n - 1; i-- > 0;) {
        vfl::Pose l;
        float lp[3];
        if (!ts_pose(ts[i], false, &l, lp)) return false;
        w = vfl::attach(w, lp, l.q);
    }
    *out = w;
    return vfl::finite_pose(w);
}

// The passenger's world pose now (its eased local pose on this client's copy of its parent). False: not on a ship
// part, or its parent's poses are unreadable.
bool passenger_pose(Passenger& p, ULONGLONG now, vfl::Pose* out_pose) noexcept {
    float lp[3], lq[4];
    uint32_t parent = 0;
    const uint64_t state = p.chain_state.load(std::memory_order_acquire);
    const uint32_t chain_for = chain_parent(state);
    AcquireSRWLockExclusive(&p.lock);
    parent = p.parent;
    if (parent && chain_for && chain_for != parent && p.eased_for == chain_for) {
        // The other player stepped onto another part and its chain here is not built yet (the worker's next tick):
        // it stays where it was drawn on the part before, not at the game's own (stale) pose for those frames.
        parent = chain_for;
        std::memcpy(lp, p.eased_pos, sizeof lp);
        std::memcpy(lq, p.eased_q, sizeof lq);
        p.held.fetch_add(1, std::memory_order_relaxed);
    } else if (parent && chain_for == parent) {
        float d[3] = {p.pos[0] - p.eased_pos[0], p.pos[1] - p.eased_pos[1], p.pos[2] - p.eased_pos[2]};
        const float gap = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const ULONGLONG dt = now > p.eased_ms ? now - p.eased_ms : 0;  // a stamp of another thread can be later
        if (p.eased_for != parent || gap > kPassengerSnapM || dt > 1000) {
            std::memcpy(p.eased_pos, p.pos, sizeof p.pos);
            std::memcpy(p.eased_q, p.q, sizeof p.q);
        } else {
            const double a = std::min(1.0, static_cast<double>(dt) / kPassengerEaseMs);
            for (int i = 0; i < 3; ++i) p.eased_pos[i] += static_cast<float>(d[i] * a);
            float q[4];
            vfl::nlerp(p.eased_q, p.q, a, q);
            std::memcpy(p.eased_q, q, sizeof q);
        }
        p.eased_for = parent;
        p.eased_ms = now;
        std::memcpy(lp, p.eased_pos, sizeof lp);
        std::memcpy(lq, p.eased_q, sizeof lq);
    } else {
        parent = 0;
        p.eased_for = 0;
        p.drawn_ms = 0;
    }
    ReleaseSRWLockExclusive(&p.lock);
    if (!parent) return false;
    vfl::Pose base;
    if (!chain_world(p, state, &base)) {
        p.unreadable.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const vfl::Pose out = vfl::attach(base, lp, lq);
    if (!vfl::finite_pose(out) || !vfl::unit_quat(out.q)) return false;
    *out_pose = out;
    return true;
}

struct PositionArg {
    float x, y, z;
    int16_t sx, sz;
};

void passenger_position(safetyhook::Context& ctx, Passenger& p) noexcept {
    const ULONGLONG now = now_ms();
    p.types_seen.fetch_or(1u << (static_cast<uint8_t>(ctx.r8) & 31), std::memory_order_relaxed);
    p.request_ms.store(now, std::memory_order_relaxed);
    p.own_tid.store(GetCurrentThreadId(), std::memory_order_relaxed);
    p.flag_a.store(rd<uint8_t>(ctx.rsp + 0x30), std::memory_order_relaxed);
    p.flag_b.store(rd<uint8_t>(ctx.rsp + 0x38), std::memory_order_relaxed);
    vfl::Pose out;
    if (!passenger_pose(p, now, &out)) return;
    p.requests.fetch_add(1, std::memory_order_relaxed);
    PositionArg arg{};
    if (!game::fast_read(ctx.rdx, &arg, sizeof arg)) return;
    const double ox = arg.x + arg.sx * 1000.0 - out.x, oy = arg.y - out.y, oz = arg.z + arg.sz * 1000.0 - out.z;
    const auto off_cm = static_cast<uint32_t>(std::min(std::sqrt(ox * ox + oy * oy + oz * oz) * 100.0, 4.0e9));
    p.last_off_cm.store(off_cm, std::memory_order_relaxed);
    if (off_cm > p.max_off_cm.load(std::memory_order_relaxed)) p.max_off_cm.store(off_cm, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&p.lock);
    std::memcpy(p.drawn_q, out.q, sizeof out.q);
    p.drawn_ms = now;
    ReleaseSRWLockExclusive(&p.lock);
    if (!on()) return;
    PositionArg w{};
    vfl::to_sector(out.x, arg.sx, &w.x, &w.sx);
    vfl::to_sector(out.z, arg.sz, &w.z, &w.sz);
    w.y = static_cast<float>(out.y);
    if (write_bytes(ctx.rdx, &w, sizeof w)) p.placed.fetch_add(1, std::memory_order_relaxed);
}

// The two setters called for a TransformSync (no C++ objects here: SEH).
bool call_setters(uintptr_t ts, const PositionArg* pos, const float* quat, uint8_t type, uint8_t flag_a, uint8_t flag_b,
                  uint8_t rot_type, uint8_t rot_flag) noexcept {
    const auto set_position = reinterpret_cast<SetPositionFn>(g_base + g_addrs->set_position);
    const auto set_rotation = reinterpret_cast<SetRotationFn>(g_base + g_addrs->set_rotation);
    bool ok = true;
    t_own_call = true;
    __try {
        set_position(ts, pos, type, 0, nullptr, flag_a, flag_b);
        if (rot_type != kRequestNone) set_rotation(ts, quat, rot_type, rot_flag);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    t_own_call = false;
    return ok;
}

// From this client's own character's request (once a frame): the passengers that got no request of their own lately.
void passengers_idle(ULONGLONG now) noexcept {
    if (!on()) return;
    uintptr_t table = 0;
    bool table_read = false;
    for (auto& p : g_passengers) {
        const uintptr_t ts = p.ts.load(std::memory_order_acquire);
        if (!ts || p.faulted.load(std::memory_order_relaxed) || !chain_parent(p.chain_state.load(std::memory_order_relaxed)))
            continue;
        // Its own movement is asking (every frame while it moves), or it was already given one in this frame.
        // (A stamp taken on another thread can be later than `now`: that counts as "just now".)
        const ULONGLONG req = p.request_ms.load(std::memory_order_relaxed);
        if (now <= req || now - req < kPassengerIdleMs) continue;
        const ULONGLONG idle = p.idle_ms.load(std::memory_order_relaxed);
        if (now <= idle || now - idle < kPassengerIdleGapMs) continue;
        if (p.busy.exchange(true, std::memory_order_acquire)) continue;
        vfl::Pose out;
        // Still that character's TransformSync: its actor, the actor's key, the actor's components point back, AND the
        // actor table still has that actor under the key (a freed actor's memory can keep all its fields: the table is
        // what says it is alive; this call goes into the game's code with the pointer).
        const uint32_t key = p.key.load(std::memory_order_relaxed);
        const uintptr_t actor = rd<uintptr_t>(ts + kTS_Actor);
        const uintptr_t comps = actor ? rd<uintptr_t>(actor + kActor_Comps) : 0;
        bool same = key && comps && rd<uint32_t>(actor + kActor_Key) == key && rd<uintptr_t>(comps + kComps_TS) == ts;
        if (same) {
            if (!table_read) {
                table = in_world() ? actor_table() : 0;
                table_read = true;
            }
            same = table && actor_by_key(table, key) == actor;
        }
        if (same && passenger_pose(p, now, &out)) {
            int16_t sector[2]{};
            game::fast_read(ts + kTS_LocalSector, sector, sizeof sector);
            PositionArg w{};
            vfl::to_sector(out.x, sector[0], &w.x, &w.sx);
            vfl::to_sector(out.z, sector[1], &w.z, &w.sz);
            w.y = static_cast<float>(out.y);
            AcquireSRWLockExclusive(&p.lock);
            std::memcpy(p.drawn_q, out.q, sizeof out.q);
            p.drawn_ms = now;
            ReleaseSRWLockExclusive(&p.lock);
            // A rider that never asked for a rotation itself (a crew member standing at its post) still has to turn
            // with the ship: the character type, as the far vehicle requests use for both setters.
            uint8_t rot_type = p.rot_type.load(std::memory_order_relaxed);
            if (rot_type == kRequestNone && p.rider.load(std::memory_order_relaxed)) rot_type = g_addrs->char_request_type;
            if (call_setters(ts, &w, out.q, g_addrs->char_request_type, p.flag_a.load(std::memory_order_relaxed),
                             p.flag_b.load(std::memory_order_relaxed), rot_type,
                             p.rot_flag.load(std::memory_order_relaxed))) {
                p.idle_placed.fetch_add(1, std::memory_order_relaxed);
                p.idle_ms.store(now, std::memory_order_relaxed);
                const uint32_t own_tid = p.own_tid.load(std::memory_order_relaxed);
                if (own_tid && own_tid != GetCurrentThreadId()) p.cross_thread.fetch_add(1, std::memory_order_relaxed);
            } else {
                // A fault inside the game's setter: no more calls for this character (its own requests are still
                // answered: those only write the caller's argument).
                p.idle_failed.fetch_add(1, std::memory_order_relaxed);
                p.faulted.store(true, std::memory_order_release);
            }
        }
        p.busy.store(false, std::memory_order_release);
    }
}

void passenger_rotation(safetyhook::Context& ctx, Passenger& p) noexcept {
    p.rot_type.store(static_cast<uint8_t>(ctx.r8), std::memory_order_relaxed);
    p.rot_flag.store(static_cast<uint8_t>(ctx.r9), std::memory_order_relaxed);
    float q[4];
    bool have = false;
    AcquireSRWLockShared(&p.lock);
    if (p.drawn_ms && now_ms() - p.drawn_ms < 500) {
        std::memcpy(q, p.drawn_q, sizeof q);
        have = vfl::unit_quat(q);
    }
    ReleaseSRWLockShared(&p.lock);
    if (have) write_bytes(ctx.rdx, q, sizeof q);
}

// The vehicle's network pose (the controller's last sample).
bool network_pose(uintptr_t ts, vfl::Pose* p) noexcept {
    float pos[3], q[4];
    if (!game::fast_read(ts + kTS_NetPos, pos, sizeof pos) || !game::fast_read(ts + kTS_NetQuat, q, sizeof q)) return false;
    p->x = pos[0];
    p->y = pos[1];
    p->z = pos[2];
    std::memcpy(p->q, q, sizeof q);
    return vfl::usable(*p);
}

// ------------------------------------------- this client's own character on another player's ship (3663 only)
// 2026-11-15, two copies, the guest on the host's ship. What the game does by itself (the records of the user's sail
// that morning, docs/research/port-3663.md): a character that stands on a deck part hangs from it (hierarchy parent)
// and keeps its place frame after frame, at 18 m/s too. But the game takes it off the part whenever it loses the deck
// under its feet (a jump, a step off a platform, or, in a copy that draws 5-7 frames a second, the deck moving 2-3 m
// and tilting between two frames), and then it carries it with the ship only sometimes: at 08:24:52 the character
// stayed where it was in the world and the ship left from under it (the guest in the water).
//  - passenger_air_follow = 1: while the own character is in the air after leaving a part of a ship that another
//    player's mod places here, its own position request (once a frame, position_mid) is moved by what the game's
//    carry lacked (vfl::AirRide, vfl::air_carry: where its place of the frame before is now, riding the ship,
//    against its physics position), until it stands on a part again, is down at the water, stands on something
//    firm or vfl::kAirMaxS have passed; not while the ship goes slower than vfl::kAirMinSpeed. `probe` works it out
//    and counts, and writes nothing. The whole of it is in vfl::AirRide (no game memory there: the tests run it);
//    this function reads what it needs and writes the request.
//  - a record: the last kOwnRing own requests around the frame in which the game took it off the part (when, its
//    parent, the request, its physics position and the ship's pose), handed to the worker, which logs them
//    (kOwnMaxDumps times a session), and one line for each time in the air.
// Reads but for the request's three floats and two sector words, as for a passenger.
constexpr uint32_t kComps_Control = 0x40, kControl_Inner = 0x140, kInner_Physics3663 = 0x2D0, kPhysics_Position = 0x90;
constexpr uint32_t kChunkOrigin3663 = 0x7231330;
constexpr size_t kOwnRing = 24, kOwnAfter = 8;
constexpr uint32_t kOwnMaxDumps = 24, kOwnMaxAirLines = 60;
struct OwnFrame {
    uint64_t ms = 0;
    uint32_t parent = 0, root = 0;
    uint8_t attach = 0;
    bool hull_ok = false, capsule_ok = false, request_ok = false, ts_ok = false;
    double ts[3]{};       // the TransformSync's position before this request: local to the parent, world without one
    double request[3]{};  // the request as the game made it, world
    double capsule[3]{};  // the physics position, world
    vfl::Pose hull;       // the ship's pose, world
    double fix[2]{};      // what the request lacked (world x, z); zeros: nothing
    bool wrote = false;   // and it was moved by that (false with a fix: worked out only)
};
struct OwnTrace {  // the hook's own, under g_own_trace_busy
    std::array<OwnFrame, kOwnRing> ring{};
    size_t pushed = 0;
    uint64_t last_ms = 0;  // the last own request seen
    uint64_t prev_ms = 0;  // the one before this frame's (last_ms is this frame's once the hook has started)
    uint32_t last_parent = 0, root_key = 0;
    uintptr_t root_ts = 0;
    bool root_followed = false;
    int after = -1;       // frames still to record after the change; -1: none waited for
    size_t event = 0;     // `pushed` of the frame in which the parent was gone
    vfl::AirRide ride;
};
OwnTrace g_own_trace;
std::atomic_flag g_own_trace_busy = ATOMIC_FLAG_INIT;
std::atomic<uint8_t> g_air_mode{0};  // gil::Mode
// Handed to the worker: the frames in order, how many, which one saw the parent gone.
std::array<OwnFrame, kOwnRing> g_own_dump{};
size_t g_own_dump_n = 0, g_own_dump_event = 0;
std::atomic<bool> g_own_dump_ready{false};
std::atomic<uint32_t> g_own_dumps{0};
// The times in the air: written at [g_air_w % size] by the hook, read up to g_air_w by the worker.
std::array<vfl::AirDone, 8> g_air_done{};
std::atomic<uint32_t> g_air_w{0};
uint32_t g_air_r = 0, g_air_lines = 0;  // worker only
std::atomic<uint64_t> g_air_times{0}, g_air_landed{0}, g_air_fixed_frames{0}, g_air_max_fix_cm{0};

// The own character's own request (position_mid, its own type): the record and, in the air off a followed ship, the
// request moved.
void own_frame(safetyhook::Context& ctx) noexcept {
    if (g_addrs != &kAddrs3663) return;
    if (g_own_trace_busy.test_and_set(std::memory_order_acquire)) return;
    struct Release {
        ~Release() { g_own_trace_busy.clear(std::memory_order_release); }
    } release;
    OwnTrace& t = g_own_trace;
    const uintptr_t ts = ctx.rcx;
    const uintptr_t actor = rd<uintptr_t>(ts + kTS_Actor);
    const uintptr_t comps = actor ? rd<uintptr_t>(actor + kActor_Comps) : 0;
    const uintptr_t hier = comps ? rd<uintptr_t>(comps + kComps_Hierarchy) : 0;
    if (!hier) return;
    const uint32_t parent = rd<uint32_t>(hier + kHier_Parent);
    const uint64_t now = now_ms();
    if (t.last_ms && now - t.last_ms > vfl::AirRide::kStaleMs) {
        // No own request for a while (a load, the helm, nobody else here): nothing kept is about the frame before.
        // (A part it stands on now is looked up below, as one just stepped on; vfl::AirRide forgets by itself.)
        t.last_parent = 0;
        t.after = -1;
        t.root_ts = 0;
        t.root_key = 0;
        t.root_followed = false;
    }
    t.prev_ms = t.last_ms;
    t.last_ms = now;
    const bool on_part = (parent >> 28) == 0xB, was_on_part = (t.last_parent >> 28) == 0xB;
    if (!on_part && !was_on_part && t.after < 0 && !t.ride.in_air()) {
        t.last_parent = parent;
        return;
    }
    if (on_part) {
        if (parent != t.last_parent) {
            // The actor at the top of what it stands on now.
            const uintptr_t table = actor_table();
            t.root_ts = 0;
            t.root_key = 0;
            uint32_t k = parent;
            for (uint32_t depth = 0; table && k && depth <= kMaxChain; ++depth) {
                const uintptr_t pa = actor_by_key(table, k);
                const uintptr_t pcomps = pa ? rd<uintptr_t>(pa + kActor_Comps) : 0;
                const uintptr_t phier = pcomps ? rd<uintptr_t>(pcomps + kComps_Hierarchy) : 0;
                if (!pcomps) break;
                const uint32_t up = phier ? rd<uint32_t>(phier + kHier_Parent) : 0;
                if (!up) {
                    t.root_ts = rd<uintptr_t>(pcomps + kComps_TS);
                    t.root_key = k;
                    break;
                }
                k = up;
            }
        }
        // Whether another player's mod places it here: looked at in every frame (a ship is followed from the first
        // near request seen for it, which can come after the character already stands on it).
        const Vehicle* v = find(t.root_ts);
        t.root_followed = v && v->key.load(std::memory_order_relaxed) == t.root_key &&
                          v->near_seen.load(std::memory_order_relaxed);
    }
    vfl::AirFrame a;
    a.ms = now;
    a.on_part = on_part;
    a.other_parent = parent && !on_part;
    a.followed = on_part && t.root_followed;
    a.part = on_part ? parent : 0;
    a.root = t.root_key;
    double add[2] = {0, 0};
    if (!t.root_followed && t.after < 0 && !t.ride.in_air()) {
        // On something that is not another player's ship (a plank of the quay, its own ship), or just off it: the
        // game's own. AirRide is told (what it kept of a ship no longer holds).
        t.ride.frame(a, false, add);
        t.last_parent = parent;
        return;
    }
    OwnFrame f;
    f.ms = now;
    f.parent = parent;
    f.root = t.root_key;
    f.attach = rd<uint8_t>(hier + kHier_AttachType);
    {
        float p[3];
        int16_t sector[2];
        if (game::fast_read(ts + kTS_LocalPos, p, sizeof p) && game::fast_read(ts + kTS_LocalSector, sector, sizeof sector)) {
            f.ts[0] = p[0] + sector[0] * 1000.0;
            f.ts[1] = p[1];
            f.ts[2] = p[2] + sector[1] * 1000.0;
            f.ts_ok = true;
        }
    }
    PositionArg arg{};
    if (game::fast_read(ctx.rdx, &arg, sizeof arg) && std::isfinite(arg.x) && std::isfinite(arg.y) && std::isfinite(arg.z)) {
        f.request[0] = arg.x + arg.sx * 1000.0;
        f.request[1] = arg.y;
        f.request[2] = arg.z + arg.sz * 1000.0;
        f.request_ok = true;
    }
    {
        const uintptr_t control = rd<uintptr_t>(comps + kComps_Control);
        const uintptr_t inner = control ? rd<uintptr_t>(control + kControl_Inner) : 0;
        const uintptr_t physics = inner ? rd<uintptr_t>(inner + kInner_Physics3663) : 0;
        float p[3], o[3];
        if (physics && game::fast_read(physics + kPhysics_Position, p, sizeof p) &&
            game::fast_read(g_base + kChunkOrigin3663, o, sizeof o)) {
            bool finite = true;
            for (int i = 0; i < 3; ++i) {
                f.capsule[i] = static_cast<double>(p[i]) + o[i];
                finite &= std::isfinite(f.capsule[i]);
            }
            f.capsule_ok = finite;
        }
    }
    if (t.root_ts) {
        const uintptr_t ra = rd<uintptr_t>(t.root_ts + kTS_Actor);
        float unused[3];
        f.hull_ok = ra && rd<uint32_t>(ra + kActor_Key) == t.root_key && ts_pose(t.root_ts, true, &f.hull, unused);
    }

    a.hull_ok = f.hull_ok;
    a.request_ok = f.request_ok;
    a.body_ok = f.capsule_ok;
    a.hull = f.hull;
    std::memcpy(a.request, f.request, sizeof a.request);
    std::memcpy(a.body, f.capsule, sizeof a.body);
    // (With passenger_air_follow=0 it is still worked out, for the line of each time in the air; nothing is written.)
    const auto mode = static_cast<gil::Mode>(g_air_mode.load(std::memory_order_relaxed));
    const bool fix = t.ride.frame(a, mode == gil::Mode::On, add);
    // The ship's way in THIS frame. The hull's pose read above is the one the follower placed in the frame before:
    // the own character's request comes before the ship's in a frame. AirRide therefore puts the character where its
    // place was one ship's step ago, and the ship then moves on without it. Seen on 2026-11-16 06:49 (18 m/s, the
    // guest's copy at two to three frames a second): a hop of one frame, the fix 3.2 m, the ship 9.7 m on by the next
    // frame, the character 6.7 m down the deck; the next hop put it in the water. The follower knows where it is about
    // to place the ship in this frame (the same track, the same clock): the character's place is taken there too.
    // AirRide keeps counting from the place without this lead; at the next request the hull read is the pose led to,
    // so nothing lacks then and nothing is added twice.
    double lead[2] = {0, 0};
    if (mode == gil::Mode::On && t.ride.in_air() && t.ride.ship_speed() >= vfl::kAirMinSpeed && a.hull_ok &&
        a.request_ok) {
        if (Vehicle* v = find(t.root_ts)) {
            // ... and it places it LATER in the frame than this request: at two to three frames a second that was
            // 0.1 to 0.2 s (07:25: with the pose for "now" each hop still cost 1.4 to 3.4 m at 18 m/s). How much later
            // is taken from the frame before: the ship's last placement against this character's last request (never
            // more than six tenths of the frame, in case the order is ever the other way round).
            vfl::Pose next;
            AcquireSRWLockExclusive(&v->lock);
            const uint64_t drawn = v->drawn_ms;
            uint64_t ahead = 0;
            if (t.prev_ms && drawn > t.prev_ms && drawn <= now) {
                ahead = std::min<uint64_t>(drawn - t.prev_ms, (now - t.prev_ms) * 6 / 10);
            }
            const bool have = v->track.at(now + ahead, &next);
            ReleaseSRWLockExclusive(&v->lock);
            if (have && vfl::finite_pose(next) && vfl::unit_quat(next.q)) {
                const double at[3] = {a.request[0] + add[0], a.request[1], a.request[2] + add[1]};
                double l[3];
                vfl::to_local(a.hull, at, l);
                const float lf[3] = {static_cast<float>(l[0]), static_cast<float>(l[1]), static_cast<float>(l[2])};
                const float unit[4] = {0, 0, 0, 1};
                const vfl::Pose there = vfl::attach(next, lf, unit);
                const double lx = there.x - at[0], lz = there.z - at[2];
                const double m = std::hypot(lx, lz);
                if (std::isfinite(m) && m <= vfl::kAirMaxStepM) {
                    lead[0] = lx;
                    lead[1] = lz;
                }
            }
        }
    }
    if (fix || lead[0] != 0 || lead[1] != 0) {
        f.fix[0] = add[0] + lead[0];
        f.fix[1] = add[1] + lead[1];
        if (mode == gil::Mode::On) {
            PositionArg w = arg;
            vfl::to_sector(f.request[0] + f.fix[0], arg.sx, &w.x, &w.sx);
            vfl::to_sector(f.request[2] + f.fix[1], arg.sz, &w.z, &w.sz);
            f.wrote = write_bytes(ctx.rdx, &w, sizeof w);
            if (!f.wrote && fix) t.ride.unwritten();
        }
        if (fix) g_air_fixed_frames.fetch_add(1, std::memory_order_relaxed);
        const auto cm = static_cast<uint64_t>(std::hypot(f.fix[0], f.fix[1]) * 100.0);
        if (cm > g_air_max_fix_cm.load(std::memory_order_relaxed)) g_air_max_fix_cm.store(cm, std::memory_order_relaxed);
    }
    if (vfl::AirDone d; t.ride.done(&d)) {
        // A time in the air is over: its line goes to the worker.
        g_air_times.fetch_add(1, std::memory_order_relaxed);
        if (d.why == vfl::AirEnd::Landed) g_air_landed.fetch_add(1, std::memory_order_relaxed);
        const uint32_t w = g_air_w.load(std::memory_order_relaxed);
        g_air_done[w % g_air_done.size()] = d;
        g_air_w.store(w + 1, std::memory_order_release);
    }

    if (g_own_dumps.load(std::memory_order_relaxed) < kOwnMaxDumps || t.after >= 0) {
        t.ring[t.pushed % kOwnRing] = f;
        ++t.pushed;
        if (was_on_part && !on_part && t.after < 0) {
            t.after = static_cast<int>(kOwnAfter);
            t.event = t.pushed - 1;
        } else if (t.after > 0 && --t.after == 0) {
            t.after = -1;
            if (!g_own_dump_ready.load(std::memory_order_acquire)) {
                const size_t n = std::min(t.pushed, kOwnRing), first = t.pushed - n;
                for (size_t i = 0; i < n; ++i) g_own_dump[i] = t.ring[(first + i) % kOwnRing];
                g_own_dump_n = n;
                g_own_dump_event = t.event >= first ? t.event - first : 0;
                g_own_dumps.fetch_add(1, std::memory_order_relaxed);
                g_own_dump_ready.store(true, std::memory_order_release);
            }
        }
    }
    t.last_parent = parent;
}

// Worker: the frames and the times in the air the hook handed over.
void own_trace_flush() {
    for (const uint32_t w = g_air_w.load(std::memory_order_acquire); g_air_r != w; ++g_air_r) {
        if (w - g_air_r > g_air_done.size()) g_air_r = w - static_cast<uint32_t>(g_air_done.size());
        const vfl::AirDone d = g_air_done[g_air_r % g_air_done.size()];
        if (g_air_lines >= kOwnMaxAirLines) continue;
        ++g_air_lines;
        std::string end = vfl::air_end_text(d.why);
        if (d.why == vfl::AirEnd::Landed)
            end = d.off >= 0 ? std::format("stands on 0x{:08X} again, {:.2f} m across the deck from where it left it",
                                           d.to, d.off)
                             : std::format("stands on 0x{:08X}, a part of something else", d.to);
        log::info("VehicleFollow: this client's own character was in the air for {} ms after leaving 0x{:08X} of "
                  "0x{:08X}{}: in {} of its {} frames the game had carried it less than the ship went, and its "
                  "position request {} by {:.2f} m in all ({:.2f} m at most); it ended: {}",
                  d.air_ms, d.from, d.root,
                  d.ship_m >= 0 ? std::format(" (the ship went {:.1f} m meanwhile)", d.ship_m) : std::string(),
                  d.fixed, d.frames, d.wrote ? "was moved" : "would have been moved", d.total_fix, d.max_fix, end);
    }
    if (!g_own_dump_ready.load(std::memory_order_acquire)) return;
    const size_t n = std::min(g_own_dump_n, kOwnRing), ev = std::min(g_own_dump_event, n ? n - 1 : 0);
    if (n) {
        const OwnFrame& e = g_own_dump[ev];
        const uint32_t from = ev ? g_own_dump[ev - 1].parent : 0;
        log::info("VehicleFollow: the game took this client's own character off 0x{:08X}, a part of 0x{:08X} (record {} "
                  "of {} at most): its own position request in each frame around it, in the ship's frame (frame 0 is "
                  "the first without the part)", from, e.root, g_own_dumps.load(), kOwnMaxDumps);
        for (size_t i = 0; i < n; ++i) {
            const OwnFrame& f = g_own_dump[i];
            std::string where;
            if (f.hull_ok) {
                double r[3], c[3];
                if (f.request_ok) {
                    vfl::to_local(f.hull, f.request, r);
                    where += std::format(" | asked for ({:.2f}, {:.2f}, {:.2f})", r[0], r[1], r[2]);
                    if (f.fix[0] != 0 || f.fix[1] != 0) {
                        const double moved[3] = {f.request[0] + f.fix[0], f.request[1], f.request[2] + f.fix[1]};
                        vfl::to_local(f.hull, moved, r);
                        where += std::format(", {} {:.2f} m to ({:.2f}, {:.2f}, {:.2f})",
                                             f.wrote ? "moved" : "not moved; it lacked",
                                             std::hypot(f.fix[0], f.fix[1]), r[0], r[1], r[2]);
                    }
                }
                if (f.capsule_ok) {
                    vfl::to_local(f.hull, f.capsule, c);
                    where += std::format(" | physics at ({:.2f}, {:.2f}, {:.2f})", c[0], c[1], c[2]);
                }
                if (i && g_own_dump[i - 1].hull_ok) {
                    const vfl::Pose& b = g_own_dump[i - 1].hull;
                    const double dot = std::fabs(f.hull.q[0] * b.q[0] + f.hull.q[1] * b.q[1] + f.hull.q[2] * b.q[2] +
                                                 f.hull.q[3] * b.q[3]);
                    where += std::format(" | ship {:.2f} m on, {:+.2f} m up, turned {:.1f} deg",
                                         std::hypot(f.hull.x - b.x, f.hull.z - b.z), f.hull.y - b.y,
                                         2.0 * std::acos(std::min(1.0, dot)) * 57.29578);
                    // Where the physics position is against the request of the frame before (as it was left after
                    // this module), in the world: the way the game carried it since.
                    const OwnFrame& p = g_own_dump[i - 1];
                    if (f.capsule_ok && p.request_ok)
                        where += std::format(" | physics {:.2f} m from the request before",
                                             std::hypot(f.capsule[0] - (p.request[0] + (p.wrote ? p.fix[0] : 0.0)),
                                                        f.capsule[2] - (p.request[2] + (p.wrote ? p.fix[1] : 0.0))));
                }
            } else {
                where = " | the ship's pose could not be read";
            }
            log::info("VehicleFollow:   frame {:+d} ({} ms): parent 0x{:08X} | was ({:.2f}, {:.2f}, {:.2f}) {}{}",
                      static_cast<int>(i) - static_cast<int>(ev), i ? f.ms - g_own_dump[i - 1].ms : 0, f.parent,
                      f.ts[0], f.ts[1], f.ts[2], (f.parent >> 28) == 0xB ? "on the part" : "in the world", where);
        }
    }
    g_own_dump_ready.store(false, std::memory_order_release);
}

// Position request: TS in rcx, the pose {f32 x, y, z; i16 sector x, z} at rdx, the type in r8b.
void position_mid(safetyhook::Context& ctx) {
    if (t_own_call) return;
    const auto type = static_cast<uint8_t>(ctx.r8);
    if (type == g_addrs->char_request_type && type != kRequestNone) {
        if (Passenger* p = find_passenger(ctx.rcx)) {
            passenger_position(ctx, *p);
            return;
        }
        if (ctx.rcx == g_own_ts.load(std::memory_order_relaxed) && g_passenger_count.load(std::memory_order_relaxed)) {
            own_frame(ctx);
            passengers_idle(now_ms());
            return;
        }
    } else if (g_rider_count.load(std::memory_order_relaxed)) {
        // A rider's own request of another type: answered the same way.
        if (Passenger* p = find_passenger(ctx.rcx); p && p->rider.load(std::memory_order_relaxed)) {
            passenger_position(ctx, *p);
            return;
        }
    }
    if (!vehicle_request(type)) return;
    Vehicle* v = find(ctx.rcx);
    if (!v) return;
    const uintptr_t actor = rd<uintptr_t>(ctx.rcx + kTS_Actor);
    if (!actor || rd<uint32_t>(actor + kActor_Key) != v->key.load(std::memory_order_relaxed)) return;
    if (type == g_addrs->request_type) {
        v->near_seen.store(true, std::memory_order_relaxed);
    } else if (!v->near_seen.load(std::memory_order_relaxed)) {
        // A far request (the type a character's own movement uses too) of a listed vehicle never drawn from its
        // gimmick here: a mount, a cart, or a ship this client has not been near yet. The game's own.
        v->far_only.fetch_add(1, std::memory_order_relaxed);
        // Measured, not moved (2026-11-15: the guest had the AI's carts by the quay 2-10 m from where the host had
        // them, but that was read with the guest's copy behind): while the other mod's samples of it are fresh, how
        // far the pose the game asks for is from them.
        const uint64_t now = now_ms();
        const uint64_t channel = v->channel_ms.load(std::memory_order_relaxed);
        if (!channel || now - channel >= kVehicleChannelFreshMs) return;
        PositionArg arg{};
        if (!game::fast_read(ctx.rdx, &arg, sizeof arg) || !std::isfinite(arg.x) || !std::isfinite(arg.z)) return;
        vfl::Pose there;
        AcquireSRWLockShared(&v->lock);
        // (Track::at gives the pose its delay ago: asked for that much later, it gives the pose now, past the newest
        // sample by its speed. Against the delayed pose a cart at 4 m/s would read 1.2 m with a perfect game pose.)
        const bool have = v->track.at(now + g_delay_ms.load(std::memory_order_relaxed), &there);
        ReleaseSRWLockShared(&v->lock);
        if (!have) return;
        vfl::Pose asked;
        asked.x = arg.x + arg.sx * 1000.0;
        asked.z = arg.z + arg.sz * 1000.0;
        const auto cm = static_cast<uint32_t>(std::min(vfl::dist_xz(asked, there) * 100.0, 4.0e9));
        v->far_measured.fetch_add(1, std::memory_order_relaxed);
        v->far_err_sum_cm.fetch_add(cm, std::memory_order_relaxed);
        v->far_err_last_cm.store(cm, std::memory_order_relaxed);
        if (cm > v->far_err_max_cm.load(std::memory_order_relaxed)) v->far_err_max_cm.store(cm, std::memory_order_relaxed);
        return;
    }
    v->requests.fetch_add(1, std::memory_order_relaxed);
    const uint64_t now = now_ms();
    // The other mod's samples of it, while they come (vehicle_follow_on_peer_vehicles puts them in the track); else
    // the game's own network pose, about one a second.
    const uint64_t channel = v->channel_ms.load(std::memory_order_relaxed);
    const bool from_channel = channel && now - channel < kVehicleChannelGoneMs;
    vfl::Pose net;
    if (!from_channel && !network_pose(ctx.rcx, &net)) {
        g_unreadable.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    struct Arg {
        float x, y, z;
        int16_t sx, sz;
    } arg{};
    if (!game::fast_read(ctx.rdx, &arg, sizeof arg)) return;
    vfl::Pose out;
    bool have = false;
    AcquireSRWLockExclusive(&v->lock);
    if (!from_channel && v->track.push(now, net)) v->samples.fetch_add(1, std::memory_order_relaxed);
    have = v->track.at(now, &out);
    if (have) {
        v->drawn = out;
        v->drawn_ms = now;
    }
    ReleaseSRWLockExclusive(&v->lock);
    if (!have) return;
    vfl::Pose local;
    local.x = arg.x + arg.sx * 1000.0;
    local.z = arg.z + arg.sz * 1000.0;
    const auto err_cm = static_cast<uint32_t>(std::min(vfl::dist_xz(local, out) * 100.0, 4.0e9));
    v->last_err_cm.store(err_cm, std::memory_order_relaxed);
    if (err_cm > v->max_err_cm.load(std::memory_order_relaxed)) v->max_err_cm.store(err_cm, std::memory_order_relaxed);
    if (!on()) return;
    Arg w{};
    vfl::to_sector(out.x, arg.sx, &w.x, &w.sx);
    vfl::to_sector(out.z, arg.sz, &w.z, &w.sz);
    w.y = static_cast<float>(out.y);
    if (write_bytes(ctx.rdx, &w, sizeof w)) v->positions.fetch_add(1, std::memory_order_relaxed);
}

// Rotation request: TS in rcx, the quaternion at rdx, the type in r8b. Uses the pose of the position request that
// came just before it (the same update).
void rotation_mid(safetyhook::Context& ctx) {
    if (t_own_call || !on()) return;
    const auto type = static_cast<uint8_t>(ctx.r8);
    // A character's rotation request: whatever type its movement gives it (kept for this module's own calls).
    if (Passenger* p = find_passenger(ctx.rcx)) {
        passenger_rotation(ctx, *p);
        return;
    }
    if (!vehicle_request(type)) return;
    Vehicle* v = find(ctx.rcx);
    if (!v || !v->near_seen.load(std::memory_order_relaxed)) return;
    const uintptr_t actor = rd<uintptr_t>(ctx.rcx + kTS_Actor);
    if (!actor || rd<uint32_t>(actor + kActor_Key) != v->key.load(std::memory_order_relaxed)) return;
    float q[4];
    bool have = false;
    AcquireSRWLockShared(&v->lock);
    if (v->drawn_ms && now_ms() - v->drawn_ms < 500) {
        std::memcpy(q, v->drawn.q, sizeof q);
        have = vfl::unit_quat(q);
    }
    ReleaseSRWLockShared(&v->lock);
    if (have && write_bytes(ctx.rdx, q, sizeof q)) v->rotations.fetch_add(1, std::memory_order_relaxed);
}

struct Found {
    uint32_t vehicle, gimmick, gimmicks;
    uintptr_t ts;
    bool npc;        // an ordinary actor (an NPC's vehicle), not a player's
    double dist_m = 0;  // an NPC's: metres from this client's character (its last network pose; 1e18: unknown;
                        // `far` and `near` are macros of windows.h)
};

uintptr_t actor_by_key(uintptr_t table, uint32_t key) noexcept {
    const uint32_t n = rd<uint32_t>(table + kTable_Count);
    const uintptr_t buckets = rd<uintptr_t>(table + kTable_Buckets), nodes = rd<uintptr_t>(table + kTable_Nodes);
    if (!n || n > (1u << 24) || !buckets || !nodes) return 0;
    const uintptr_t b = buckets + static_cast<uintptr_t>(key % n) * kBucketStride;
    const uint32_t c = rd<uint32_t>(b);
    for (uint32_t i = 0; i < c && i < 31; ++i) {
        if (rd<uint32_t>(b + 8 + i * 8) != key) continue;
        const uint32_t idx = rd<uint32_t>(b + 0xC + i * 8);
        const uintptr_t node = rd<uintptr_t>(nodes + static_cast<uintptr_t>(idx) * 8);
        return node ? rd<uintptr_t>(node + 8) : 0;
    }
    return 0;
}

// The vehicle actors another user controls that a vehicle gimmick hangs from: every type-7 actor with a vehicle
// component hanging (attach type 7) from a type-5 ChildOnly actor whose controller is not this client's user. One entry
// per vehicle actor (most are stabled horses with their gear; the one with a ship out is told apart by its requests).
std::vector<std::pair<uint32_t, uintptr_t>> g_scan_mine;  // worker only: this client's own vehicles of the last scan
std::vector<Found> scan(uint32_t* own_out, uint32_t* dropped_out) {
    std::vector<Found> out;
    g_scan_mine.clear();
    const uintptr_t world = rd<uintptr_t>(g_base + g_addrs->world_slot);
    const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
    const uintptr_t table = mgr ? rd<uintptr_t>(mgr + kMgr_Table) : 0;
    const uintptr_t user = mgr ? rd<uintptr_t>(mgr + kMgr_User) : 0;
    const uint32_t own = user ? rd<uint32_t>(user + kUser_Key) : 0;
    *own_out = own;
    *dropped_out = 0;
    if (!table || !own) return out;
    const uint32_t n = rd<uint32_t>(table + kTable_Count);
    const uintptr_t buckets = rd<uintptr_t>(table + kTable_Buckets), nodes = rd<uintptr_t>(table + kTable_Nodes);
    if (!n || n > (1u << 20) || !buckets || !nodes) return out;
    for (uint32_t b = 0; b < n; ++b) {
        const uintptr_t base = buckets + static_cast<uintptr_t>(b) * kBucketStride;
        const uint32_t c = rd<uint32_t>(base);
        for (uint32_t i = 0; i < c && i < 31; ++i) {
            const uint32_t idx = rd<uint32_t>(base + 0xC + i * 8);
            const uintptr_t node = rd<uintptr_t>(nodes + static_cast<uintptr_t>(idx) * 8);
            const uintptr_t a = node ? rd<uintptr_t>(node + 8) : 0;
            const uintptr_t info = a ? rd<uintptr_t>(a + kActor_Info) : 0;
            if (!info || rd<uint8_t>(info + 1) != kTypeGimmick) continue;
            const uintptr_t comps = rd<uintptr_t>(a + kActor_Comps);
            const uintptr_t hier = comps && rd<uintptr_t>(comps + kComps_Vehicle) ? rd<uintptr_t>(comps + kComps_Hierarchy) : 0;
            if (!hier || rd<uint8_t>(hier + kHier_AttachType) != kAttachVehicle) continue;
            const uint32_t parent_key = rd<uint32_t>(hier + kHier_Parent);
            const auto listed = std::find_if(out.begin(), out.end(), [&](const Found& f) { return f.vehicle == parent_key; });
            if (listed != out.end()) {
                ++listed->gimmicks;
                continue;
            }
            const uintptr_t p = parent_key ? actor_by_key(table, parent_key) : 0;
            const uintptr_t pinfo = p ? rd<uintptr_t>(p + kActor_Info) : 0;
            const uintptr_t vt = p ? rd<uintptr_t>(p) : 0;
            const bool npc = g_addrs->vt_normal && vt == g_base + g_addrs->vt_normal;
            if (!pinfo || !(npc || vt == g_base + g_addrs->vt_child_only) || rd<uint8_t>(pinfo + 1) != kTypeVehicle) continue;
            const uint32_t ctl = rd<uint32_t>(p + kActor_Controller);
            if (ctl == own) {
                // One of this client's own: its pose goes to the other mod (own_vehicles_publish).
                bool noted = false;
                for (const auto& m : g_scan_mine) noted |= m.first == parent_key;
                const uintptr_t mcomps = noted ? 0 : rd<uintptr_t>(p + kActor_Comps);
                const uintptr_t mts = mcomps ? rd<uintptr_t>(mcomps + kComps_TS) : 0;
                if (mts && rd<uintptr_t>(mts + kTS_Actor) == p && g_scan_mine.size() < kMaxFound)
                    g_scan_mine.emplace_back(parent_key, mts);
                continue;
            }
            if (!ctl) continue;
            const uintptr_t pcomps = rd<uintptr_t>(p + kActor_Comps);
            const uintptr_t ts = pcomps ? rd<uintptr_t>(pcomps + kComps_TS) : 0;
            if (!ts || rd<uintptr_t>(ts + kTS_Actor) != p) continue;
            if (out.size() == kMaxFound) {
                ++*dropped_out;
                continue;
            }
            out.push_back({parent_key, rd<uint32_t>(a + kActor_Key), 1, ts, npc});
        }
    }
    // The players' first, then the NPCs' by distance from this client's character; no more than the list holds.
    {
        const uintptr_t own_actor = rd<uintptr_t>(mgr + kMgr_OwnActor);
        const uintptr_t own_comps = own_actor ? rd<uintptr_t>(own_actor + kActor_Comps) : 0;
        const uintptr_t own_ts = own_comps ? rd<uintptr_t>(own_comps + kComps_TS) : 0;
        vfl::Pose me;
        const bool have_me = own_ts && network_pose(own_ts, &me);
        for (Found& f : out) {
            vfl::Pose at;
            f.dist_m = f.npc && have_me && network_pose(f.ts, &at) ? std::hypot(at.x - me.x, at.z - me.z) : f.npc ? 1e18 : 0;
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Found& a, const Found& b) {
        return a.npc != b.npc ? !a.npc : a.dist_m < b.dist_m;
    });
    if (out.size() > kMaxVehicles) {
        *dropped_out += static_cast<uint32_t>(out.size() - kMaxVehicles);
        out.resize(kMaxVehicles);
    }
    return out;
}

// ---------------------------------------------------------------- the parts of a followed ship
// The ship's wheel on the other player's client (2026-11-14, 3663, two copies; the user's report "the ship wheel is
// flying"): the wheel gimmick hangs from its mount's socket (hierarchy attach type 6; the mount hangs from the vehicle
// at (0, 2.94, 1.85)). On the controller's client its scene object's local transform is (0, 0.81, 0.29); on the other
// client the same object is created with (0, 3.76, 2.14), its offset from the VEHICLE, so it is drawn 2.9 m above the
// deck and 1.9 m astern. Nothing rewrites it afterwards (30 s of write watch on both copies: no write), and the
// TransformSync only copies it (a write to the TS is undone within a tick; one write to the scene object's local
// position stays, and the TS, the world pose and the picture follow). The controller's local pose of the part arrives
// about once a second in its MoveActorAck, which the client never applies to a gimmick.
// So: for every gimmick attached by a socket (attach 6) under a followed vehicle, when its scene object's local position
// is more than kPartOffM (0.5 m; a rope ladder hangs 0.33 m apart on the two copies) from the controller's last one
// (same parent), the position is written once into the scene
// object. At most kMaxPartFixes times per part (something else would be moving it). Only where the build's scene
// object is known (3663). probe: logged, not written.
constexpr uint16_t kOpMoveActorAck = 2697, kOpMoveActorReq = 2556;  // logical opcodes
constexpr uint32_t kComps_SceneComp = 0x30, kSceneComp_Node = 0x18, kNode_ToObject = 0x28;
constexpr uint32_t kScene_Link = 0x80, kScene_Local = 0x1D0, kXform_Pos = 0x1C, kXform_Sector = 0x28;
constexpr uint8_t kAttachSocket = 6;
constexpr size_t kMaxParts = 48;
constexpr uint32_t kMaxPartFixes = 5;
constexpr float kPartOffM = 0.5f, kPartSettledM = 0.05f;
constexpr ULONGLONG kPartPoseMaxAgeMs = 60000;

struct Part {
    std::atomic<uint32_t> key{0};  // 0: free. Written by the worker; read by the network thread
    // worker only
    uint32_t parent = 0, vehicle = 0, fixes = 0;
    bool seen = false, gave_up = false, probe_logged = false;
    // the controller's last local position of it: under g_parts_lock (network thread writes, worker reads)
    uint32_t net_parent = 0;
    float net_pos[3]{};
    ULONGLONG net_ms = 0;
};
std::array<Part, kMaxParts> g_parts;
SRWLOCK g_parts_lock = SRWLOCK_INIT;
std::atomic<uint32_t> g_part_count{0};
std::atomic<uint64_t> g_part_acks{0}, g_part_fixes{0};

float dist3(const float* a, const float* b) noexcept {
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}

// The scene object of an actor's components, or 0 (not the build's classes, or not attached to a parent node).
uintptr_t scene_object(uintptr_t comps) noexcept {
    const uintptr_t comp = rd<uintptr_t>(comps + kComps_SceneComp);
    if (!comp || rd<uintptr_t>(comp) != g_base + g_addrs->vt_scene_comp) return 0;
    const uintptr_t node = rd<uintptr_t>(comp + kSceneComp_Node);
    const uintptr_t so = node > kNode_ToObject ? node - kNode_ToObject : 0;
    if (!so || rd<uintptr_t>(so) != g_base + g_addrs->vt_scene_object || !rd<uintptr_t>(so + kScene_Link)) return 0;
    return so;
}

// The followed vehicle an actor hangs from (at most 4 levels up), or 0.
uint32_t followed_ancestor(uintptr_t table, uint32_t parent_key) noexcept {
    for (int depth = 0; depth < 4 && (parent_key >> 28) == 0xB; ++depth) {
        for (auto& v : g_vehicles)
            if (v.following && v.ts.load() && v.key.load() == parent_key) return parent_key;
        const uintptr_t p = actor_by_key(table, parent_key);
        const uintptr_t pcomps = p ? rd<uintptr_t>(p + kActor_Comps) : 0;
        const uintptr_t phier = pcomps ? rd<uintptr_t>(pcomps + kComps_Hierarchy) : 0;
        parent_key = phier ? rd<uint32_t>(phier + kHier_Parent) : 0;
    }
    return 0;
}

void parts_pass() {
    if (!g_addrs->vt_scene_object) return;
    bool any = false;
    for (auto& v : g_vehicles) any |= v.following && v.ts.load() != 0;
    if (!any && !g_part_count.load()) return;
    for (auto& p : g_parts) p.seen = false;
    const uintptr_t world = rd<uintptr_t>(g_base + g_addrs->world_slot);
    const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
    const uintptr_t table = mgr ? rd<uintptr_t>(mgr + kMgr_Table) : 0;
    const uint32_t n = table ? rd<uint32_t>(table + kTable_Count) : 0;
    const uintptr_t buckets = table ? rd<uintptr_t>(table + kTable_Buckets) : 0, nodes = table ? rd<uintptr_t>(table + kTable_Nodes) : 0;
    if (any && n && n <= (1u << 20) && buckets && nodes) {
        const ULONGLONG now = now_ms();
        for (uint32_t b = 0; b < n; ++b) {
            const uintptr_t base = buckets + static_cast<uintptr_t>(b) * kBucketStride;
            const uint32_t c = rd<uint32_t>(base);
            for (uint32_t i = 0; i < c && i < 31; ++i) {
                const uint32_t key = rd<uint32_t>(base + 8 + i * 8);
                if ((key >> 28) != 0xB) continue;
                const uint32_t idx = rd<uint32_t>(base + 0xC + i * 8);
                const uintptr_t node = rd<uintptr_t>(nodes + static_cast<uintptr_t>(idx) * 8);
                const uintptr_t a = node ? rd<uintptr_t>(node + 8) : 0;
                const uintptr_t info = a ? rd<uintptr_t>(a + kActor_Info) : 0;
                if (!info || rd<uint8_t>(info + 1) != kTypeGimmick) continue;
                const uintptr_t comps = rd<uintptr_t>(a + kActor_Comps);
                const uintptr_t hier = comps ? rd<uintptr_t>(comps + kComps_Hierarchy) : 0;
                if (!hier || rd<uint8_t>(hier + kHier_AttachType) != kAttachSocket) continue;
                const uint32_t parent_key = rd<uint32_t>(hier + kHier_Parent);
                const uint32_t vehicle = followed_ancestor(table, parent_key);
                if (!vehicle) continue;
                Part* part = nullptr;
                for (auto& p : g_parts)
                    if (p.key.load() == key) part = &p;
                if (!part) {
                    for (auto& p : g_parts) {
                        if (p.key.load()) continue;
                        AcquireSRWLockExclusive(&g_parts_lock);
                        p.net_parent = 0;
                        p.net_ms = 0;
                        ReleaseSRWLockExclusive(&g_parts_lock);
                        p.fixes = 0;
                        p.gave_up = p.probe_logged = false;
                        p.key.store(key);
                        part = &p;
                        break;
                    }
                }
                if (!part) continue;  // more socket parts than slots: the rest are not watched
                part->seen = true;
                part->parent = parent_key;
                part->vehicle = vehicle;
                if (part->gave_up) continue;
                uint32_t net_parent = 0;
                float net[3]{};
                ULONGLONG net_ms = 0;
                AcquireSRWLockShared(&g_parts_lock);
                net_parent = part->net_parent;
                std::memcpy(net, part->net_pos, sizeof net);
                net_ms = part->net_ms;
                ReleaseSRWLockShared(&g_parts_lock);
                if (!net_ms || now - net_ms > kPartPoseMaxAgeMs || net_parent != parent_key) continue;
                if (!std::isfinite(net[0]) || !std::isfinite(net[1]) || !std::isfinite(net[2])) continue;
                const uintptr_t so = scene_object(comps);
                const uintptr_t ts = rd<uintptr_t>(comps + kComps_TS);
                if (!so || !ts) continue;
                float local[3]{}, ts_local[3]{};
                int16_t sector[2]{};
                if (!game::fast_read(so + kScene_Local + kXform_Pos, local, sizeof local) ||
                    !game::fast_read(so + kScene_Local + kXform_Sector, sector, sizeof sector) ||
                    !game::fast_read(ts + kTS_LocalPos, ts_local, sizeof ts_local))
                    continue;
                if (sector[0] || sector[1]) continue;                    // not a local transform
                if (dist3(local, ts_local) > kPartSettledM) continue;    // the TS has not caught up with the object yet
                const float off = dist3(local, net);
                if (off <= kPartOffM) continue;
                if (!on()) {
                    if (!part->probe_logged)
                        log::info("VehicleFollow (probe): part 0x{:08X} (on 0x{:08X}, ship 0x{:08X}) is at local ({:.2f}, "
                                  "{:.2f}, {:.2f}) here and ({:.2f}, {:.2f}, {:.2f}) on its controller's client ({:.2f} m): "
                                  "not moved", key, parent_key, vehicle, local[0], local[1], local[2], net[0], net[1],
                                  net[2], off);
                    part->probe_logged = true;
                    continue;
                }
                if (part->fixes >= kMaxPartFixes) {
                    part->gave_up = true;
                    log::warn("VehicleFollow: part 0x{:08X} (on 0x{:08X}) moved away from its controller's place {} times; "
                              "left alone from now on", key, parent_key, part->fixes);
                    continue;
                }
                // The same object still (its vtable and the actor's key), then the 12 bytes.
                if (rd<uintptr_t>(so) != g_base + g_addrs->vt_scene_object || rd<uint32_t>(a + kActor_Key) != key) continue;
                if (!write_bytes(so + kScene_Local + kXform_Pos, net, sizeof net)) continue;
                ++part->fixes;
                g_part_fixes.fetch_add(1, std::memory_order_relaxed);
                log::info("VehicleFollow: part 0x{:08X} (on 0x{:08X}, ship 0x{:08X}) was at local ({:.2f}, {:.2f}, {:.2f}) "
                          "on this client; its controller has it at ({:.2f}, {:.2f}, {:.2f}), {:.2f} m away: moved there "
                          "(scene object; time {} of at most {})", key, parent_key, vehicle, local[0], local[1],
                          local[2], net[0], net[1], net[2], off, part->fixes, kMaxPartFixes);
            }
        }
    }
    uint32_t count = 0;
    for (auto& p : g_parts) {
        if (!p.key.load()) continue;
        if (p.seen)
            ++count;
        else
            p.key.store(0);  // the part or its ship is gone
    }
    g_part_count.store(count);
}

// ---------------------------------------------------------------- passengers: the worker's side
uintptr_t actor_table() noexcept {
    const uintptr_t world = rd<uintptr_t>(g_base + g_addrs->world_slot);
    const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
    return mgr ? rd<uintptr_t>(mgr + kMgr_Table) : 0;
}

// Every 3 s: the other players' characters in the table (0xA keys that are not this client's own actor).
void passengers_list() {
    if (g_addrs->char_request_type == kRequestNone) return;
    const uintptr_t world = rd<uintptr_t>(g_base + g_addrs->world_slot);
    const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
    const uintptr_t table = mgr ? rd<uintptr_t>(mgr + kMgr_Table) : 0;
    const uintptr_t own = mgr ? rd<uintptr_t>(mgr + kMgr_OwnActor) : 0;
    const uint32_t n = table ? rd<uint32_t>(table + kTable_Count) : 0;
    const uintptr_t buckets = table ? rd<uintptr_t>(table + kTable_Buckets) : 0, nodes = table ? rd<uintptr_t>(table + kTable_Nodes) : 0;
    if (!own || !n || n > (1u << 20) || !buckets || !nodes) return;
    for (uint32_t b = 0; b < n; ++b) {
        const uintptr_t base = buckets + static_cast<uintptr_t>(b) * kBucketStride;
        const uint32_t c = rd<uint32_t>(base);
        for (uint32_t i = 0; i < c && i < 31; ++i) {
            const uint32_t key = rd<uint32_t>(base + 8 + i * 8);
            if ((key >> 28) != 0xA) continue;
            const uint32_t idx = rd<uint32_t>(base + 0xC + i * 8);
            const uintptr_t node = rd<uintptr_t>(nodes + static_cast<uintptr_t>(idx) * 8);
            const uintptr_t a = node ? rd<uintptr_t>(node + 8) : 0;
            if (!a || a == own || rd<uint32_t>(a + kActor_Key) != key) continue;
            bool known = false;
            for (size_t s = 0; s < kMaxPlayers; ++s) known |= g_passengers[s].key.load() == key;
            if (known) continue;
            for (size_t s = 0; s < kMaxPlayers; ++s) {
                Passenger& p = g_passengers[s];
                if (p.key.load()) continue;
                reset_slot(p);
                p.key.store(key);
                g_passenger_count.fetch_add(1);
                log::info("VehicleFollow: another player's character 0x{:08X} is here; while it stands on a ship part "
                          "it is drawn on this client's copy of that part", key);
                break;
            }
        }
    }
}

// Is `parent` (what an actor hangs from) a part of a ship of this client's user? A gimmick first, then up to a
// vehicle actor with no parent whose controller is `own`.
bool on_own_ship(uintptr_t table, uint32_t parent, uint32_t own) noexcept {
    uint32_t k = parent;
    for (uint32_t depth = 0; k && depth < kMaxChain; ++depth) {
        const uintptr_t pa = actor_by_key(table, k);
        const uintptr_t pinfo = pa ? rd<uintptr_t>(pa + kActor_Info) : 0;
        const uintptr_t pcomps = pa ? rd<uintptr_t>(pa + kActor_Comps) : 0;
        if (!pinfo || !pcomps) return false;
        const uint8_t type = rd<uint8_t>(pinfo + 1);
        if (depth == 0 && type != kTypeGimmick) return false;
        const uintptr_t phier = rd<uintptr_t>(pcomps + kComps_Hierarchy);
        const uint32_t up = phier ? rd<uint32_t>(phier + kHier_Parent) : 0;
        if (!up) return type == kTypeVehicle && rd<uint32_t>(pa + kActor_Controller) == own;
        k = up;
    }
    return false;
}

// After each scan: this client's own vehicles (g_scan_mine) for the other mod, the vfl::kMaxVehiclePoses nearest to
// a player (this client's character or another player's) within kPoseRangeM.
void own_vehicles_publish() {
    std::array<vfl::Pose, 1 + kMaxPlayers> refs{};
    size_t n_refs = 0;
    {
        const uintptr_t world = rd<uintptr_t>(g_base + g_addrs->world_slot);
        const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
        const uintptr_t own_actor = mgr ? rd<uintptr_t>(mgr + kMgr_OwnActor) : 0;
        const uintptr_t own_comps = own_actor ? rd<uintptr_t>(own_actor + kActor_Comps) : 0;
        const uintptr_t own_ts = own_comps ? rd<uintptr_t>(own_comps + kComps_TS) : 0;
        if (own_ts && network_pose(own_ts, &refs[n_refs])) ++n_refs;
        for (size_t s = 0; s < kMaxPlayers; ++s) {
            const uintptr_t ts = g_passengers[s].key.load() ? g_passengers[s].ts.load() : 0;
            if (ts && network_pose(ts, &refs[n_refs])) ++n_refs;
        }
    }
    if (!n_refs) return;  // nobody to be near (a load): the list stays
    std::vector<std::pair<double, std::pair<uint32_t, uintptr_t>>> near_ones;
    for (const auto& m : g_scan_mine) {
        vfl::Pose at;
        float unused[3];
        if (!ts_pose(m.second, true, &at, unused)) continue;
        double d = 1e18;
        for (size_t r = 0; r < n_refs; ++r) d = std::min(d, std::hypot(at.x - refs[r].x, at.z - refs[r].z));
        if (d <= kPoseRangeM) near_ones.push_back({d, m});
    }
    std::sort(near_ones.begin(), near_ones.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const size_t n = std::min(near_ones.size(), vfl::kMaxVehiclePoses);
    bool changed = n != g_own_vehicle_count.load();
    for (size_t i = 0; i < n && !changed; ++i) {
        bool have = false;
        for (auto& o : g_own_vehicles) have |= o.key.load() == near_ones[i].second.first;
        changed = !have;
    }
    if (changed) {
        // (The network thread's last-sent poses stay with their slots: a vehicle that takes another's slot is sent at
        // once, its pose differs.)
        for (size_t i = 0; i < vfl::kMaxVehiclePoses; ++i) {
            g_own_vehicles[i].ts.store(0, std::memory_order_release);
            g_own_vehicles[i].key.store(i < n ? near_ones[i].second.first : 0, std::memory_order_relaxed);
            g_own_vehicles[i].ts.store(i < n ? near_ones[i].second.second : 0, std::memory_order_release);
        }
        g_own_vehicle_count.store(static_cast<uint32_t>(n));
        std::string list;
        for (size_t i = 0; i < n; ++i)
            list += std::format("{}0x{:08X} ({:.0f} m)", i ? ", " : "", near_ones[i].second.first, near_ones[i].first);
        log::info("VehicleFollow: {} vehicle(s) this client controls within {:.0f} m of a player{}{}: where it has them "
                  "goes to the other player's mod ten times a second while they move{}", n, kPoseRangeM,
                  n ? ": " : "", list,
                  near_ones.size() > n ? std::format(" ({} more: the nearest {} only)", near_ones.size() - n, n)
                                       : std::string());
    } else {
        // The same vehicles: only their TransformSyncs, in case one changed.
        for (size_t i = 0; i < n; ++i)
            for (auto& o : g_own_vehicles)
                if (o.key.load() == near_ones[i].second.first) o.ts.store(near_ones[i].second.second, std::memory_order_release);
    }
}

// Every 3 s: this client's own riders, for the other player's mod: the NPC actors (types 3 and 4, 0xB keys) that hang
// from a part of a vehicle of this client's user, the vfl::kMaxRiders nearest to either player within kRiderRangeM.
// Those types only and by distance (live, 14-nov 15:20: with every non-gimmick actor on any own vehicle the list filled
// with sixteen type-3 actors of the host's other vehicles before the ship was even out, and two of its ten crew
// members did not fit; the host's client also controls the AI's ships, crews included).
// (The player's ship's crew is type 4; the pirates of an NPC's ship are type 3: 0xB0101580 'Pirate', 14-nov 16:20.)
constexpr uint8_t kTypeNpc = 4, kTypeNpcHostile = 3;
constexpr double kRiderRangeM = 250.0;
void riders_list() {
    if (g_addrs->char_request_type == kRequestNone) return;
    const uintptr_t world = rd<uintptr_t>(g_base + g_addrs->world_slot);
    const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
    const uintptr_t table = mgr ? rd<uintptr_t>(mgr + kMgr_Table) : 0;
    const uintptr_t user = mgr ? rd<uintptr_t>(mgr + kMgr_User) : 0;
    const uint32_t own = user ? rd<uint32_t>(user + kUser_Key) : 0;
    const uint32_t n = table ? rd<uint32_t>(table + kTable_Count) : 0;
    const uintptr_t buckets = table ? rd<uintptr_t>(table + kTable_Buckets) : 0, nodes = table ? rd<uintptr_t>(table + kTable_Nodes) : 0;
    if (!own || !n || n > (1u << 20) || !buckets || !nodes) return;
    std::array<uint32_t, vfl::kMaxRiders> keys{};
    std::array<uintptr_t, vfl::kMaxRiders> tss{};
    std::array<double, vfl::kMaxRiders> dist{};
    uint32_t found = 0, more = 0;
    // Where the players are: this client's own character and the other players' (their last network poses).
    std::array<vfl::Pose, 1 + kMaxPlayers> refs{};
    size_t n_refs = 0;
    {
        const uintptr_t own_actor = rd<uintptr_t>(mgr + kMgr_OwnActor);
        const uintptr_t own_comps = own_actor ? rd<uintptr_t>(own_actor + kActor_Comps) : 0;
        const uintptr_t own_ts = own_comps ? rd<uintptr_t>(own_comps + kComps_TS) : 0;
        if (own_ts && network_pose(own_ts, &refs[n_refs])) ++n_refs;
        for (size_t s = 0; s < kMaxPlayers; ++s) {
            const uintptr_t ts = g_passengers[s].key.load() ? g_passengers[s].ts.load() : 0;
            if (ts && network_pose(ts, &refs[n_refs])) ++n_refs;
        }
    }
    if (!n_refs) return;  // nobody to be near (a load): the list stays as it is
    for (uint32_t b = 0; b < n; ++b) {
        const uintptr_t base = buckets + static_cast<uintptr_t>(b) * kBucketStride;
        const uint32_t c = rd<uint32_t>(base);
        for (uint32_t i = 0; i < c && i < 31; ++i) {
            const uint32_t key = rd<uint32_t>(base + 8 + i * 8);
            if ((key >> 28) != 0xB) continue;
            const uint32_t idx = rd<uint32_t>(base + 0xC + i * 8);
            const uintptr_t node = rd<uintptr_t>(nodes + static_cast<uintptr_t>(idx) * 8);
            const uintptr_t a = node ? rd<uintptr_t>(node + 8) : 0;
            const uintptr_t info = a ? rd<uintptr_t>(a + kActor_Info) : 0;
            if (!info || rd<uint32_t>(a + kActor_Key) != key) continue;
            if (const uint8_t type = rd<uint8_t>(info + 1); type != kTypeNpc && type != kTypeNpcHostile) continue;
            const uintptr_t comps = rd<uintptr_t>(a + kActor_Comps);
            const uintptr_t hier = comps ? rd<uintptr_t>(comps + kComps_Hierarchy) : 0;
            const uint32_t parent = hier ? rd<uint32_t>(hier + kHier_Parent) : 0;
            if ((parent >> 28) != 0xB) continue;
            const uintptr_t ts = rd<uintptr_t>(comps + kComps_TS);
            if (!ts || rd<uintptr_t>(ts + kTS_Actor) != a || !on_own_ship(table, parent, own)) continue;
            // How far from the nearest player (its own last network pose: the world position its controller gave).
            vfl::Pose at;
            if (!network_pose(ts, &at)) continue;
            double d = 1e18;
            for (size_t r = 0; r < n_refs; ++r) {
                const double dx = at.x - refs[r].x, dy = at.y - refs[r].y, dz = at.z - refs[r].z;
                d = std::min(d, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
            if (d > kRiderRangeM) continue;
            // Kept sorted by distance: the nearest vfl::kMaxRiders.
            uint32_t at_i = found;
            while (at_i > 0 && dist[at_i - 1] > d) --at_i;
            if (at_i == vfl::kMaxRiders) {
                ++more;
                continue;
            }
            if (found == vfl::kMaxRiders) ++more;
            for (uint32_t j = std::min<uint32_t>(found, vfl::kMaxRiders - 1); j > at_i; --j) {
                keys[j] = keys[j - 1];
                tss[j] = tss[j - 1];
                dist[j] = dist[j - 1];
            }
            keys[at_i] = key;
            tss[at_i] = ts;
            dist[at_i] = d;
            if (found < vfl::kMaxRiders) ++found;
        }
    }
    bool changed = found != g_own_rider_count.load();
    for (uint32_t i = 0; i < found && !changed; ++i) {
        bool have = false;
        for (auto& r : g_own_riders) have |= r.key.load() == keys[i];
        changed = !have;
    }
    for (size_t i = 0; i < vfl::kMaxRiders; ++i) {
        g_own_riders[i].ts.store(0, std::memory_order_release);
        g_own_riders[i].key.store(i < found ? keys[i] : 0, std::memory_order_relaxed);
        g_own_riders[i].ts.store(i < found ? tss[i] : 0, std::memory_order_release);
    }
    g_own_rider_count.store(found);
    if (!changed) return;
    std::string list;
    for (uint32_t i = 0; i < found; ++i) list += std::format("{}0x{:08X} ({:.0f} m)", i ? ", " : "", keys[i], dist[i]);
    log::info("VehicleFollow: {} rider(s) on this client's own vehicles within {:.0f} m of a player{}{}: their places go "
              "to the other player's mod ten times a second{}", found, kRiderRangeM, found ? ": " : "", list,
              more ? std::format(" ({} more are not sent: the nearest {} only)", more, vfl::kMaxRiders) : std::string());
}

// Every 250 ms: each listed character's TransformSync and, for the parent of its last Move, the chain of
// TransformSyncs up to the root vehicle. True: a character is on a ship part (the hooks are wanted).
bool passengers_tick() {
    own_trace_flush();
    if (!g_passenger_count.load()) return false;
    const uintptr_t table = actor_table();
    if (!table) return false;
    {
        const uintptr_t world = rd<uintptr_t>(g_base + g_addrs->world_slot);
        const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
        const uintptr_t own = mgr ? rd<uintptr_t>(mgr + kMgr_OwnActor) : 0;
        const uintptr_t own_comps = own ? rd<uintptr_t>(own + kActor_Comps) : 0;
        const uintptr_t own_ts = own_comps ? rd<uintptr_t>(own_comps + kComps_TS) : 0;
        g_own_ts.store(own_ts && rd<uintptr_t>(own_ts + kTS_Actor) == own ? own_ts : 0, std::memory_order_relaxed);
    }
    bool any = false;
    for (auto& p : g_passengers) {
        const uint32_t key = p.key.load();
        if (!key) continue;
        const bool rider = p.rider.load();
        if (rider && now_ms() - p.channel_ms.load() > kRiderGoneMs) {
            // The other mod no longer names it (the ship was put away, the rider left it, the link is down).
            if (p.logged_parent)
                log::info("VehicleFollow: rider 0x{:08X} is no longer named by the other player's mod; {} states of it, "
                          "{} own requests answered, {} more given while it stood still, the game's own pose up to "
                          "{:.1f} m away", key, p.channel_states.load(), p.placed.load(), p.idle_placed.load(),
                          p.max_off_cm.load() / 100.0);
            p.ts.store(0);
            p.chain_state = 0;
            p.key.store(0);
            g_passenger_count.fetch_sub(1);
            g_rider_count.fetch_sub(1);
            continue;
        }
        const uintptr_t a = actor_by_key(table, key);
        const uintptr_t comps = a ? rd<uintptr_t>(a + kActor_Comps) : 0;
        const uintptr_t ts = comps ? rd<uintptr_t>(comps + kComps_TS) : 0;
        if (rider && (!ts || rd<uintptr_t>(ts + kTS_Actor) != a)) {
            // Not on this client (yet): its slot waits for it while the other mod names it.
            p.ts.store(0);
            p.chain_state.store(0, std::memory_order_release);
            continue;
        }
        if (!ts || rd<uintptr_t>(ts + kTS_Actor) != a) {
            if (p.logged_parent || p.placed.load())
                log::info("VehicleFollow: character 0x{:08X} gone; {} of its Moves seen, {} requests on a ship part, {} "
                          "placed, {} more given while it stood still, the game's own pose up to {:.1f} m away", key,
                          p.moves.load(), p.requests.load(), p.placed.load(), p.idle_placed.load(),
                          p.max_off_cm.load() / 100.0);
            p.ts.store(0);
            p.chain_state = 0;
            p.key.store(0);
            g_passenger_count.fetch_sub(1);
            continue;
        }
        p.ts.store(ts, std::memory_order_release);
        uint32_t parent = 0;
        AcquireSRWLockShared(&p.lock);
        parent = p.parent;
        ReleaseSRWLockShared(&p.lock);
        if (rider) {
            const uintptr_t hier = rd<uintptr_t>(comps + kComps_Hierarchy);
            p.attached_here.store(hier && rd<uint32_t>(hier + kHier_Parent), std::memory_order_relaxed);
        }
        uint32_t len = 0, vehicle = 0;
        std::array<uintptr_t, kMaxChain> chain{};
        std::array<uint32_t, kMaxChain> keys{};
        uint32_t k = parent;
        for (uint32_t depth = 0; k && depth < kMaxChain; ++depth) {
            const uintptr_t pa = actor_by_key(table, k);
            const uintptr_t pinfo = pa ? rd<uintptr_t>(pa + kActor_Info) : 0;
            const uintptr_t pcomps = pa ? rd<uintptr_t>(pa + kActor_Comps) : 0;
            const uintptr_t pts = pcomps ? rd<uintptr_t>(pcomps + kComps_TS) : 0;
            if (!pinfo || !pts || rd<uintptr_t>(pts + kTS_Actor) != pa) break;
            const uint8_t type = rd<uint8_t>(pinfo + 1);
            if (depth == 0 && type != kTypeGimmick) break;  // on a horse or another character: not here
            chain[depth] = pts;
            keys[depth] = k;
            const uintptr_t phier = rd<uintptr_t>(pcomps + kComps_Hierarchy);
            const uint32_t up = phier ? rd<uint32_t>(phier + kHier_Parent) : 0;
            if (!up) {
                if (type == kTypeVehicle) {
                    len = depth + 1;
                    vehicle = k;
                }
                break;
            }
            k = up;
        }
        if (len) {
            // Published only when something changed (the parent, the depth, a pointer or a key), into the slot the
            // hooks are not reading.
            const uint64_t cur = p.chain_state.load();
            const uint32_t cur_slot = static_cast<uint32_t>(cur >> 8) & 1;
            bool same = chain_parent(cur) == parent && static_cast<uint32_t>(cur & 0xFF) == len;
            for (uint32_t i = 0; same && i < len; ++i)
                same = p.chains[cur_slot].ts[i].load() == chain[i] && p.chains[cur_slot].key[i].load() == keys[i];
            if (!same) {
                const uint32_t slot = cur ? cur_slot ^ 1 : 0;
                for (uint32_t i = 0; i < kMaxChain; ++i) {
                    p.chains[slot].ts[i].store(chain[i], std::memory_order_relaxed);
                    p.chains[slot].key[i].store(keys[i], std::memory_order_relaxed);
                }
                p.vehicle.store(vehicle);
                p.chain_state.store(static_cast<uint64_t>(parent) << 32 | static_cast<uint64_t>(slot) << 8 | len,
                                    std::memory_order_release);
            }
            any = true;
            if (p.logged_parent != parent) {
                log::info("VehicleFollow: {} 0x{:08X} stands on 0x{:08X} (vehicle 0x{:08X}, {} level{} up): {} on "
                          "this client's copy of it", rider ? "rider" : "character", key, parent, vehicle, len,
                          len == 1 ? "" : "s", on() ? "drawn" : "measured (probe), not placed,");
                p.logged_parent = parent;
            }
        } else {
            p.chain_state.store(0, std::memory_order_release);
            if (p.logged_parent) {
                log::info("VehicleFollow: {} 0x{:08X} left 0x{:08X}; {} requests there, {} placed, {} more given "
                          "while it stood still, the game's own pose up to {:.1f} m away", rider ? "rider" : "character",
                          key, p.logged_parent,
                          p.requests.load(), p.placed.load(), p.idle_placed.load(), p.max_off_cm.load() / 100.0);
                p.logged_parent = 0;
                p.max_off_cm = 0;
            }
        }
    }
    return any;
}

// The other player's Move (Ack on the guest, user 2's Req on the host; the same leading layout): the parent and the
// local pose of a listed character.
void passenger_move(std::span<const uint8_t> payload, const movement::Layout& l, uint32_t key) noexcept {
    for (auto& p : g_passengers) {
        if (p.key.load(std::memory_order_relaxed) != key) continue;
        p.moves.fetch_add(1, std::memory_order_relaxed);
        // The other mod says what it stands on 20 times a second: the game's Move is only the fallback.
        if (now_ms() - p.channel_ms.load(std::memory_order_relaxed) < kChannelFreshMs) return;
        uint64_t parent = 0;
        float pos[3], q[4];
        std::memcpy(&parent, payload.data() + l.parent, 8);
        std::memcpy(pos, payload.data() + l.local_pos, sizeof pos);
        std::memcpy(q, payload.data() + l.local_quat, sizeof q);
        const bool ok = parent != ~0ull && (parent >> 32) == 0 && vfl::unit_quat(q) && std::isfinite(pos[0]) &&
                        std::isfinite(pos[1]) && std::isfinite(pos[2]) && std::fabs(pos[0]) <= 500.0f &&
                        std::fabs(pos[1]) <= 500.0f && std::fabs(pos[2]) <= 500.0f;
        const uint32_t now_parent = ok ? static_cast<uint32_t>(parent) : 0;
        AcquireSRWLockExclusive(&p.lock);
        const uint32_t before = p.parent;
        p.parent = now_parent;
        if (ok) {
            std::memcpy(p.pos, pos, sizeof pos);
            std::memcpy(p.q, q, sizeof q);
        }
        ReleaseSRWLockExclusive(&p.lock);
        if (before != now_parent && g_wake) SetEvent(g_wake);
        return;
    }
}

void refresh(const std::vector<Found>& found) {
    std::string gone, listed;
    for (auto& v : g_vehicles) {
        const uintptr_t ts = v.ts.load();
        if (!ts) continue;
        const Found* f = nullptr;
        for (const auto& c : found)
            if (c.ts == ts && c.vehicle == v.key.load()) f = &c;
        if (f) {
            v.gimmicks = f->gimmicks;
            continue;
        }
        if (v.following)
            log::info("VehicleFollow: vehicle 0x{:08X} (gimmick 0x{:08X}) gone; followed {} positions / {} rotations of {} "
                      "requests, {} samples, error max {:.1f} m", v.key.load(), v.gimmick, v.positions.load(),
                      v.rotations.load(), v.requests.load(), v.samples.load(), v.max_err_cm.load() / 100.0);
        else
            gone += std::format(" 0x{:08X}", v.key.load());
        v.ts.store(0, std::memory_order_release);
    }
    for (const auto& f : found) {
        bool known = false;
        for (auto& v : g_vehicles) known |= v.ts.load() == f.ts && v.key.load() == f.vehicle;
        if (known) continue;
        for (auto& v : g_vehicles) {
            if (v.ts.load()) continue;
            AcquireSRWLockExclusive(&v.lock);
            v.track.clear();
            v.track.set_delay(g_delay_ms.load());
            v.drawn_ms = 0;
            ReleaseSRWLockExclusive(&v.lock);
            v.gimmick = f.gimmick;
            v.gimmicks = f.gimmicks;
            v.following = false;
            v.near_seen = false;
            v.far_only = 0;
            v.far_logged = false;
            v.far_measured = v.far_err_sum_cm = 0;
            v.far_err_max_cm = v.far_err_last_cm = 0;
            v.requests = v.positions = v.rotations = v.samples = 0;
            v.channel_ms = 0;
            v.channel_samples = 0;
            v.last_err_cm = v.max_err_cm = 0;
            v.key.store(f.vehicle);
            v.ts.store(f.ts, std::memory_order_release);
            listed += std::format(" 0x{:08X} ({}{} gimmick{}, e.g. 0x{:08X})", f.vehicle, f.npc ? "an NPC's, " : "",
                                  f.gimmicks, f.gimmicks == 1 ? "" : "s", f.gimmick);
            break;
        }
    }
    if (!listed.empty())
        log::info("VehicleFollow: listed vehicles of another user with vehicle gimmicks:{}; each is followed only while "
                  "it is drawn from one (a ship out: its per-frame vehicle requests)", listed);
    if (!gone.empty()) log::info("VehicleFollow: unlisted (gone, never followed):{}", gone);
}

void announce(Vehicle& v, const char* how) {
    v.following = true;
    vfl::Pose net;
    const bool ok = network_pose(v.ts.load(), &net);
    log::info("VehicleFollow: vehicle 0x{:08X} of another user is drawn from its vehicle gimmick ({}; {} hang from it, "
              "e.g. 0x{:08X}): following it; network pose {}", v.key.load(), how, v.gimmicks, v.gimmick,
              ok ? std::format("({:.1f}, {:.1f}, {:.1f})", net.x, net.y, net.z) : std::string("none yet"));
}

// Before the hooks exist: is any listed vehicle drawn from its gimmick? Such a vehicle gets a vehicle position request
// every frame and the type waits at TS+0x5BC until the TransformSync tick consumes it (~2% of the time on the guest,
// 13-nov); a stabled horse's never shows 7 (4 s of polling, none). Polled for kProbeMs, a few frames.
bool probe_active() {
    std::array<uintptr_t, kMaxVehicles> ts{};
    bool any_listed = false;
    for (size_t i = 0; i < kMaxVehicles; ++i) {
        if (g_vehicles[i].following) continue;
        ts[i] = g_vehicles[i].ts.load();
        any_listed |= ts[i] != 0;
    }
    if (!any_listed) return false;
    const ULONGLONG end = now_ms() + kProbeMs;
    for (uint32_t spin = 0;; ++spin) {
        for (size_t i = 0; i < kMaxVehicles; ++i) {
            const uint8_t type = ts[i] ? rd<uint8_t>(ts[i] + kTS_RequestType, kRequestNone) : kRequestNone;
            if (!vehicle_request(type)) continue;
            // Either type puts the hooks in and has the vehicle's parts watched (kit v23 did: the wheel was mended
            // while the guest was still 6 km away). Only the POSE waits for a near request (position_mid).
            const bool is_near = type == g_addrs->request_type;  // (`near` is a macro of windows.h)
            if (is_near) g_vehicles[i].near_seen.store(true, std::memory_order_relaxed);
            announce(g_vehicles[i], is_near ? "a near vehicle request pending at TS+0x5BC"
                                         : "a far vehicle request pending at TS+0x5BC: its parts are watched, its pose "
                                           "stays the game's until it is seen near");
            return true;
        }
        YieldProcessor();
        if ((spin & 0xFF) == 0 && now_ms() >= end) return false;
    }
}

DWORD WINAPI worker(LPVOID) {
    try {
        for (int i = 0; i < 2400; ++i) {
            if (rd<uintptr_t>(g_base + g_addrs->world_slot)) break;
            Sleep(500);
        }
        std::vector<gad::Section> secs;
        if (!p8_exe_sections(g_base, secs)) {
            log::error("VehicleFollow: the exe's sections are unreadable; not installed");
            return 0;
        }
        log::info("VehicleFollow ({}, vehicle_follow={}): looking for the other player's ships every 3 s while in the "
                  "world (never during a load); the two setters are hooked only when the first one is out",
                  g_guest.load() ? "guest" : "host", gil::mode_name(static_cast<gil::Mode>(g_mode.load())));
        bool hooked = false;
        uint64_t last_total = 0;
        ULONGLONG last_summary = now_ms();
        uint32_t last_own = 0;
        bool dropped_logged = false;
        bool passengers = false;
        ULONGLONG next_scan = 0;
        for (;;) {
            if (g_wake)
                WaitForSingleObject(g_wake, 250);
            else
                Sleep(250);
            if (!in_world()) {
                // A load: any actor may go. The passengers' pointers are dropped, so the hooks match nothing of theirs
                // and no call goes into the game until the world is back and they are found again. The vehicles keep
                // their entries: those paths only use the TransformSync the game itself passes in, checked by key.
                g_own_ts.store(0, std::memory_order_relaxed);
                for (auto& p : g_passengers) {
                    p.ts.store(0, std::memory_order_release);
                    p.chain_state.store(0, std::memory_order_release);
                }
                for (auto& r : g_own_riders) r.ts.store(0, std::memory_order_release);
                for (auto& o : g_own_vehicles) o.ts.store(0, std::memory_order_release);
                continue;
            }
            const ULONGLONG loop_now = now_ms();
            if (loop_now < next_scan) {
                // Between two scans: only the passengers' chains (a few table lookups), and their hooks if not in yet.
                passengers = passengers_tick();
                if (hooked || !passengers) continue;
            }
            next_scan = loop_now + kScanMs;
            uint32_t own = 0, dropped = 0;
            const auto found = scan(&own, &dropped);
            if (own != last_own) {
                log::info("VehicleFollow: this client's user 0x{:08X}", own);
                last_own = own;
            }
            if (dropped && !dropped_logged) {
                log::warn("VehicleFollow: more than {} vehicles of another user; {} not listed", kMaxVehicles, dropped);
                dropped_logged = true;
            }
            refresh(found);
            own_vehicles_publish();
            passengers_list();
            riders_list();
            passengers = passengers_tick();
            // The setters run for every actor, many times a frame: hooked only once a listed vehicle is drawn from its
            // gimmick (a ship out) or the other player's character stands on a ship part, never during the world loads
            // before it.
            if (!hooked && (passengers || probe_active())) {
                const bool a = p8_install_mid(g_base, secs, g_addrs->set_position, kPatch, kSetPositionHead, &position_mid,
                                              g_pos_hook, "VehicleFollow", "the TransformSync position request");
                const bool b = a && p8_install_mid(g_base, secs, g_addrs->set_rotation, kPatch, kSetRotationHead, &rotation_mid,
                                                   g_rot_hook, "VehicleFollow", "the TransformSync rotation request");
                if (!a || !b) {
                    g_pos_hook = {};
                    g_rot_hook = {};
                    log::error("VehicleFollow: not both setters could be hooked; nothing installed (stopped)");
                    return 0;
                }
                hooked = true;
                log::info("VehicleFollow: hooked exe+0x{:X} / exe+0x{:X}; the other player's ships are {} {} ms behind "
                          "its samples", g_addrs->set_position, g_addrs->set_rotation, on() ? "drawn" : "measured (probe), not moved,",
                          g_delay_ms.load());
            }
            if (hooked)
                for (auto& v : g_vehicles)
                    if (v.ts.load() && !v.following && (v.requests.load() || v.far_only.load()))
                        announce(v, v.requests.load() ? "vehicle requests in the hook"
                                                      : "far vehicle requests in the hook: its parts are watched, its "
                                                        "pose stays the game's until it is seen near");
            for (auto& v : g_vehicles) {
                if (!v.ts.load() || v.far_logged || !v.far_only.load()) continue;
                v.far_logged = true;
                log::info("VehicleFollow: vehicle 0x{:08X} of another user gets far-type ({}) position requests and was "
                          "never drawn from its gimmick near this client: left to the game (a mount moves this way; a "
                          "ship first seen from far is followed once this client has been near it)", v.key.load(),
                          g_addrs->request_type_far);
            }
            parts_pass();
            const ULONGLONG now = now_ms();
            if (now - last_summary < 60000) continue;
            last_summary = now;
            uint64_t total = 0;
            std::string line;
            size_t idle = 0;
            for (auto& v : g_vehicles) {
                if (!v.ts.load()) continue;
                if (!v.following) {
                    ++idle;
                    // Left to the game, but the other mod sends where it has it: how far apart the two are.
                    if (const uint64_t m = v.far_measured.load()) {
                        total += m;
                        line += std::format(" | 0x{:08X} left to the game (far requests only, {} of {} with the other "
                                            "mod's samples fresh): the game's pose {:.1f} m from where the other mod "
                                            "has it now on average (the game's own delay included), {:.1f} m now, "
                                            "{:.1f} m at most",
                                            v.key.load(), m, v.far_only.load(), v.far_err_sum_cm.load() / 100.0 / m,
                                            v.far_err_last_cm.load() / 100.0, v.far_err_max_cm.load() / 100.0);
                    }
                    continue;
                }
                total += v.requests.load();
                vfl::Pose d;
                AcquireSRWLockShared(&v.lock);
                d = v.drawn;
                const size_t n = v.track.size();
                const uint64_t lone = v.track.lone_jumps();
                ReleaseSRWLockShared(&v.lock);
                line += std::format(" | 0x{:08X} (gimmick 0x{:08X}): requests {} followed {}/{} samples {} (kept {}; {} "
                                    "of them from the other mod; lone far samples not drawn {}) drawn ({:.1f}, {:.1f}) "
                                    "error now {:.1f} m max {:.1f} m",
                                    v.key.load(), v.gimmick, v.requests.load(), v.positions.load(), v.rotations.load(),
                                    v.samples.load(), n, v.channel_samples.load(), lone, d.x, d.z,
                                    v.last_err_cm.load() / 100.0, v.max_err_cm.load() / 100.0);
            }
            struct {
                uint32_t listed = 0, on_part = 0, attached = 0, types = 0, faulted = 0, worst_cm = 0;
                uint64_t states = 0, requests = 0, placed = 0, idle = 0, cross = 0;
            } riders;
            for (auto& p : g_passengers) {
                if (!p.key.load() || !p.rider.load()) continue;
                ++riders.listed;
                riders.on_part += chain_parent(p.chain_state.load()) ? 1 : 0;
                riders.attached += p.attached_here.load() ? 1 : 0;
                riders.types |= p.types_seen.load();
                riders.faulted += p.faulted.load() ? 1 : 0;
                riders.worst_cm = std::max(riders.worst_cm, p.max_off_cm.load());
                riders.states += p.channel_states.load();
                riders.requests += p.requests.load();
                riders.placed += p.placed.load();
                riders.idle += p.idle_placed.load();
                riders.cross += p.cross_thread.load();
            }
            if (g_vehicles_sent.load() || g_vehicle_events.load()) {
                total += g_vehicles_sent.load() + g_vehicle_events.load();
                line += std::format(" | own vehicles found far from where they were at one look and not sent until the "
                                    "next look confirmed it: {}",
                                    g_own_lone_jumps.load());
                line += std::format(" | vehicle poses: own ones sent {} times ({} listed now); the other mod's: {} events "
                                    "({} with the poses' times: the newest sample of the last one placed {} ms before "
                                    "its arrival, the other clock taken anew {} times), {} poses of vehicles not listed "
                                    "here", g_vehicles_sent.load(), g_own_vehicle_count.load(), g_vehicle_events.load(),
                                    g_vehicle_timed_events.load(), g_vehicle_stamp_behind_ms.load(),
                                    g_vehicle_clock_resets.load(), g_vehicle_poses_unlisted.load());
            }
            if (riders.listed || g_rider_events.load() || g_riders_sent.load()) {
                total += riders.requests + riders.idle + g_riders_sent.load();
                line += std::format(" | riders: own ones sent {} times ({} now); the other player's: {} events, {} listed "
                                    "({} of them with a parent on this client, {} placed on a ship part by this "
                                    "module, {} left out: no slot), states {}, own requests {} (types "
                                    "0x{:X}) placed {}, given while still {} (on another thread {}, {} faulted), the "
                                    "game's own pose up to {:.1f} m away",
                                    g_riders_sent.load(), g_own_rider_count.load(), g_rider_events.load(), riders.listed,
                                    riders.attached, riders.on_part, g_rider_dropped.load(), riders.states, riders.requests,
                                    riders.types, riders.placed, riders.idle, riders.cross, riders.faulted,
                                    riders.worst_cm / 100.0);
            }
            for (auto& p : g_passengers) {
                // Only a character that has been on a ship part: its Moves alone do not make a line every minute.
                if (!p.key.load() || p.rider.load() || !(p.requests.load() + p.idle_placed.load())) continue;
                total += p.requests.load() + p.idle_placed.load();
                line += std::format(" | character 0x{:08X}: Moves {} channel states {} on a ship part now {} own requests {} placed {} "
                                    "given while still {} (faults {}{}, on another thread than its own requests {}, "
                                    "unreadable {}) held on the part before {} rotation type {} the game's own "
                                    "pose {:.1f} m away now, max {:.1f} m",
                                    p.key.load(), p.moves.load(), p.channel_states.load(),
                                    chain_parent(p.chain_state.load()) ? "yes" : "no", p.requests.load(),
                                    p.placed.load(), p.idle_placed.load(), p.idle_failed.load(),
                                    p.faulted.load() ? ": no more calls for it" : "", p.cross_thread.load(),
                                    p.unreadable.load(), p.held.load(), p.rot_type.load(),
                                    p.last_off_cm.load() / 100.0, p.max_off_cm.load() / 100.0);
            }
            if (const uint64_t air = g_air_times.load()) {
                total += air + g_air_fixed_frames.load();
                const auto air_mode = static_cast<gil::Mode>(g_air_mode.load());
                line += std::format(" | this client's own character in the air off another player's ship {} time(s), "
                                    "down on a part again {} (passenger_air_follow={}: {} requests {}, {:.2f} m at most)",
                                    air, g_air_landed.load(), gil::mode_name(air_mode), g_air_fixed_frames.load(),
                                    air_mode == gil::Mode::On ? "moved" : "that lacked something, none moved",
                                    g_air_max_fix_cm.load() / 100.0);
            }
            if (total == last_total && line.empty()) continue;
            last_total = total;
            log::info("VehicleFollow summary ({}, vehicle_follow={}): unreadable network poses {}, {} listed vehicle(s) "
                      "not drawn from a gimmick; socket parts watched {} (their controller's poses received {}, moved "
                      "{}){}", g_guest.load() ? "guest" : "host",
                      gil::mode_name(static_cast<gil::Mode>(g_mode.load())), g_unreadable.load(), idle,
                      g_part_count.load(), g_part_acks.load(), g_part_fixes.load(),
                      line.empty() ? std::string(" | no ship of another user followed") : line);
        }
    } catch (...) {
        log::error("VehicleFollow: the worker stopped (exception)");
    }
    return 0;
}

}  // namespace

bool vehicle_follow_own_anchor(uint32_t* self_key, uint32_t* anchor, float pos[3], float quat[4]) noexcept {
    *self_key = *anchor = 0;
    if (!g_started.load(std::memory_order_relaxed) || !g_addrs || g_addrs->char_request_type == kRequestNone || !in_world())
        return false;
    const uintptr_t world = rd<uintptr_t>(g_base + g_addrs->world_slot);
    const uintptr_t mgr = world ? rd<uintptr_t>(world + kWorld_Mgr) : 0;
    const uintptr_t own = mgr ? rd<uintptr_t>(mgr + kMgr_OwnActor) : 0;
    const uintptr_t comps = own ? rd<uintptr_t>(own + kActor_Comps) : 0;
    const uintptr_t ts = comps ? rd<uintptr_t>(comps + kComps_TS) : 0;
    if (!ts || rd<uintptr_t>(ts + kTS_Actor) != own) return false;
    const uint32_t key = rd<uint32_t>(own + kActor_Key);
    if ((key >> 28) != 0xA) return false;
    *self_key = key;
    const uintptr_t hier = rd<uintptr_t>(comps + kComps_Hierarchy);
    const uint32_t parent = hier ? rd<uint32_t>(hier + kHier_Parent) : 0;
    if ((parent >> 28) != 0xB) return true;  // on nothing: the key alone
    float p[3], q[4];
    int16_t sector[2];
    if (!game::fast_read(ts + kTS_LocalPos, p, sizeof p) || !game::fast_read(ts + kTS_LocalQuat, q, sizeof q) ||
        !game::fast_read(ts + kTS_LocalSector, sector, sizeof sector))
        return true;
    // Local to its parent: no sector, a few metres at most, a unit rotation. Anything else is not sent.
    if (sector[0] || sector[1] || !vfl::unit_quat(q)) return true;
    for (const float v : p)
        if (!std::isfinite(v) || std::fabs(v) > 500.0f) return true;
    *anchor = parent;
    std::memcpy(pos, p, sizeof p);
    std::memcpy(quat, q, sizeof q);
    return true;
}

void vehicle_follow_on_peer_state(uint32_t self_key, uint32_t anchor, const float pos[3], const float quat[4]) noexcept {
    if (!self_key || !g_passenger_count.load(std::memory_order_relaxed)) return;
    for (auto& p : g_passengers) {
        if (p.key.load(std::memory_order_relaxed) != self_key) continue;
        const bool ok = (anchor >> 28) == 0xB && vfl::unit_quat(quat) && std::isfinite(pos[0]) && std::isfinite(pos[1]) &&
                        std::isfinite(pos[2]) && std::fabs(pos[0]) <= 500.0f && std::fabs(pos[1]) <= 500.0f &&
                        std::fabs(pos[2]) <= 500.0f;
        const uint32_t now_parent = ok ? anchor : 0;
        AcquireSRWLockExclusive(&p.lock);
        const uint32_t before = p.parent;
        p.parent = now_parent;
        if (ok) {
            std::memcpy(p.pos, pos, sizeof p.pos);
            std::memcpy(p.q, quat, sizeof p.q);
        }
        ReleaseSRWLockExclusive(&p.lock);
        if (before != now_parent && g_wake) SetEvent(g_wake);
        p.channel_ms.store(now_ms(), std::memory_order_relaxed);
        p.channel_states.fetch_add(1, std::memory_order_relaxed);
        return;
    }
}

bool vehicle_follow_peer_world(uint32_t self_key, float out[3]) noexcept {
    if (!self_key || !g_started.load(std::memory_order_relaxed) || !g_passenger_count.load(std::memory_order_relaxed))
        return false;
    for (size_t s = 0; s < kMaxPlayers; ++s) {
        Passenger& p = g_passengers[s];
        if (p.key.load(std::memory_order_relaxed) != self_key) continue;
        const ULONGLONG channel = p.channel_ms.load(std::memory_order_relaxed), now = now_ms();
        if (!channel || (now > channel && now - channel > kChannelFreshMs)) return false;
        const uint64_t state = p.chain_state.load(std::memory_order_acquire);
        float lp[3], lq[4];
        const uint32_t chain_for = chain_parent(state);
        AcquireSRWLockShared(&p.lock);
        uint32_t parent = p.parent;
        if (parent && chain_for && chain_for != parent && p.eased_for == chain_for) {
            // It stepped onto another part and its chain here is not built yet: the hooks keep drawing it on the part
            // before (passenger_pose), and so does the tag.
            parent = chain_for;
            std::memcpy(lp, p.eased_pos, sizeof lp);
            std::memcpy(lq, p.eased_q, sizeof lq);
        } else {
            // The eased pose the hooks draw it at, once there is one for this parent; else the pose that was sent.
            const bool eased = parent && p.eased_for == parent;
            std::memcpy(lp, eased ? p.eased_pos : p.pos, sizeof lp);
            std::memcpy(lq, eased ? p.eased_q : p.q, sizeof lq);
        }
        ReleaseSRWLockShared(&p.lock);
        if (!parent || chain_for != parent) return false;
        vfl::Pose base;
        if (!chain_world(p, state, &base)) return false;
        const vfl::Pose w = vfl::attach(base, lp, lq);
        if (!vfl::finite_pose(w)) return false;
        out[0] = static_cast<float>(w.x);
        out[1] = static_cast<float>(w.y);
        out[2] = static_cast<float>(w.z);
        return true;
    }
    return false;
}

namespace {
bool pose_differs(const vfl::Pose& a, const vfl::Pose& b) noexcept {
    return std::fabs(a.x - b.x) > 2e-3 || std::fabs(a.y - b.y) > 2e-3 || std::fabs(a.z - b.z) > 2e-3 ||
           std::fabs(a.q[0] - b.q[0]) > 1e-4f || std::fabs(a.q[1] - b.q[1]) > 1e-4f ||
           std::fabs(a.q[2] - b.q[2]) > 1e-4f || std::fabs(a.q[3] - b.q[3]) > 1e-4f;
}
}  // namespace

void vehicle_follow_observe() noexcept {
    if (!g_started.load(std::memory_order_relaxed) || !g_addrs || !g_own_vehicle_count.load(std::memory_order_relaxed) ||
        !in_world())
        return;
    const uint64_t now = now_ms();
    for (auto& o : g_own_vehicles) {
        const uint32_t key = o.key.load(std::memory_order_relaxed);
        const uintptr_t ts = o.ts.load(std::memory_order_acquire);
        if (!key || !ts) continue;
        const uintptr_t actor = rd<uintptr_t>(ts + kTS_Actor);
        if (!actor || rd<uint32_t>(actor + kActor_Key) != key) continue;
        vfl::Pose p;
        float unused[3];
        if (!ts_pose(ts, true, &p, unused) || !vfl::usable(p)) continue;
        o.read_ms = now;
        if (o.seen_key == key && o.seen_ms && !pose_differs(p, o.seen)) {
            o.jump = false;
            continue;
        }
        if (o.seen_key == key && o.seen_ms && vfl::Track::apart(o.seen, p)) {
            if (!o.jump || vfl::Track::apart(o.jump_pose, p)) {
                o.jump = true;
                o.jump_pose = p;
                g_own_lone_jumps.fetch_add(1, std::memory_order_relaxed);
                continue;  // the next look says whether it is really there
            }
        }
        o.jump = false;
        o.seen = p;
        o.seen_ms = now;
        o.seen_key = key;
    }
}

std::vector<uint8_t> vehicle_follow_own_vehicles() noexcept {
    std::vector<uint8_t> none;
    if (!g_started.load(std::memory_order_relaxed) || !g_addrs || !g_own_vehicle_count.load(std::memory_order_relaxed) ||
        !in_world())
        return none;
    try {
        vehicle_follow_observe();
        std::array<vfl::VehiclePose, vfl::kMaxVehiclePoses> poses;
        size_t n = 0;
        const uint64_t now = now_ms();
        for (auto& o : g_own_vehicles) {
            const uint32_t key = o.key.load(std::memory_order_relaxed);
            if (!key || !o.ts.load(std::memory_order_acquire) || !o.seen_ms || o.seen_key != key) continue;
            // Not read at this look nor at the last ones (its actor went away, its pose is unreadable): nothing, so
            // the other side goes back to the game's own updates of it instead of holding a stale pose.
            if (now - o.read_ms > kPoseReadFreshMs) continue;
            const vfl::Pose& p = o.seen;
            // Only what moved since it was last sent, and a keepalive so the other side keeps taking these.
            if (!pose_differs(p, o.sent) && now - o.sent_ms < kPoseKeepaliveMs) continue;
            o.sent = p;
            o.sent_ms = now;
            vfl::VehiclePose w;
            w.key = key;
            w.pos[0] = static_cast<float>(p.x);
            w.pos[1] = static_cast<float>(p.y);
            w.pos[2] = static_cast<float>(p.z);
            std::memcpy(w.q, p.q, sizeof w.q);
            w.age_ms = static_cast<uint16_t>(std::min<uint64_t>(now > o.seen_ms ? now - o.seen_ms : 0, 0xFFFF));
            poses[n++] = w;
        }
        if (!n) return none;
        auto out = vfl::encode_vehicles_timed(now, {poses.data(), n});
        if (!out[0]) return none;  // every pose refused by the encoder: no event with nothing in it
        g_vehicles_sent.fetch_add(1, std::memory_order_relaxed);
        return out;
    } catch (...) {
        return none;
    }
}

void vehicle_follow_on_peer_vehicles(std::span<const uint8_t> payload, bool timed) noexcept {
    if (!g_started.load(std::memory_order_relaxed) || !g_addrs) return;
    vfl::VehiclePose poses[vfl::kMaxVehiclePoses];
    uint64_t sender_ms = 0;
    const size_t n = timed ? vfl::decode_vehicles_timed(payload, &sender_ms, poses) : vfl::decode_vehicles(payload, poses);
    g_vehicle_events.fetch_add(1, std::memory_order_relaxed);
    const uint64_t now = now_ms();
    // Each sample at the moment the sender saw it, on this clock (vfl::SampleClock); an untimed event's at its arrival.
    const bool stamped = timed && n;
    if (stamped) {
        g_vehicle_timed_events.fetch_add(1, std::memory_order_relaxed);
        g_vehicle_clock.on_event(now, sender_ms);
        g_vehicle_clock_resets.store(g_vehicle_clock.resets(), std::memory_order_relaxed);
    }
    for (size_t i = 0; i < n; ++i) {
        bool listed = false;
        const uint64_t at = stamped ? g_vehicle_clock.at(now, sender_ms, poses[i].age_ms) : now;
        if (stamped && !i) g_vehicle_stamp_behind_ms.store(static_cast<int64_t>(now - at), std::memory_order_relaxed);
        for (auto& v : g_vehicles) {
            if (!v.ts.load(std::memory_order_acquire) || v.key.load(std::memory_order_relaxed) != poses[i].key) continue;
            vfl::Pose p;
            p.x = poses[i].pos[0];
            p.y = poses[i].pos[1];
            p.z = poses[i].pos[2];
            std::memcpy(p.q, poses[i].q, sizeof p.q);
            AcquireSRWLockExclusive(&v.lock);
            if (v.track.push(at, p)) v.samples.fetch_add(1, std::memory_order_relaxed);
            ReleaseSRWLockExclusive(&v.lock);
            v.channel_ms.store(now, std::memory_order_relaxed);
            v.channel_samples.fetch_add(1, std::memory_order_relaxed);
            listed = true;
            break;
        }
        if (!listed) g_vehicle_poses_unlisted.fetch_add(1, std::memory_order_relaxed);
    }
}

std::vector<uint8_t> vehicle_follow_own_riders() noexcept {
    std::vector<uint8_t> none;
    if (!g_started.load(std::memory_order_relaxed) || !g_addrs || !g_own_rider_count.load(std::memory_order_relaxed) ||
        !in_world())
        return none;
    try {
        std::array<vfl::Rider, vfl::kMaxRiders> riders;
        size_t n = 0;
        for (auto& r : g_own_riders) {
            const uint32_t key = r.key.load(std::memory_order_relaxed);
            const uintptr_t ts = r.ts.load(std::memory_order_acquire);
            if (!key || !ts) continue;
            // Still that actor's TransformSync (the worker's list is up to 3 s old).
            const uintptr_t actor = rd<uintptr_t>(ts + kTS_Actor);
            const uintptr_t comps = actor && rd<uint32_t>(actor + kActor_Key) == key ? rd<uintptr_t>(actor + kActor_Comps) : 0;
            if (!comps || rd<uintptr_t>(comps + kComps_TS) != ts) continue;
            const uintptr_t hier = rd<uintptr_t>(comps + kComps_Hierarchy);
            vfl::Rider x;
            x.key = key;
            x.anchor = hier ? rd<uint32_t>(hier + kHier_Parent) : 0;
            int16_t sector[2];
            if (!game::fast_read(ts + kTS_LocalPos, x.pos, sizeof x.pos) || !game::fast_read(ts + kTS_LocalQuat, x.q, sizeof x.q) ||
                !game::fast_read(ts + kTS_LocalSector, sector, sizeof sector))
                continue;
            // Local to its parent: no sector. One that left the ship (no parent) is simply not named any more.
            if (sector[0] || sector[1] || !vfl::rider_ok(x)) continue;
            riders[n++] = x;
        }
        if (!n) return none;
        g_riders_sent.fetch_add(1, std::memory_order_relaxed);
        return vfl::encode_riders({riders.data(), n});
    } catch (...) {
        return none;
    }
}

void vehicle_follow_on_peer_riders(std::span<const uint8_t> payload) noexcept {
    if (!g_started.load(std::memory_order_relaxed) || !g_addrs || g_addrs->char_request_type == kRequestNone) return;
    vfl::Rider riders[vfl::kMaxRiders];
    const size_t n = vfl::decode_riders(payload, riders);
    g_rider_events.fetch_add(1, std::memory_order_relaxed);
    const ULONGLONG now = now_ms();
    for (size_t i = 0; i < n; ++i) {
        const vfl::Rider& r = riders[i];
        Passenger* slot = nullptr;
        Passenger* unused = nullptr;
        for (size_t s = kMaxPlayers; s < kMaxPassengers; ++s) {
            Passenger& p = g_passengers[s];
            const uint32_t key = p.key.load(std::memory_order_relaxed);
            if (key == r.key) {
                slot = &p;
                break;
            }
            if (!key && !unused) unused = &p;
        }
        const bool fresh = !slot;
        if (fresh) {
            if (!unused) {
                g_rider_dropped.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            slot = unused;
            reset_slot(*slot);
            slot->rider.store(true);
        }
        AcquireSRWLockExclusive(&slot->lock);
        const uint32_t before = slot->parent;
        slot->parent = r.anchor;
        std::memcpy(slot->pos, r.pos, sizeof slot->pos);
        std::memcpy(slot->q, r.q, sizeof slot->q);
        ReleaseSRWLockExclusive(&slot->lock);
        slot->channel_ms.store(now, std::memory_order_relaxed);
        slot->channel_states.fetch_add(1, std::memory_order_relaxed);
        if (fresh) {
            slot->key.store(r.key);
            g_passenger_count.fetch_add(1);
            g_rider_count.fetch_add(1);
        }
        if ((fresh || before != r.anchor) && g_wake) SetEvent(g_wake);
    }
}

void vehicle_follow_on_user2_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    if (opcode != kOpMoveActorReq || !g_passenger_count.load(std::memory_order_relaxed)) return;
    const auto* l = movement::layout(game::current_build());
    if (!l || payload.size() != l->move_req) return;
    uint32_t key = 0;
    std::memcpy(&key, payload.data(), 4);
    if ((key >> 28) == 0xA) passenger_move(payload, *l, key);
}

void vehicle_follow_on_in(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    if (opcode != kOpMoveActorAck ||
        (!g_part_count.load(std::memory_order_relaxed) && !g_passenger_count.load(std::memory_order_relaxed)))
        return;
    const auto* l = movement::layout(game::current_build());
    if (!l || payload.size() != l->move_ack) return;
    uint32_t key = 0;
    std::memcpy(&key, payload.data(), 4);
    if ((key >> 28) == 0xA) {
        passenger_move(payload, *l, key);
        return;
    }
    if ((key >> 28) != 0xB) return;
    for (auto& p : g_parts) {
        if (p.key.load(std::memory_order_relaxed) != key) continue;
        uint64_t parent = 0;
        float pos[3];
        std::memcpy(&parent, payload.data() + l->parent, 8);
        std::memcpy(pos, payload.data() + l->local_pos, sizeof pos);
        AcquireSRWLockExclusive(&g_parts_lock);
        p.net_parent = parent == ~0ull ? 0 : static_cast<uint32_t>(parent);
        std::memcpy(p.net_pos, pos, sizeof pos);
        p.net_ms = now_ms();
        ReleaseSRWLockExclusive(&g_parts_lock);
        g_part_acks.fetch_add(1, std::memory_order_relaxed);
        return;
    }
}

void vehicle_follow_start(const std::string& mode, bool guest, uint32_t delay_ms, const std::string& air_mode) {
    const auto m = gil::parse_mode(mode);
    if (!m) {
        log::warn("VehicleFollow: vehicle_follow '{}' is not 0 | probe | 1; it stays off", mode);
        return;
    }
    if (*m == gil::Mode::Off) return;
    const Addrs* addrs = game::is_3576() ? &kAddrs3576 : game::is_3663() ? &kAddrs3663 : nullptr;
    if (!addrs) {
        log::warn("VehicleFollow: vehicle_follow is for builds 3576 and 3663 only; off");
        return;
    }
    if (g_started.exchange(true)) return;
    g_addrs = addrs;
    if (addrs == &kAddrs3663)
        log::info("VehicleFollow: build 1.0.0.3663: vehicle requests of types {} (near) and {} (far) are followed; the "
                  "setters' bytes are checked before hooking", addrs->request_type, addrs->request_type_far);
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    {
        LARGE_INTEGER f;
        g_qpc_freq = QueryPerformanceFrequency(&f) ? f.QuadPart : 0;
    }
    g_guest.store(guest);
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_mode.store(static_cast<uint8_t>(*m));
    g_delay_ms.store(std::clamp<uint32_t>(delay_ms, 0, 3000));
    if (const auto air = gil::parse_mode(air_mode); !air) {
        log::warn("VehicleFollow: passenger_air_follow '{}' is not 0 | probe | 1; it stays off", air_mode);
    } else if (*air != gil::Mode::Off && addrs != &kAddrs3663) {
        log::warn("VehicleFollow: passenger_air_follow is for build 3663 only; off");
    } else {
        g_air_mode.store(static_cast<uint8_t>(*air));
        if (*air != gil::Mode::Off)
            log::info("VehicleFollow: passenger_air_follow={}: while this client's own character is in the air after "
                      "leaving a part of another player's ship, its position requests {} by what the game's carry "
                      "lacked (for {:.1f} s at most, never down at the water or standing on firm ground, never for a "
                      "ship slower than {:.0f} m/s)",
                      gil::mode_name(*air),
                      *air == gil::Mode::On ? "are moved" : "are only worked out", vfl::kAirMaxS, vfl::kAirMinSpeed);
    }
    log::info("VehicleFollow ON ({}, vehicle_follow={}, delay {} ms): hooked once a ship of another user is found",
              guest ? "guest" : "host", gil::mode_name(*m), g_delay_ms.load());
    CloseHandle(CreateThread(nullptr, 0, worker, nullptr, 0, nullptr));
}

}  // namespace cdmp::session2
