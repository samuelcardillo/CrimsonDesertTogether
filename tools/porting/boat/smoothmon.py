"""smoothmon.py pid key seconds : how smoothly ONE copy moves an actor (a ship's vehicle actor on the guest): samples
its TransformSync position as fast as it can, takes every change as a frame, and for the time it moves at 2 m/s or
more counts the frames in which it goes BACK along its course (more than 5 cm) and the time it stands still (no
change for 150 ms or more), with the size of its largest forward step against the median one. The numbers
track_replay.cpp gives for a capture, taken from the running game. Read-only (3663)."""
import math
import sys
import time

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/livetest')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
r = Reader(int(sys.argv[1]))
key = int(sys.argv[2], 16)
secs = float(sys.argv[3])
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
ts = r.ptr(r.ptr(a + 0x68) + 0x1A0)
frames = []  # (time, x, z)
last = None
t0 = time.perf_counter()
while time.perf_counter() - t0 < secs:
    p = r.unpack(ts + 0xB4, '<3f')
    s = r.unpack(ts + 0xC0, '<hh')
    cur = (p[0] + s[0] * 1000.0, p[2] + s[1] * 1000.0)
    if cur != last:
        frames.append((time.perf_counter() - t0, cur[0], cur[1]))
        last = cur
if len(frames) < 20:
    raise SystemExit(f'{key:08X}: only {len(frames)} position changes in {secs:.0f} s (it does not move)')
# The course at each frame: from the frame about half a second before to the one about half a second after.
moving = back = 0
back_max = still_s = moved_s = 0.0
steps = []
j0 = j1 = 0
for i in range(1, len(frames)):
    ti, xi, zi = frames[i]
    while frames[j0][0] < ti - 0.5:
        j0 += 1
    while j1 + 1 < len(frames) and frames[j1][0] < ti + 0.5:
        j1 += 1
    span = frames[j1][0] - frames[j0][0]
    dx, dz = frames[j1][1] - frames[j0][1], frames[j1][2] - frames[j0][2]
    dist = math.hypot(dx, dz)
    if span < 0.3 or dist / span < 2.0:
        continue
    dt = ti - frames[i - 1][0]
    along = ((xi - frames[i - 1][1]) * dx + (zi - frames[i - 1][2]) * dz) / dist
    moving += 1
    moved_s += dt
    steps.append(along)
    if along < -0.05:
        back += 1
        back_max = max(back_max, -along)
    if dt >= 0.15:
        still_s += dt
if not moving:
    raise SystemExit(f'{key:08X}: {len(frames)} frames, never at 2 m/s or more')
steps.sort()
med = steps[len(steps) // 2]
print(f'{key:08X} on pid {sys.argv[1]}: {len(frames)} frames in {secs:.0f} s ({len(frames) / secs:.0f} a second), moving '
      f'{moved_s:.0f} s | went BACK in {back} frames ({back * 60 / max(moved_s, 1e-3):.1f} a minute, up to {back_max:.2f} m) | '
      f'stood still {still_s:.1f} s ({100 * still_s / max(moved_s, 1e-3):.0f}%) | forward step: median {med:.3f} m, largest '
      f'{steps[-1]:.2f} m ({steps[-1] / max(med, 1e-6):.1f} times the median)')
