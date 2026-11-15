"""statwatch.py pid seconds : which stat entries of the copy's own character CHANGE, and how far, over a stretch of
play (statdump.py's walk, sampled five times a second). Made to find the stamina entry on a new build: start it,
make the character sprint, and the entry whose current value drops and comes back is the one. Prints, for every
entry that changed, its id, maximum, lowest and highest value seen and how many samples differed from the one
before. Read-only."""
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


def entries():
    world = r.ptr(B + WORLD_SLOT)
    mgr = r.ptr(world + 0x30)
    actor = r.ptr(mgr + 0x50)
    comps = r.ptr(actor + 0x68)
    status = r.ptr(comps + 0x20)
    container = r.ptr(status + 0x18) if status else 0
    base = r.ptr(container + 0x58) if container else 0
    count = r.unpack(container + 0x60, '<I')[0] if container else 0
    if not base or not count or count > 256:
        return {}
    out = {}
    for i in range(count):
        sid, cur, _regen, mx = r.unpack(base + i * 0x90, '<Qqqq')
        out[sid] = (cur / 1000.0, mx / 1000.0)
    return out


seen = {}  # id -> [max, lowest, highest, changes, last]
t0 = time.time()
samples = 0
while time.time() - t0 < secs:
    try:
        cur = entries()
    except Exception:  # the character is between worlds
        cur = {}
    for sid, (c, m) in cur.items():
        s = seen.setdefault(sid, [m, c, c, 0, c])
        s[0] = m
        s[1] = min(s[1], c)
        s[2] = max(s[2], c)
        if c != s[4]:
            s[3] += 1
        s[4] = c
    samples += 1
    time.sleep(0.2)
print(f'{samples} samples in {secs:.0f} s, {len(seen)} entries')
for sid, (m, lo, hi, n, _last) in sorted(seen.items()):
    if n:
        print(f'  id {sid:<5d} max {m:9.1f}: from {lo:9.1f} to {hi:9.1f}, changed in {n} samples')
