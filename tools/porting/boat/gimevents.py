"""gimevents.py capture.bin key_lo key_hi : the gimmick updates a client sent (UpdateGimmickReq, client to server) for
the keys in [key_lo, key_hi] (hex), per key: each (event, parameter) pair with its count and first / last capture
time; and, per key, how many GimmickBranchStateReq, GimmickExecuteFrameEventReq and
CreateProjectileFromGimmickEventReq it sent. Events as the u32 the mod logs (0x003ECC59), not as packet bytes.
Read-only. Build 1.0.0.3663 (native opcodes from src/common/native_opcode_table.inc)."""
import collections
import mmap
import re
import struct
import sys

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/scripts')
from analyze_capture import REC  # noqa: E402

names = {}
on = False
for line in open(r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/src/common/native_opcode_table.inc',
                 encoding='utf-8'):
    if line.startswith('constexpr'):
        on = 'kEntries3663' in line
        continue
    if on:
        mm = re.match(r'\s*\{(\d+), (\d+), \d+U, "(\w+)"\}', line)
        if mm:
            names[int(mm.group(2))] = mm.group(3)

path = sys.argv[1]
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
OTHER = ('GimmickBranchStateReq', 'GimmickExecuteFrameEventReq', 'CreateProjectileFromGimmickEventReq',
         'UpdateGimmickAck')
f = open(path, 'rb')
m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
pos = 12
t0 = None
events = collections.defaultdict(dict)  # key -> (event, param) -> [count, first, last]
other = collections.defaultdict(collections.Counter)
while pos + REC.size <= len(m):
    t, d, _, size, copied, _, sess, thread = REC.unpack_from(m, pos)
    pos += REC.size
    pkt = m[pos:pos + copied]
    pos += copied
    if t0 is None:
        t0 = t
    if len(pkt) < 9:
        continue
    op = struct.unpack_from('<H', pkt, 0)[0]
    name = names.get(op)
    if name != 'UpdateGimmickReq' and name not in OTHER:
        continue
    k = struct.unpack_from('<I', pkt, 5)[0]
    if not (lo <= k <= hi):
        continue
    tt = (t - t0) / 1e6
    if name == 'UpdateGimmickReq' and d == 1 and len(pkt) >= 5 + 12:
        ev, par = struct.unpack_from('<II', pkt, 9)
        e = events[k].setdefault((ev, par), [0, tt, tt])
        e[0] += 1
        e[2] = tt
    else:
        other[k][('C>S ' if d == 1 else 'S>C ') + name] += 1
for k in sorted(set(events) | set(other)):
    print(f'{k:08X}')
    for (ev, par), (n, a, b) in sorted(events[k].items(), key=lambda e: e[1][1]):
        print(f'   UpdateGimmickReq event 0x{ev:08X} parameter 0x{par:08X}: {n:5d}  t {a:8.1f} .. {b:8.1f}')
    for name, n in sorted(other[k].items()):
        print(f'   {name}: {n}')
