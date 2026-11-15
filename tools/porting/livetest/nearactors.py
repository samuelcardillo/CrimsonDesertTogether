"""nearactors.py pid [radius_m] [types] : the actors of a copy's table near its own character, nearest first: key,
type (3 hostile NPC, 4 NPC, 5 vehicle, 6 fish...; the byte at info+1), distance, world position and, when the actor
has a status component like the player's, its health (stat entry 0: current / maximum). types: comma-separated
(default 3: what a fight is looked for with). For finding an enemy to walk to and for watching its health from both
copies (nearactors.py host 60 3; nearactors.py guest 60 3). Read-only (3663)."""
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
radius = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
types = {int(v) for v in (sys.argv[3] if len(sys.argv) > 3 else '3').split(',')}


def rot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    cx, cy, cz = y * vz - z * vy, z * vx - x * vz, x * vy - y * vx
    ccx, ccy, ccz = y * cz - z * cy, z * cx - x * cz, x * cy - y * cx
    return (vx + 2 * w * cx + 2 * ccx, vy + 2 * w * cy + 2 * ccy, vz + 2 * w * cz + 2 * ccz)


world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
t = r.ptr(mgr + 8)
n = r.unpack(t + 0x88, '<I')[0]
buckets, nodes = r.ptr(t + 0x98), r.ptr(t + 0xA0)
own = r.ptr(mgr + 0x50)
actors = {}
for bi in range(n):
    base = buckets + bi * 0x100
    try:
        c = r.unpack(base, '<I')[0]
        for i in range(min(c, 31)):
            k, idx = r.unpack(base + 8 + i * 8, '<II')
            a = r.ptr(r.ptr(nodes + idx * 8) + 8)
            if a and r.unpack(a + 0x60, '<I')[0] == k:
                actors[k] = a
    except Exception:
        continue


def pose(a):
    comps = r.ptr(a + 0x68)
    ts, h = r.ptr(comps + 0x1A0), r.ptr(comps + 0x1B8)
    par = r.unpack(h + 0x1C, '<I')[0] if h else 0
    p = r.unpack(ts + 0xB4, '<3f')
    s = r.unpack(ts + 0xC0, '<hh')
    q = r.unpack(ts + 0xA4, '<4f')
    return par, (p[0] + s[0] * 1000, p[1], p[2] + s[1] * 1000), q


def world_pos(a, depth=0):
    par, p, _q = pose(a)
    if not par or par not in actors or depth > 5:
        return p
    pp = world_pos(actors[par], depth + 1)
    _, _, pq = pose(actors[par])
    w = rot(pq, p)
    return (pp[0] + w[0], pp[1] + w[1], pp[2] + w[2])


def health(a):
    try:
        comps = r.ptr(a + 0x68)
        status = r.ptr(comps + 0x20)
        container = r.ptr(status + 0x18) if status else 0
        base = r.ptr(container + 0x58) if container else 0
        count = r.unpack(container + 0x60, '<I')[0] if container else 0
        if not base or not count or count > 256:
            return None
        for i in range(count):
            sid, cur, _regen, mx = r.unpack(base + i * 0x90, '<Qqqq')
            if sid == 0 and 0 <= cur <= mx and 0 < mx < 10 ** 12:
                return cur / 1000.0, mx / 1000.0
    except Exception:
        pass
    return None


me = world_pos(own)
rows = []
for k, a in actors.items():
    if a == own:
        continue
    try:
        info = r.ptr(a + 0x88)
        ty = r.unpack(info + 1, '<B')[0] if info else 255
        if ty not in types:
            continue
        p = world_pos(a)
        d = math.dist(me, p)
        if d <= radius:
            rows.append((d, k, ty, p, health(a)))
    except Exception:
        continue
rows.sort()
print(f'own character at ({me[0]:.1f}, {me[1]:.1f}, {me[2]:.1f}); {len(rows)} actor(s) of type(s) {sorted(types)} within {radius:.0f} m')
for d, k, ty, p, hp in rows[:40]:
    hp_s = f'health {hp[0]:.0f}/{hp[1]:.0f}' if hp else 'no health entry'
    print(f'  {k:08X} type {ty} {d:6.1f} m at ({p[0]:.1f}, {p[1]:.1f}, {p[2]:.1f}) {hp_s}')
