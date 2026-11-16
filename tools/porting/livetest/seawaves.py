"""seawaves.py host_pid guest_pid [seconds [period]] : the twelve wave trains of the swell on both copies (1.0.0.3663)
and how far apart they are. Each copy's current frame is the first 0x180 bytes of the static buffer exe+0x75AD350
("OceanWaveConstantBuffer": 12 records of 0x20 bytes: phase +0, strength +4, direction +8, origin x/z +0x10/+0x14),
and the wind's strength for the swell is at +0x600. Without sea_sync every train differs (its phase, its direction,
its origin); with it the guest's are the host's, a frame or two behind. Read-only.

The phase difference is given as an angle (0..3.14) and, next to it, as the time the slower copy is behind at that
train's speed (a train of 21 m turns 2.27 rad/s: one frame of a copy running at 5 fps is 0.45 rad)."""
import math
import struct
import sys
import time

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from live_reader_check import Reader  # noqa: E402

BASE = 0x140000000
BUF = 0x75AD350
LENGTHS = (280, 210, 165, 130, 100, 80, 62, 48, 36, 30, 25, 21)
MULT = (1.5, 1.9, 2.3, 2.7, 3.1, 3.5, 3.8, 4.1, 4.4, 4.7, 5.0, 5.3)
SPEED = [math.sqrt(2 * math.pi * 9.8 / l) * m * 0.25 for l, m in zip(LENGTHS, MULT)]  # rad/s at full frame rate


def trains(r):
    raw = r.read(BASE + BUF, 0x180)
    wind = struct.unpack('<f', r.read(BASE + BUF + 0x600, 4))[0]
    out = []
    for i in range(12):
        ph, fade, direction, _, ox, oz = struct.unpack_from('<6f', raw, i * 0x20)
        out.append((ph, fade, direction, ox, oz))
    return out, wind


def angle(a, b):
    d = abs(a - b) % (2 * math.pi)
    return min(d, 2 * math.pi - d)


h, g = Reader(int(sys.argv[1])), Reader(int(sys.argv[2]))
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 0
period = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
t0 = time.time()
worst = [0.0] * 12
same_place = 0
looks = 0
while True:
    ht, hw = trains(h)
    gt, gw = trains(g)
    looks += 1
    line = []
    all_same = True
    for i in range(12):
        dp = angle(ht[i][0], gt[i][0])
        dd = abs(ht[i][2] - gt[i][2])
        do = math.hypot(ht[i][3] - gt[i][3], ht[i][4] - gt[i][4])
        worst[i] = max(worst[i], dp)
        if dd > 0.01 or do > 0.5:
            all_same = False
        line.append((dp, dd, do))
    same_place += all_same
    if secs == 0 or looks == 1:
        print(f'wind for the swell: host {hw:.4f} guest {gw:.4f}')
        print('  train  length  host: phase strength direction origin            | guest: phase strength direction origin'
              '            | apart: phase (as time)  direction  origin')
        for i in range(12):
            a, b, (dp, dd, do) = ht[i], gt[i], line[i]
            print(f'  {i:5d}  {LENGTHS[i]:4d} m  {a[0]:8.2f} {a[1]:5.2f} {a[2]:7.1f} ({a[3]:8.0f},{a[4]:8.0f}) | '
                  f'{b[0]:8.2f} {b[1]:5.2f} {b[2]:7.1f} ({b[3]:8.0f},{b[4]:8.0f}) | {dp:5.2f} rad ({dp / SPEED[i]:5.2f} s) '
                  f'{dd:6.1f} deg {do:7.0f} m')
    else:
        print(f'{time.time() - t0:6.1f} s  phase apart (rad): ' + ' '.join(f'{x[0]:4.2f}' for x in line)
              + f' | direction and origin {"the same" if all_same else "DIFFERENT"} | wind {hw:.4f} / {gw:.4f}')
    if time.time() - t0 >= secs:
        break
    time.sleep(period)
if secs:
    print(f'{looks} looks: direction and origin the same in {same_place}; the largest phase difference per train (rad): '
          + ' '.join(f'{x:4.2f}' for x in worst))
    print('as time at each train\'s speed (s): ' + ' '.join(f'{x / s:4.2f}' for x, s in zip(worst, SPEED)))
