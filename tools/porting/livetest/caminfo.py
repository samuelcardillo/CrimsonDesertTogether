"""caminfo.py pid : the copy's camera against its own character: where the camera is, how far behind the character
(horizontally) and above it, and its pitch (negative: looking down). Read-only."""
import math
import os as _os
import sys

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace(chr(92), '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT, CAMERA_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
r = Reader(int(sys.argv[1]))
c = r.ptr(B + CAMERA_SLOT)
blk = r.ptr(c + 0x430)
px, py, pz = r.unpack(blk + 0x80, '<fff')
fx, fy, fz = r.unpack(blk + 0x90, '<fff')
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
actor = r.ptr(mgr + 0x50)
comps = r.ptr(actor + 0x68)
control = r.ptr(comps + 0x40)
inner = r.ptr(control + 0x140)
physics = r.ptr(inner + 0x2D0)
p = r.unpack(physics + 0x90, '<3f')
o = r.unpack(B + 0x7237470, '<3f')
cx, cy, cz = p[0] + o[0], p[1] + o[1], p[2] + o[2]
print(f'camera ({px:.1f}, {py:.1f}, {pz:.1f}) character ({cx:.1f}, {cy:.1f}, {cz:.1f}): {math.hypot(px - cx, pz - cz):.1f} m '
      f'away horizontally, {py - cy:+.1f} m above; pitch {math.degrees(math.asin(max(-1, min(1, fy)))):+.0f} deg, '
      f'heading {math.degrees(math.atan2(fx, fz)):+.0f} deg')
