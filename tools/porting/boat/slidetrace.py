"""slidetrace.py pid seconds [min_step_m] : HOW a character slides along a ship's deck, frame by frame, in one copy.
About a thousand times a second it reads the copy's own character's position local to what it stands on and the
position of the copy's ship (the vehicle 0xB010000A), and prints one line for every change of the local position of
min_step_m (0.005) or more: when, the step along the deck, how far the ship moved since the character's local
position last changed, and the ship's speed over that stretch. At the end: how many steps, their sizes, and the steps
as a share of what the ship covered. One-frame-sized steps now and then, or a small creep in every frame? Read-only
(3663)."""
import math
import os as _os
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE + '/../livetest')
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
r = Reader(int(sys.argv[1]))
secs = float(sys.argv[2])
MIN = float(sys.argv[3]) if len(sys.argv) > 3 else 0.005
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
t = r.ptr(mgr + 8)
n = r.unpack(t + 0x88, '<I')[0]
buckets, nodes = r.ptr(t + 0x98), r.ptr(t + 0xA0)


def actor(key):
    b = buckets + (key % n) * 0x100
    for i in range(min(r.unpack(b, '<I')[0], 31)):
        k, idx = r.unpack(b + 8 + i * 8, '<II')
        if k == key:
            return r.ptr(r.ptr(nodes + idx * 8) + 8)
    return 0


own = r.ptr(mgr + 0x50)
comps = r.ptr(own + 0x68)
ts, hier = r.ptr(comps + 0x1A0), r.ptr(comps + 0x1B8)
ship_ts = r.ptr(r.ptr(actor(0xB010000A) + 0x68) + 0x1A0)


def ship():
    p = r.unpack(ship_ts + 0xB4, '<3f')
    s = r.unpack(ship_ts + 0xC0, '<hh')
    return (p[0] + s[0] * 1000, p[2] + s[1] * 1000)


last = r.unpack(ts + 0xB4, '<3f')
last_ship = ship()
last_t = t0 = time.perf_counter()
steps = []
frames = 0
prev_ship = last_ship
covered = 0.0
while time.perf_counter() - t0 < secs:
    par = r.unpack(hier + 0x1C, '<I')[0]
    if not par:
        print(f'{time.perf_counter() - t0:7.3f} the character has no parent any more (off the ship)')
        break
    p = r.unpack(ts + 0xB4, '<3f')
    s = ship()
    if s != prev_ship:
        frames += 1
        covered += math.hypot(s[0] - prev_ship[0], s[1] - prev_ship[1])
        prev_ship = s
    d = math.dist(p, last)
    if d >= MIN:
        now = time.perf_counter()
        moved = math.hypot(s[0] - last_ship[0], s[1] - last_ship[1])
        dt = now - last_t
        steps.append((d, moved))
        print(f'{now - t0:7.3f} step {d:6.3f} m (dx {p[0] - last[0]:+.3f} dz {p[2] - last[2]:+.3f}) | the ship moved '
              f'{moved:7.2f} m in the {dt:6.2f} s since the last step ({moved / max(dt, 1e-6):4.1f} m/s)', flush=True)
        last, last_ship, last_t = p, s, now
    time.sleep(0.0007)
total = sum(d for d, _ in steps)
print(f'{len(steps)} step(s) of {MIN} m or more in {time.perf_counter() - t0:.0f} s, {frames} ship frames, the ship covered '
      f'{covered:.0f} m; slid {total:.2f} m in all ({100 * total / max(covered, 1e-6):.2f}% of that)'
      + (f'; steps from {min(d for d, _ in steps):.3f} to {max(d for d, _ in steps):.3f} m' if steps else ''))
