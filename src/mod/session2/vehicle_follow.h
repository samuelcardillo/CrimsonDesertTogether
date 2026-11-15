#pragma once
// [session2] vehicle_follow = 0 (default) | probe | 1, either PC (builds 1.0.0.3576 and, static only, 3663). docs/ESTADO-3527.md
// "13-nov: BARCO"; the pure part (smoothing, sector form): vehicle_follow_logic.h.
//
// The other player's ship. Each client simulates its own copy of it; this client's copy follows only the replayed
// helmsman actions and drifts away (no sail/speed state, and the client never applies MoveActorAck to a type-7 actor).
// The ship gimmick (type 7, ClientVehicleActorComponent) hangs in the hierarchy (attach type 7) from the vehicle actor
// (type 5, pa::ClientChildOnlyInGameActor), whose TransformSync the client asks every frame, with a position and a
// rotation request of type 7 (0xB03BB0 / 0xB03FB0 from its character update), to sit on the local ship; the ship is
// then drawn from that parent. The vehicle actor's last network pose (the controller's, ~1 Hz) is in its TransformSync
// at +0x550 (world vec3) / +0x55C (quaternion).
//
// With 1, for vehicles of a ship whose controller (+0x90) is not this client's user, those two requests get a pose
// drawn vehicle_follow_delay_ms (300) behind the controller's samples (interpolated, extrapolated up to 1 s past the
// newest; vfl::Track; about the delay its character is drawn with, so it stays on the wheel): the ship, its parts and what stands on it are
// drawn where the controller has them. Nothing else is touched: the local ship's physics body still runs underneath
// (its collision stays where this client's own simulation has it). probe: the same detection and the same pose,
// measured against the local one, never written.
// Which vehicles: every type-5 ChildOnly actor of another user that a vehicle gimmick hangs from (attach type 7), one
// entry each. Most are the other player's stabled horses with ten-odd gear gimmicks each, which never get a type-7
// request; the one with a ship out gets one every frame, and only those requests are touched.
// Mechanism: two mid hooks at the setters' entries (verified bytes, 5-byte patch), installed only when a listed vehicle
// is drawn from its gimmick (its pending request type, TS+0x5BC, seen at 7 for a few frames of polling; never during
// the world loads before it); the list is rebuilt every 3 s by a worker from the actor table while the client is in
// the world (load state 0x18; never during a load), with SEH-guarded reads; the hooks only compare pointers.
// Lines 'VehicleFollow ...': the vehicles listed or unlisted, the install, each vehicle followed (and its counts when
// it goes), a summary every 60 s while something happens.

//
// Request types (3663, live 2026-11-14): 7 every frame while this client is near the ship (as on 3576), 12 a few times a
// second while it is far away; both are followed on 3663 (3576: 7 only, the far one was never looked at). The far type
// is also the type of a character's own per-frame request, and the list holds the other user's mounts too, so a far
// request is followed only for a vehicle that has been seen with the NEAR type (a ship this client has been near): a
// mount that moves like a character is left to the game ('... gets far-type position requests ...' says so once).
// Before that (kit v23) a listed non-ship vehicle was followed once in a live session.
// The drawn pose: time from the performance counter (GetTickCount64 steps by 10-16 ms: a stutter above ~64 fps); past
// the newest sample the velocity is taken over at least 500 ms of samples and capped at 40 m/s (two ~1 Hz samples that
// arrive a frame apart, a resent packet, would otherwise read as hundreds of m/s for a second).
// The ship's parts (3663 only): a gimmick attached by a socket under a followed vehicle (the ship's wheel on its mount)
// is created on the other client with its offset from the vehicle instead of from its parent (the wheel floats 2.9 m
// above the deck). Its controller's local position arrives in its MoveActorAck (vehicle_follow_on_in, guest side); when
// the part's scene object is more than 0.5 m off, the position is written once into the scene object
// ([[comps+0x30]+0x18]-0x28, local transform at +0x1D0). 'VehicleFollow: part ...' lines; probe: logged only.
// Passengers (3663 only, both PCs): the client draws the other player's character at the world pose of its Moves, never
// attached to this client's copy of what it stands on: a passenger standing still on a sailing ship stays behind in
// the water (the host saw the guest 107 m astern at 12.7 m/s), and the helmsman sits 0.3-1.9 m off the wheel. While the
// other player's last Move names a gimmick hanging from a vehicle, its character's own per-frame requests (type 12)
// get the pose of this client's copy of that gimmick composed with the Move's local pose (eased over 120 ms). The
// What it stands on comes from the other PC's mod, 20 times a second, in the mod's own PlayerState (protocol 10:
// vehicle_follow_own_anchor on the sender, vehicle_follow_on_peer_state on the receiver): its hierarchy parent and its
// pose local to it. It has to: for a character on a ship's deck (parent = the hull, a ladder, a deck part) the game's
// server sends the other player BLANK MoveActorAcks (all zeros, 30 a second; 14-nov, guest capture), so the guest saw
// the host stay where it boarded until it took the wheel (a mount parent is the one the server keeps). The game's
// Moves (vehicle_follow_on_in on the guest, vehicle_follow_on_user2_req on the host) are only the fallback when no
// channel state came in the last 2 s.
// 'VehicleFollow: character ...' lines. The host needs vehicle_follow=1 too for this.
// After the review of 2026-11-14 (none of it seen to fail on one PC; all of it could on two). A second review, of
// these changes against kit v23, added: a far request still puts the hooks in and has the vehicle's parts watched
// (v23 mended the wheel while the guest was 6 km away; only the POSE waits for a near request); a still passenger's
// own request no longer keeps the module's away for 30 ms; two time differences can no longer wrap.
//  - a character or a chain pointer can outlive its actor for a worker tick: every use checks that the TransformSync
//    still belongs to the actor with the expected key, and the calls into the game's setters also ask the actor table;
//  - during a load the passengers' pointers are dropped, and a setter that faulted once is never called again for
//    that character;
//  - the chain of what the character stands on is published whole (two slots), and while the chain of a NEW part is
//    not built yet the character stays where it was drawn on the part before (it used to show at the game's own
//    stale pose for up to 250 ms at every step from part to part); the worker is woken at once by a change;
//  - a passenger standing still gets a request EVERY frame (it got one every 30 ms: every other frame at 60 fps, a
//    0.2 m sawtooth against the deck at full sail);
//  - the summary counts the calls made on another thread than the character's own requests (if the game updates
//    characters on several threads those calls race its update: not known yet).

