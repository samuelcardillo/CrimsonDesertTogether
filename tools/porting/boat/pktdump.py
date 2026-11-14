"""pktdump.py capture.bin name key [t_from t_to] [max] [uniq] : the packets of one message (its name in the 3663 native
opcode table, e.g. UpdateGimmickReq) whose payload starts with an actor key (hex; 0 = any): capture time, direction,
length and payload hex. With 'uniq' as the last argument only payloads not seen before (ignoring nothing) are printed,
with how many came between. Read-only."""
import mmap
import struct
import sys
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/scripts')
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/boat')
from analyze_capture import REC

import re
names = {}
on = False
for line in open(r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/src/common/native_opcode_table.inc', encoding='utf-8'):
    if line.startswith('constexpr'):
        on = 'kEntries3663' in line
        continue
    if on:
        mm = re.match(r'\s*\{(\d+), (\d+), \d+U, "(\w+)"\}', line)
        if mm:
            names[mm.group(3)] = int(mm.group(2))

args = sys.argv[1:]
uniq = args and args[-1] == 'uniq'
if uniq:
    args = args[:-1]
path, name, key = args[0], args[1], int(args[2], 16)
ta = float(args[3]) if len(args) > 3 else 0.0
tb = float(args[4]) if len(args) > 4 else 1e12
mx = int(args[5]) if len(args) > 5 else 40
want = names[name]
f = open(path, 'rb')
m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
pos = 12
t0 = None
seen = set()
skipped = 0
shown = 0
while pos + REC.size <= len(m) and shown < mx:
    t, d, _, size, copied, _, sess, thread = REC.unpack_from(m, pos)
    pos += REC.size
    pkt = m[pos:pos + copied]
    pos += copied
    if t0 is None:
        t0 = t
    tt = (t - t0) / 1e6
    if tt < ta or len(pkt) < 9:
        continue
    if tt > tb:
        break
    op = struct.unpack_from('<H', pkt, 0)[0]
    if op != want:
        continue
    payload = bytes(pkt[5:])
    if key and struct.unpack_from('<I', payload, 0)[0] != key:
        continue
    if uniq:
        if payload in seen:
            skipped += 1
            continue
        seen.add(payload)
    print(f't={tt:9.3f} {"C>S" if d == 1 else "S>C"} len {len(payload):4d} (+{skipped}) | {payload[:200].hex(" ")}')
    skipped = 0
    shown += 1
