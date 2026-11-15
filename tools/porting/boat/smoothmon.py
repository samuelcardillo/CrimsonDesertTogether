"""smoothmon.py pid key seconds [min_speed] : how smoothly ONE copy moves an actor (a ship's vehicle actor on the guest): samples
its TransformSync position about a thousand times a second, takes every change as a frame, and for the time it moves at 2 m/s or
more counts the frames in which it goes BACK along its course (more than 5 cm) and the time it stands still (no
change for 150 ms or more), with the size of its largest forward step against the median one. The numbers
track_replay.cpp gives for a capture, taken from the running game. And how EVEN the motion is: the distance covered in
each quarter of a second against the average of the 1.25 s around it (1 = steady; a real ship changes speed far more
slowly than that): the standard deviation and the extremes. min_speed (2 m/s): what counts as moving; with two
copies on one PC the copy behind runs slow and a ship in first gear does about 1 m/s there: use 0.5. Read-only (3663)."""
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
MIN = float(sys.argv[4]) if len(sys.argv) > 4 else 2.0
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
    # About a thousand looks a second. Reading without a pause slows the game it reads: on 15-nov two of these at
    # once, one on each copy, took both copies to 5-6 frames a second, the one in front included.
    time.sleep(0.0007)
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
    if span < 0.3 or dist / span < MIN:
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
    raise SystemExit(f'{key:08X}: {len(frames)} frames, never at {MIN:g} m/s or more')
steps.sort()
med = steps[len(steps) // 2]
# Evenness: the path length in quarter-second windows, each against the mean of the five windows around it.
t_end = frames[-1][0]
nwin = int(t_end / 0.25)
dist_in = [0.0] * nwin
for i in range(1, len(frames)):
    w = int(frames[i][0] / 0.25)
    if w < nwin:
        dist_in[w] += math.hypot(frames[i][1] - frames[i - 1][1], frames[i][2] - frames[i - 1][2])
ratios = []
for w in range(2, nwin - 2):
    around = sum(dist_in[w - 2:w + 3]) / 5
    if around / 0.25 >= MIN:
        ratios.append(dist_in[w] / around)
even = ''
if len(ratios) >= 8:
    mean = sum(ratios) / len(ratios)
    sd = math.sqrt(sum((v - mean) ** 2 for v in ratios) / len(ratios))
    even = (f' | evenness over {len(ratios)} quarter-seconds: +-{100 * sd:.0f}% (standard deviation), from '
            f'{min(ratios):.2f} to {max(ratios):.2f} times the speed around it')
print(f'{key:08X} on pid {sys.argv[1]}: {len(frames)} frames in {secs:.0f} s ({len(frames) / secs:.0f} a second), moving '
      f'{moved_s:.0f} s | went BACK in {back} frames ({back * 60 / max(moved_s, 1e-3):.1f} a minute, up to {back_max:.2f} m) | '
      f'stood still {still_s:.1f} s ({100 * still_s / max(moved_s, 1e-3):.0f}%) | forward step: median {med:.3f} m, largest '
      f'{steps[-1]:.2f} m ({steps[-1] / max(med, 1e-6):.1f} times the median){even}')
