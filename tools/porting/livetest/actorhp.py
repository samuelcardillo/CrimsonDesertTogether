"""actorhp.py pid key [key ...] : the health of actors by key in one copy (stat entry 0 of their status component:
current / maximum), or 'absent' when the copy has no such actor, with the distance to the copy's own character and
the actor's world x and z. One line. To watch an enemy's health from both copies while one player hits it
(actorhp.py host KEY; actorhp.py guest KEY). Read-only (3663)."""
import math
import os as _os
import sys

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
r = Reader(int(sys.argv[1]))
keys = [int(v, 16) for v in sys.argv[2:]]
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
t = r.ptr(mgr + 8)
n = r.unpack(t + 0x88, '<I')[0]
buckets, nodes = r.ptr(t + 0x98), r.ptr(t + 0xA0)
own = r.ptr(mgr + 0x50)


def actor(key):
    b = buckets + (key % n) * 0x100
    for i in range(min(r.unpack(b, '<I')[0], 31)):
        k, idx = r.unpack(b + 8 + i * 8, '<II')
        if k == key:
            return r.ptr(r.ptr(nodes + idx * 8) + 8)
    return 0


def pos(a):
    ts = r.ptr(r.ptr(a + 0x68) + 0x1A0)
    p = r.unpack(ts + 0xB4, '<3f')
    s = r.unpack(ts + 0xC0, '<hh')
    return (p[0] + s[0] * 1000, p[1], p[2] + s[1] * 1000)


def health(a):
    comps = r.ptr(a + 0x68)
    status = r.ptr(comps + 0x20)
    container = r.ptr(status + 0x18) if status else 0
    base = r.ptr(container + 0x58) if container else 0
    count = r.unpack(container + 0x60, '<I')[0] if container else 0
    if not base or not count or count > 256:
        return None
    for i in range(count):
        sid, cur, _regen, mx = r.unpack(base + i * 0x90, '<Qqqq')
        if sid == 0:
            return cur / 1000.0, mx / 1000.0
    return None


me = pos(own)
out = []
for k in keys:
    try:
        a = actor(k)
        if not a:
            out.append(f'{k:08X} absent')
            continue
        hp = health(a)
        at = pos(a)
        d = math.dist(me, at)
        where = f'at {d:.1f} m ({at[0]:.1f}, {at[2]:.1f})'
        out.append(f'{k:08X} {hp[0]:.0f}/{hp[1]:.0f} {where}' if hp else f'{k:08X} no health entry {where}')
    except Exception as e:
        out.append(f'{k:08X} unreadable ({e})')
own_hp = health(own)
print(' | '.join(out) + (f' | own {own_hp[0]:.0f}/{own_hp[1]:.0f}' if own_hp else ''))
