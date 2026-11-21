#pragma once
// [session2] sea_sync = 1 (default) | 0, both PCs, build 1.0.0.3663 only (the reasons and the rules: sea_sync_logic.h;
// the game's side: docs/research/mar-3663.md). The guest draws the host's swell, so a ship (whose pose is the host's)
// sits in the same waves on both PCs.
//
//   both      one inline hook at the entry of exe+0x3FCE750 (sea): the routine that, once per frame, copies the sea's
//             twelve wave trains into the constant buffer that goes to the GPU. Installed by this module's thread once
//             the world is up; its 24 entry bytes are checked first and only the first two instructions (7 bytes) move.
//   host      in that hook: the frame's twelve trains and the wind's strength are read (never written) and kept with
//             the time. The network thread sends the newest as tunnel control state 17, 10 Hz, inside the
//             conversation with user 2. A single slot, never a backlog.
//   guest     the frames only store the sample (network thread). In the hook, before the game's copy: the host's
//             trains, carried forward to now, are written over this client's own in the frame about to be copied
//             (guarded writes of 5 words per train). Nothing is written without a fresh sample (1.5 s) of a sea that
//             moves: then this client's sea goes on by itself from where it is.
// 3732 (static, 2026-11-21; never run): the address above is 3663's; the routine is exe+0x3FD2C40 there, the same
// entry bytes (sea_sync.cpp).

#include <cstdint>
#include <span>

#include "common/troc_link.h"

namespace cdmp::session2 {

// From the worker, once, on either PC.
void sea_sync_start(bool enabled, bool guest);

// Host, network thread, every pass of its loop: the newest frame goes into the tunnel when there is a new one, 100 ms
// have passed, user 2 is in the world and the tunnel is not far behind. Flushed by the next tunnel_tick.
void sea_sync_host_tick(troc::TrocLink& link) noexcept;

// Guest, network thread: a tunnel packet from the host: true when it was this module's (taken out of the stream).
bool sea_sync_on_frame(std::span<const uint8_t> frame, uint64_t epoch) noexcept;

}  // namespace cdmp::session2