// Riders (3663, both PCs; 2026-11-14): the crew of a ship is in the same position as a passenger, only worse: NPC
// actors docked to a part of the hull, for which the server also gives the other player blank Moves, so they stay
// where the ship was when they appeared and are left behind over the water when it sails. The mod of the ship's
// controller names them ten times a second (proto::EventKind::Riders: each one's hierarchy parent and local pose) and
// the receiver draws each on its own copy of that parent, like the other player's character: its own requests are
// answered (whatever their type) and, while it stands still, it is given one every frame. That holds whether or
// not the rider has a parent on this client: the game keeps some of the crew attached at the pose of their creation
// and drops the parent of the others, and a position request is in world coordinates either way.
// 'VehicleFollow: rider ...' lines, and one part of the summary. An older mod on the other side drops the event:
// nothing changes for it.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

// delay_ms: [session2] vehicle_follow_delay_ms (default 300; 0..3000).
void vehicle_follow_start(const std::string& mode, bool guest, uint32_t delay_ms);
// Guest: every packet of the host before the client gets it (the mod's network thread). Keeps the controller's last
// local position of the watched ship parts (MoveActorAck, logical 2697); nothing else. Off or no part watched: nothing.
void vehicle_follow_on_in(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// Host: every Req of user 2 as it is queued for the server (the mod's network thread). Keeps user 2's character's last
// parent and local pose (MoveActorReq, logical 2556); nothing else. Off or no other character listed: nothing.
void vehicle_follow_on_user2_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// The mod's own channel (protocol 10), both PCs, the mod's network thread.
// Sender: this client's character's key and, when it stands on something (a hierarchy parent), that actor's key and
// the character's pose local to it. False (nothing to send): the module is off, the build has no passengers, or the
// client is not in the world. anchor 0: on nothing.
bool vehicle_follow_own_anchor(uint32_t* self_key, uint32_t* anchor, float pos[3], float quat[4]) noexcept;
// Receiver: the other player's last state. Kept for the listed character with that key; nothing else.
void vehicle_follow_on_peer_state(uint32_t self_key, uint32_t anchor, const float pos[3], const float quat[4]) noexcept;
// Where THIS client draws the other player's character (its feet) while it stands on a ship part: this client's copy of
// the part with the local pose the other mod sent, what the passenger hooks give the game. For the name tag: the
// position the other mod sends is where ITS client has the ship, and this client draws the ship a moment behind (the
// follower's delay), so on a sailing ship the tag ran metres ahead of the character or behind it. Any thread (the
// overlay calls it every frame): guarded reads and a shared lock. False: no such character listed, it stands on no
// ship part, its state is older than 2 s, or its parent is unreadable.
bool vehicle_follow_peer_world(uint32_t self_key, float out[3]) noexcept;
// Vehicle poses (2026-11-14): the game tells the other client where a vehicle is about once a second; each mod sends
// where its client has the vehicles it controls (its ship, the NPCs' ships and boats) ten times a second while they
// move, and the other mod's follower uses those samples. Sender: the payload (empty: nothing to send now). Receiver:
// the other mod's event. The mod's network thread.
// Since 2026-11-15 each pose goes with its time (proto::EventKind::VehiclesTimed): vehicle_follow_observe, called on
// every turn of the network thread's loop (every 5 ms or less), notes when each of this client's vehicles got the pose
// it has, and the receiver places the sample at that moment on its own clock, not at its arrival (why:
// vehicle_follow_logic.h, "timed vehicle poses"). `timed` false: an event of a release b2/b3 peer
// (proto::EventKind::Vehicles), stamped on arrival as before.
void vehicle_follow_observe() noexcept;
std::vector<uint8_t> vehicle_follow_own_vehicles() noexcept;
void vehicle_follow_on_peer_vehicles(std::span<const uint8_t> payload, bool timed) noexcept;
// Riders, sender (the mod's network thread, ten times a second): the payload of a proto::EventKind::Riders event for
// the actors that ride a ship of this client's user. Empty: none (or the module is off, or a load is going on).
std::vector<uint8_t> vehicle_follow_own_riders() noexcept;
// Riders, receiver (the mod's network thread): the other player's event. A rider not named for 3 s is let go.
void vehicle_follow_on_peer_riders(std::span<const uint8_t> payload) noexcept;

}  // namespace cdmp::session2
