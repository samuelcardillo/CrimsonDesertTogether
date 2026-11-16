#pragma once
// [session2] gimmick_relay = 0 (default) | probe | 1, HOST, build 1.0.0.3663. EXPERIMENT (2026-11-14, the ship's
// sails), never run in the game yet.
//
// What is known (the host's and the guest's captures of that morning, docs/research/port-3663.md):
//  - the sail is one gimmick of the ship. When the helmsman changes gear, the owner's client changes its branch state
//    and executes frame events on it, and tells the server: GimmickBranchStateReq 2609, GimmickExecuteFrameEventReq
//    3082 and UpdateGimmickReq 2758 (key | 0xF0 bytes: event hash, parameter, ..., the vehicle's key twice, a time);
//  - the game passes none of that on to a client that does not control the gimmick, so the guest's sail never moves;
//  - a client applies a gimmick update the server CONFIRMS: UpdateGimmickAck 2297 (key | key | the same 0xF0 bytes)
//    -> its handler 0xD0A050 -> 0xA005C0(the gimmick component, &result, the 0xF0 bytes), which queues the update for
//    the component, but only when this client's user is the gimmick's controller (actor +0x90).
// So, with 1, every UpdateGimmickReq the host's own client sends for a SAIL-LIKE gimmick of one of its own ships is
// given to user 2 as the UpdateGimmickAck the server would write for it (server_push_extra). Sail-like: nothing hangs
// from it and it hangs directly from a vehicle that is a ship, i.e. one with at least 6 gimmicks hanging directly from
// it, one of which carries parts itself (the wheel's mount, a lantern). That takes in the sail and the cannons and
// leaves out the hull (whose update repeats twice a second), the wheel's mount, and everything of a mount or a cart.
// With gimmick_relay_states = * only: a periodic update (the same gimmick, event and parameter less than a second
// after the one before) goes twice and is then left out until it has been silent for a second.
// Since 2026-11-16 (the AI's ships, whose sails stayed furled on the guest; docs/research/port-3663.md, "Barco, 16-nov
// por la tarde"), for the sail's states (not with *):
//  - a ship is a vehicle with TWO or more gimmicks hanging directly from it, one of them carrying parts (an AI ship
//    seen from far has four; the six-part rule left it out on both PCs);
//  - a gimmick that has taken one of the listed states is a sail from then on, and EVERYTHING its controller says
//    about it is relayed (a sail goes through states that are not on the list, and the guest's copy does not skip a
//    gear);
//  - the last state each sail entered is kept and said again: 2, 8 and 20 s after its gimmick comes onto the list (a
//    ship that appeared, or user 2 that arrived), and every 30 s while it stays there with no news (the guest makes
//    its copy of a ship when the ship comes into ITS range, and a copy starts furled).
// "GimmickRelay trace" lines (400 a session): each update of a sail, its branch on this client, and whether it went.
// [session2] gimmick_relay_states = hex state hashes | * (default: empty = the sail's four states of 2026-11-14:
// 0x29A56175 furled, 0xC3738DA8 first gear, 0x0FE0996E second, 0x8DC1B584 third). Every gimmick's updates use the
// same events (0x003ECC59 enter the state in the parameter, 0x92786A19 leave it, 0x128B06E9 / 0 settled); what tells
// the sail from a cannon (states 0x1F72B718, 0x46A85472) is the state, the u32 at +8 of the request. Only updates of
// a listed state are relayed, and the "settled" of a gimmick that had one; the others are counted and said once. So
// a shot does not make the guest that cannon's controller.
// The guest's client will only apply an update to a gimmick it controls on its own copy: making it the sail's
// controller there is the other half of the experiment (tools/porting/boat/setctl.py, or a record rewritten at
// creation). probe: the same lines and counts, nothing queued.
//
// The guest's half: [session2] gimmick_take = 0 (default) | probe | 1, GUEST, 3663, the same experiment. For an
// UpdateGimmickAck on its way to the client whose gimmick is on this client, is controlled by ANOTHER user, hangs
// directly from a vehicle that user controls and is sail-like on this client too (the same test), this client's own
// user is written into the gimmick's controller (actor +0x90) before the packet goes on: the client then lets the
// update through. The controllers are read at every Ack (the actor is looked up by key again right before the write);
// only what the table pass said about the gimmick's place in its ship is remembered, for 30 s, and forgotten at a
// field change. One line per gimmick and outcome. Whether a gimmick that was not created under this client's control
// can run the update is what the experiment is for; so is what the client does afterwards with a gimmick it now
// believes it controls. Its own requests for such a gimmick (the list of docked_gimmicks_logic.h: 3082 2758 2609
// 2774 3089 2779 2826 2618; and its movement: 2556 2828 3067) are WITHHELD in guest_send, as docked_gimmicks does
// for the state requests of the gimmicks it rewrites (since 16-nov the same smaller-ship rule for a sail's states, and
// "GimmickTake trace" lines: each Ack that arrives with the branch this copy is in, and the branch when the gimmick
// is given back):
// the 2976 server ignored the first three from a non-controller but took the others from anyone (2779 fires a
// projectile). probe: logged, not written, nothing withheld.
//
// Threads: the sniffer's PostPacket probe (the client's sending thread) copies the packet into a fixed ring under a
// try-lock and signals the module's worker, which builds and queues the Ack; the worker also lists the sail-like
// gimmicks of the host's own ships every 2 s while user 2 is in (SEH-guarded reads of the client's actor table). The
// guest's half runs on the mod's network thread; its write to the actor is SEH-guarded, but nothing stops the game
// from releasing the actor between the look-up and the write (a window of microseconds): an experiment's risk, not
// something to ship.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// session2::install on the host (after the sniffer is in). raw / states = the ini values.
void gimmick_relay_start(const std::string& raw, const std::string& states);
std::string gimmick_relay_describe();  // atomics only ("" when off)

// The guest's worker start-up. raw = the ini value of gimmick_take; hold_ms = gimmick_take_hold_ms: how long after
// the host's last update of a taken gimmick its controller on this client is written back (0: never).
void gimmick_take_configure(const std::string& raw, uint32_t hold_ms);
// Guest, the mod's network thread, a few times a second: gives back what has been held long enough.
void gimmick_take_tick() noexcept;
// Guest: every packet of the host on its way to the client (logical opcode, payload); the mod's network thread.
void gimmick_take_on_in(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// Guest: a request of the client about to go to the host (guest_send; header + payload, logical opcode). True:
// withhold it (a state request of a gimmick taken on this client only).
bool gimmick_take_withhold(const uint8_t* packet, size_t size) noexcept;

}  // namespace cdmp::session2
