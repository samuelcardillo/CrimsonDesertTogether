"""windcmp.py host_pid guest_pid [seconds [period]] : the wind and the scroll accumulators of the scene weather system
on both copies, side by side (3663): A+0x138 windSpeed, A+0x154 windDegree, A+0x158 oceanWindDegree, and X's own
words: +0x14C the angle that follows the wind (degrees), +0x144/+0x148 its direction, +0x13C/+0x140 the cloud scroll,
+0x154..+0x160 two more scrolls. X and A are taken from the last WeatherSync line of each copy's log (cdmp.host.log,
cdmp.guest.log). For "the sea washes over the ship on the guest" (16-nov): what drives the waves has to be the same
on both. Read-only."""
import os as _os
import re
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from live_reader_check import Reader  # noqa: E402

BIN = r'D:/SteamLibrary/steamapps/common/Crimson Desert/bin64'


def blocks(log):
    with open(_os.path.join(BIN, log), 'rb') as f:
        f.seek(0, 2)
        f.seek(max(0, f.tell() - 400000))
        text = f.read().decode('utf-8', 'replace')
    m = None
    for m in re.finditer(r'WeatherSync \[[^\]]*\]: tick \d+ .*?X (0x[0-9A-F]+) .*?holder (0x[0-9A-F]+) A (0x[0-9A-F]+) B (0x[0-9A-F]+)', text):
        pass
    if not m:
        raise SystemExit(f'no WeatherSync line in {log}')
    return int(m.group(1), 16), int(m.group(3), 16)


def look(r, x, a):
    f = lambda addr: r.unpack(addr, '<f')[0]
    return dict(windSpeed=f(a + 0x138), windDegree=f(a + 0x154), oceanWindDegree=f(a + 0x158), follow=f(x + 0x14C),
                dir=(f(x + 0x144), f(x + 0x148)), cloud=(f(x + 0x13C), f(x + 0x140)),
                s2=(f(x + 0x154), f(x + 0x158)), s3=(f(x + 0x15C), f(x + 0x160)), viewer=r.unpack(x + 0x31, '<B')[0])


def text(d):
    return (f"wind {d['windSpeed']:6.2f} m/s, degree {d['windDegree']:6.1f}, ocean degree {d['oceanWindDegree']:6.1f}, "
            f"follows at {d['follow']:6.1f} dir ({d['dir'][0]:+.3f}, {d['dir'][1]:+.3f}) | cloud scroll "
            f"({d['cloud'][0]:.1f}, {d['cloud'][1]:.1f}) scroll2 ({d['s2'][0]:.2f}, {d['s2'][1]:.2f}) scroll3 "
            f"({d['s3'][0]:.2f}, {d['s3'][1]:.2f}) viewer {d['viewer']}")


h, g = Reader(int(sys.argv[1])), Reader(int(sys.argv[2]))
hx, ha = blocks('cdmp.host.log')
gx, ga = blocks('cdmp.guest.log')
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 0
period = float(sys.argv[4]) if len(sys.argv) > 4 else 2.0
t0 = time.time()
while True:
    print(f'{time.time() - t0:5.1f} s host : {text(look(h, hx, ha))}')
    print(f'        guest: {text(look(g, gx, ga))}')
    if time.time() - t0 >= secs:
        break
    time.sleep(period)
