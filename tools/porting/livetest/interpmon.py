"""interpmon.py pid key seconds [out] : one remote actor's position interpolation buffer on a copy, a few hundred looks a
second (3663: CC = comps+0x40, movement controller = CC+0xC8, position buffer = mc+0x430: array +0x08, count +0x10,
samples of 0xA0 bytes, time stamp +0x98, position at the sample's start). Every look in which something changed is a
line: the time, the number of samples, the oldest and newest time stamps (ms), how far ahead of the oldest the newest
is, the newest sample's position and the actor's TransformSync position. At the end: how often the count fell, how
long the drawn position stood still while the buffer's newest sample kept moving, and the largest jump. For a copy
that stalls and jumps (a ridden mount on the other player's screen, 16-nov). Read-only."""
import math
import os as _os
import struct
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
r = Reader(int(sys.argv[1]))
key = int(sys.argv[2], 16)
secs = float(sys.argv[3])
out = open(sys.argv[4], 'w') if len(sys.argv) > 4 else None
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
t = r.ptr(mgr + 8)
n = r.unpack(t + 0x88, '<I')[0]
b = r.ptr(t + 0x98) + (key % n) * 0x100
a = 0
for i in range(min(r.unpack(b, '<I')[0], 31)):
    k, idx = r.unpack(b + 8 + i * 8, '<II')
    if k == key:
        a = r.ptr(r.ptr(r.ptr(t + 0xA0) + idx * 8) + 8)
if not a:
    raise SystemExit(f'{key:08X} is not in this copy\'s actor table')
comps = r.ptr(a + 0x68)
ts_c = r.ptr(comps + 0x1A0)
cc = r.ptr(comps + 0x40)
mc = r.ptr(cc + 0xC8)
buf = mc + 0x430
print(f'actor 0x{a:X} cc 0x{cc:X} mc 0x{mc:X} (its cc back pointer {"ok" if r.ptr(mc + 0x60) == cc else "WRONG"})')
last = None
rows = []
t0 = time.perf_counter()
while time.perf_counter() - t0 < secs:
    try:
        cnt = r.unpack(buf + 0x10, '<I')[0]
        arr = r.ptr(buf + 0x08)
        first = newest = 0
        np = (0.0, 0.0, 0.0)
        if 0 < cnt <= 64 and arr:
            first = r.unpack(arr + 0x98, '<Q')[0]
            lastp = arr + (cnt - 1) * 0xA0
            newest = r.unpack(lastp + 0x98, '<Q')[0]
            np = r.unpack(lastp, '<3f')
        p = r.unpack(ts_c + 0xB4, '<3f')
        s = r.unpack(ts_c + 0xC0, '<hh')
    except Exception:
        time.sleep(0.005)
        continue
    cur = (cnt, first, newest, round(p[0], 3), round(p[2], 3))
    if cur != last:
        last = cur
        rows.append((time.perf_counter() - t0, cnt, first, newest, np, (p[0] + s[0] * 1000, p[1], p[2] + s[1] * 1000)))
    time.sleep(0.001)
falls = jumps = 0
still = 0.0
big = 0.0
prev = None
hold_from = None
for row in rows:
    tt, cnt, first, newest, np, p = row
    line = (f'{tt:7.3f} n {cnt:2d} oldest {first} newest {newest} (+{newest - first if cnt else 0:5d} ms) newest sample '
            f'({np[0]:.2f},{np[1]:.2f},{np[2]:.2f}) drawn ({p[0]:.2f},{p[1]:.2f},{p[2]:.2f})')
    if prev:
        if cnt < prev[1]:
            falls += 1
            line += f'  <-- count fell {prev[1]} -> {cnt}'
        d = math.hypot(p[0] - prev[5][0], p[2] - prev[5][2])
        if d > 1e-3:
            if hold_from is not None and tt - hold_from >= 0.3:
                still += tt - hold_from
                line += f'  <-- moved {d:.2f} m after standing {tt - hold_from:.2f} s'
                jumps += 1
                big = max(big, d)
            hold_from = tt
        elif hold_from is None:
            hold_from = tt
    if out:
        out.write(line + '\n')
    prev = row
print(f'{len(rows)} changes in {secs:.0f} s: the sample count fell {falls} times; the drawn position stood 0.3 s or more '
      f'{jumps} times ({still:.1f} s in all) and then moved up to {big:.2f} m at once')
