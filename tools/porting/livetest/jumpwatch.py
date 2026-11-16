"""jumpwatch.py pid seconds : the copy's own character for that long, a hundred looks a second: how far its height
(local to its parent while it has one, world without) rose above where it began, for how long it had no parent, and
which parents it had. One line. To tell a jump that happened from a key press that did nothing (jumptest.sh).
Read-only (3663)."""
import os as _os
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
r = Reader(int(sys.argv[1]))
secs = float(sys.argv[2])
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
a = r.ptr(mgr + 0x50)
comps = r.ptr(a + 0x68)
ts = r.ptr(comps + 0x1A0)
h = r.ptr(comps + 0x1B8)
y0 = None
rise = 0.0
free = 0.0
parents = []
t0 = last = time.perf_counter()
while time.perf_counter() - t0 < secs:
    try:
        y = r.unpack(ts + 0xB4, '<fff')[1]
        par = r.unpack(h + 0x1C, '<I')[0]
    except Exception:
        time.sleep(0.01)
        continue
    now = time.perf_counter()
    if par not in parents:
        parents.append(par)
        y0 = None  # a new parent: another frame of reference for the height
    if y0 is None:
        y0 = y
    rise = max(rise, y - y0)
    if par == 0:
        free += now - last
    last = now
    time.sleep(0.01)
print(f'rose {rise:.2f} m, {free * 1000:.0f} ms without a parent, parents ' + ' -> '.join(f'0x{p:X}' for p in parents))
