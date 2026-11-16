"""shiphead.py pid : the heading of the copy's ship (the vehicle 0xB010000A): the direction its bow points to, in
degrees as face.py and caminfo.py count them (atan2(x, z): 0 = +z, 90 = +x), and its position. The bow is the
ship's -z axis (the wheel is at local z 1.85 and the stern deck further up in z). Read-only."""
import math
import os as _os
import sys

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace(chr(92), '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
r = Reader(int(sys.argv[1]))
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
t = r.ptr(mgr + 8)
n = r.unpack(t + 0x88, '<I')[0]
key = int(__import__("os").environ.get("CDMP_SHIP", "0xB010000A"), 16)
b = r.ptr(t + 0x98) + (key % n) * 0x100
a = 0
for i in range(min(r.unpack(b, '<I')[0], 31)):
    k, idx = r.unpack(b + 8 + i * 8, '<II')
    if k == key:
        a = r.ptr(r.ptr(r.ptr(t + 0xA0) + idx * 8) + 8)
if not a:
    raise SystemExit('no ship')
ts = r.ptr(r.ptr(a + 0x68) + 0x1A0)
x, y, z, w = r.unpack(ts + 0xA4, '<4f')
p = r.unpack(ts + 0xB4, '<3f')
s = r.unpack(ts + 0xC0, '<hh')
# the ship's -z axis in the world: rotate (0, 0, -1) by q
vx, vy, vz = 0.0, 0.0, -1.0
cx, cy, cz = y * vz - z * vy, z * vx - x * vz, x * vy - y * vx
ccx, ccy, ccz = y * cz - z * cy, z * cx - x * cz, x * cy - y * cx
fx, fz = vx + 2 * w * cx + 2 * ccx, vz + 2 * w * cz + 2 * ccz
print(f'{math.degrees(math.atan2(fx, fz)):+.0f} {p[0] + s[0] * 1000:.1f} {p[2] + s[1] * 1000:.1f}')
