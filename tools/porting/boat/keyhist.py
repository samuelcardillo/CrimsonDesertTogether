"""keyhist.py capture.bin key_lo key_hi [t_from t_to] [skip_moves] : per (direction, message, key) counts of every packet
in a .bin capture whose payload STARTS with an actor key in [key_lo, key_hi] (hex), and the capture time of the first
and the last one. With a 6th argument, MoveActorReq/Ack are left out. Read-only.
Directions as the sniffer writes them: 1 = client to server, 2 = server to client."""
import collections
import mmap
import struct
import sys
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/scripts')
from analyze_capture import REC


def load_names(section='kEntries3663'):
    """native id -> name for the installed build, from src/common/native_opcode_table.inc (the capture holds native ids)."""
    import re
    names, on = {}, False
    for line in open(r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/src/common/native_opcode_table.inc', encoding='utf-8'):
        if line.startswith('constexpr'):
            on = section in line
            continue
        if on:
            mm = re.match(r'\s*\{(\d+), (\d+), \d+U, "(\w+)"\}', line)
            if mm:
                names[int(mm.group(2))] = mm.group(3)
    return names

path = sys.argv[1]
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
ta = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
tb = float(sys.argv[5]) if len(sys.argv) > 5 else 1e12
skip_moves = len(sys.argv) > 6
names = load_names()
f = open(path, 'rb')
m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
pos = 12
t0 = None
cnt = collections.Counter()
span = {}
last_t = 0.0
while pos + REC.size <= len(m):
    t, d, _, size, copied, _, sess, thread = REC.unpack_from(m, pos)
    pos += REC.size
    pkt = m[pos:pos + copied]
    pos += copied
    if t0 is None:
        t0 = t
    tt = (t - t0) / 1e6
    last_t = tt
    if tt < ta or len(pkt) < 9:
        continue
    if tt > tb:
        break
    op, slot, plen = struct.unpack_from('<HbH', pkt, 0)
    k = struct.unpack_from('<I', pkt, 5)[0]
    if not (lo <= k <= hi):
        continue
    name = names.get(op, str(op))
    if skip_moves and name in ('MoveActorReq', 'MoveActorAck', 'RegionInOutReq'):
        continue
    key = (d, slot, name, k)
    cnt[key] += 1
    a = span.get(key)
    span[key] = (a[0] if a else tt, tt)
print(f'capture ends at t={last_t:.1f} s')
for key, n in sorted(cnt.items(), key=lambda e: (e[0][3], e[0][0], -e[1])):
    d, slot, name, k = key
    print(f'{k:08X} {"C>S" if d == 1 else "S>C"} slot {slot:2d} {name:40s} {n:6d}  t {span[key][0]:8.1f} .. {span[key][1]:8.1f}')
