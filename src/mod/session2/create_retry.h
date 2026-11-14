#pragma once
// [session2] create_retry = 0 | probe (default) | 1, GUEST, build 1.0.0.3663. The pure part and the whole story:
// create_retry_logic.h.
//
// On the guest some fittings of the host's ship are never created (the lit stern lantern among them): their records
// reach the client seconds before the vehicle and the hull they dock to, their creation fails ("Actor
// AsyncPostProcessForCreate failed", game_errors) and nothing sends them again. With 1 the guest keeps every gimmick
// record whose attach parent (record +0x113) is neither in the same ActorLoadDataAck nor on this client yet; a
// reported failure of one of them makes its record go to the client again, through the usual path, once the parent
// is there (at most twice per record), and then the kept records that hang from it and are still missing.
// probe: the same bookkeeping and lines, nothing delivered. Nothing is asked of the host: both PCs keep working with
// a peer that does not have this.
// Everything here runs on the mod's network thread (guest.cpp deliver_body / guest_take_outgoing); the reads of the
// client's actor table are the guarded ones of game/actor_lookup.h.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

// The worker's start-up. raw = the ini value; false: off (or not this build).
bool create_retry_configure(const std::string& raw, uint32_t lost_budget);
// Every packet of the host on its way to the client (logical opcode, payload; the connection's epoch).
void create_retry_on_in(uint16_t opcode, std::span<const uint8_t> payload, uint64_t epoch) noexcept;
// The network thread, every turn: the packets (whole, as troc::make builds them) to give to the client now.
void create_retry_tick(uint64_t epoch, bool remote_live, std::vector<std::vector<uint8_t>>& out) noexcept;
std::string create_retry_describe();  // atomics only ("" when off)

}  // namespace cdmp::session2
