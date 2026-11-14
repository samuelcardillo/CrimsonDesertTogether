#pragma once
// Development aid (2026-11-14, the ship's sails): which client code is behind a request this client sends.
//
// [session2] send_trace = (empty: off) | a list of LOGICAL opcodes, e.g. "2609" or "2609,2758". Both roles.
//
// The game's per-message send stubs are thunks into the protected section, so the code that sends, say, a
// GimmickBranchStateReq (2609: the owner's client changing a gimmick's branch state, which the game never passes on to
// the other player) cannot be found from the exe on disk. With this on, for the first kLinesPerOp packets of each
// listed opcode that this client posts (ClientNet::PostPacket, the sniffer's mid hook, on the sending thread), one log
// line: the packet's first bytes and every value on the sender's stack that points into the exe's executable sections
// right after a call instruction (a scan of the stack, not an unwind: the protected frames have no unwind data). The
// first few are the send path; the ones after are the game code that decided to send.
// Read-only: the stack and the bytes before each candidate are read SEH-guarded; nothing is written.

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// The worker's start-up (after the sniffer is installed). raw = the ini value. Nothing happens when it is empty.
void send_trace_start(const std::string& raw);

}  // namespace cdmp::session2
