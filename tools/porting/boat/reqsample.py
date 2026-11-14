"""reqsample.py pid seconds [actor_key] : samples as fast as it can the pending TransformSync request (TS+0x5A0..0x5E0)
of the copy's own character (or of the actor with that key) and prints each DIFFERENT pending request it catches (type at
+0x5BC other than 15 = none), next to the actor's hierarchy parent, its local position (+0xB4) and its parent's world
position (+0xEC). To see in which frame a parented actor's position requests are given: its parent's, or the
world's. Read-only (3663)."""
import struct
import sys
import time

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/livetest')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
r = Reader(int(sys.argv[1]))
secs = float(sys.argv[2])
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
if len(sys.argv) > 3:
    key = int(sys.argv[3], 16)
    t = r.ptr(mgr + 8)
    n = r.unpack(t + 0x88, '<I')[0]
    b = r.ptr(t + 0x98) + (key % n) * 0x100
    a = 0
    for i in range(min(r.unpack(b, '<I')[0], 31)):
        k, idx = r.unpack(b + 8 + i * 8, '<II')
        if k == key:
            a = r.ptr(r.ptr(r.ptr(t + 0xA0) + idx * 8) + 8)
else:
    a = r.ptr(mgr + 0x50)
comps = r.ptr(a + 0x68)
ts = r.ptr(comps + 0x1A0)
h = r.ptr(comps + 0x1B8)
seen = set()
t0 = time.time()
reads = caught = 0
while time.time() - t0 < secs:
    raw = r.read(ts + 0x5A0, 0x40)
    reads += 1
    typ = raw[0x1C]
    if typ == 15:
        continue
    caught += 1
    sig = raw[0x08:0x20]
    if sig in seen or len(seen) > 40:
        continue
    seen.add(sig)
    par = r.unpack(h + 0x1C, '<I')[0] if h else 0
    lp = r.unpack(ts + 0xB4, '<3f')
    ls = r.unpack(ts + 0xC0, '<2h')
    pw = r.unpack(ts + 0xEC, '<3f')
    ps = r.unpack(ts + 0xF8, '<2h')
    f = struct.unpack_from('<3f', raw, 0x0C)
    s = struct.unpack_from('<2h', raw, 0x18)
    print(f'{time.time() - t0:5.2f} type {typ} request ({f[0]:.2f},{f[1]:.2f},{f[2]:.2f}) sec {s} | parent {par:08X} '
          f'local ({lp[0]:.2f},{lp[1]:.2f},{lp[2]:.2f}) sec {ls} | parent world ({pw[0]:.2f},{pw[1]:.2f},{pw[2]:.2f}) '
          f'sec {ps} | raw {raw[:0x24].hex(" ")}', flush=True)
print(f'{reads} reads, {caught} with a pending request, {len(seen)} different')
