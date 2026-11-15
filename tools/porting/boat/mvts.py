"""mvts.py capture.bin key[,key...] t_from t_to [max] : the MoveActorAck packets of those actors (3527..3663 layout,
239 bytes) with their TIME STAMP: capture time, key, the packet's ts (u64 ms at +212) and its distance to the previous
packet's of the same actor, parent (+68), local position (+4), the Path's world position (+76) and the position at
+196. For the order of two streams about one actor (a ridden mount: its own packets about once a second and the ones
the host's mod derives from its rider's, horse_follow). Read-only."""
import mmap
import re
import struct
import sys

ROOT = r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi'
REC = struct.Struct("<QBBHHHII")  # t_us dir pad size copied pad session thread
native = None
on = False
for line in open(ROOT + '/src/common/native_opcode_table.inc', encoding='utf-8'):
    if line.startswith('constexpr'):
        on = 'kEntries3663' in line
        continue
    mm = re.match(r'\s*\{(\d+), (\d+), \d+U, "MoveActorAck"\}', line)
    if mm and on:
        native = int(mm.group(2))
keys = {int(k, 16) for k in sys.argv[2].split(',')}
ta, tb = float(sys.argv[3]), float(sys.argv[4])
mx = int(sys.argv[5]) if len(sys.argv) > 5 else 400
f = open(sys.argv[1], 'rb')
m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
pos = 12
t0 = None
shown = 0
last_ts = {}
while pos + REC.size <= len(m) and shown < mx:
    t, d, _, size, copied, _, sess, thread = REC.unpack_from(m, pos)
    pos += REC.size
    pkt = m[pos:pos + copied]
    pos += copied
    if t0 is None:
        t0 = t
    tt = (t - t0) / 1e6
    if tt > tb:
        break
    if tt < ta or len(pkt) != 5 + 239 or struct.unpack_from('<H', pkt, 0)[0] != native:
        continue
    p = bytes(pkt[5:])
    key = struct.unpack_from('<I', p, 0)[0]
    if key not in keys:
        continue
    parent = struct.unpack_from('<Q', p, 68)[0]
    lp = struct.unpack_from('<3f', p, 4)
    wp = struct.unpack_from('<3f', p, 76)
    ap = struct.unpack_from('<3f', p, 196)
    ts = struct.unpack_from('<Q', p, 212)[0]
    par = 'none' if parent == 0xFFFFFFFFFFFFFFFF else f'{parent & 0xFFFFFFFF:08X}'
    dts = ts - last_ts[key] if key in last_ts else 0
    last_ts[key] = ts
    print(f't={tt:9.3f} {"S>C" if d else "C>S"} {key:08X} ts {ts} ({dts:+6d}) parent {par:>8s} local ({lp[0]:9.2f},{lp[1]:7.2f},'
          f'{lp[2]:9.2f}) world ({wp[0]:9.2f},{wp[1]:7.2f},{wp[2]:9.2f}) +196 ({ap[0]:9.2f},{ap[1]:7.2f},{ap[2]:9.2f})')
    shown += 1
