"""statseries.py pid seconds id [id ...] : the current value of the given stat entries of the copy's own character,
four times a second, one line each time one of them changed (with the character's position). To see which entry
follows what the character does (a sprint, a swim). Read-only."""
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
ids = [int(v) for v in sys.argv[3:]]


def read():
    world = r.ptr(B + WORLD_SLOT)
    mgr = r.ptr(world + 0x30)
    actor = r.ptr(mgr + 0x50)
    comps = r.ptr(actor + 0x68)
    status = r.ptr(comps + 0x20)
    container = r.ptr(status + 0x18)
    base = r.ptr(container + 0x58)
    count = r.unpack(container + 0x60, '<I')[0]
    out = {}
    for i in range(min(count, 256)):
        sid, cur, _regen, _mx = r.unpack(base + i * 0x90, '<Qqqq')
        if sid in ids:
            out[sid] = cur / 1000.0
    ts = r.ptr(comps + 0x1A0)
    p = r.unpack(ts + 0xB4, '<3f')
    return out, p


t0 = time.time()
last = None
while time.time() - t0 < secs:
    try:
        cur, p = read()
    except Exception:
        time.sleep(0.25)
        continue
    if cur != last:
        print(f'{time.time() - t0:6.1f} ' + ' '.join(f'{i}={cur.get(i, float("nan")):7.1f}' for i in ids) +
              f' | local ({p[0]:.1f}, {p[1]:.1f}, {p[2]:.1f})', flush=True)
        last = cur
    time.sleep(0.25)
