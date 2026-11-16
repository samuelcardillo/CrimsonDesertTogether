"""aboard.py pid [seconds [period]] : is the copy's own character on the ship (the vehicle CDMP_SHIP, 0xB010000A), with
or without a hierarchy parent? Its world position (composed through its parents) in the ship's frame on that same
copy: aboard when it is within the hull (|x| < 6, -26 < z < 16) and between 0.5 and 9 m above the ship's origin.
One line a look ("aboard" / "NOT aboard", the place, the parent); with seconds, a look every `period` (3) and a last
line with the count. For a passenger the mod holds on deck while the game has taken its part away (16-nov): the
parent alone no longer says whether it is on the ship. Read-only (3663)."""
import os as _os
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
SHIP = int(_os.environ.get('CDMP_SHIP', '0xB010000A'), 16)
r = Reader(int(sys.argv[1]))
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 0
period = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
t = r.ptr(mgr + 8)
n = r.unpack(t + 0x88, '<I')[0]


def rot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    cx, cy, cz = y * vz - z * vy, z * vx - x * vz, x * vy - y * vx
    ccx, ccy, ccz = y * cz - z * cy, z * cx - x * cz, x * cy - y * cx
    return (vx + 2 * w * cx + 2 * ccx, vy + 2 * w * cy + 2 * ccy, vz + 2 * w * cz + 2 * ccz)


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def actor(key):
    b = r.ptr(t + 0x98) + (key % n) * 0x100
    for i in range(min(r.unpack(b, '<I')[0], 31)):
        k, idx = r.unpack(b + 8 + i * 8, '<II')
        if k == key:
            return r.ptr(r.ptr(r.ptr(t + 0xA0) + idx * 8) + 8)
    return 0


def pose(a, depth=0):
    comps = r.ptr(a + 0x68)
    ts = r.ptr(comps + 0x1A0)
    h = r.ptr(comps + 0x1B8)
    par = r.unpack(h + 0x1C, '<I')[0] if h else 0
    p = r.unpack(ts + 0xB4, '<3f')
    s = r.unpack(ts + 0xC0, '<hh')
    q = r.unpack(ts + 0xA4, '<4f')
    p = (p[0] + s[0] * 1000.0, p[1], p[2] + s[1] * 1000.0)
    if not par or depth > 5:
        return par, p, q
    pa = actor(par)
    if not pa:
        return par, p, q
    _, pp, pq = pose(pa, depth + 1)
    w = rot(pq, p)
    return par, (pp[0] + w[0], pp[1] + w[1], pp[2] + w[2]), qmul(pq, q)


def look():
    own = r.ptr(mgr + 0x50)
    par, p, _ = pose(own)
    sa = actor(SHIP)
    if not sa:
        return False, f'the ship 0x{SHIP:08X} is not in this copy'
    _, sp, sq = pose(sa)
    d = (p[0] - sp[0], p[1] - sp[1], p[2] - sp[2])
    l = rot((-sq[0], -sq[1], -sq[2], sq[3]), d)
    ok = abs(l[0]) < 6 and -26 < l[2] < 16 and 0.5 < l[1] < 9
    return ok, f'at ({l[0]:.2f}, {l[1]:.2f}, {l[2]:.2f}) in the ship\'s frame, parent 0x{par:X}'


if not secs:
    ok, text = look()
    print(('aboard ' if ok else 'NOT aboard ') + text)
else:
    yes = total = free = 0
    t0 = time.time()
    last = ''
    while time.time() - t0 < secs:
        try:
            ok, text = look()
        except Exception as e:  # a read that failed: counted as a look that did not see it aboard
            ok, text = False, f'unreadable ({e})'
        total += 1
        yes += ok
        free += ok and text.endswith('parent 0x0')
        last = ('aboard ' if ok else 'NOT aboard ') + text
        if not ok:
            print(f'{time.time() - t0:5.1f} s {last}')
        time.sleep(period)
    print(f'aboard in {yes} of {total} looks over {secs:.0f} s ({free} of them without a parent: held by the mod); '
          f'last: {last}')
