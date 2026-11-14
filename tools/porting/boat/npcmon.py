"""npcmon.py host guest seconds [period] [root_key] : the actors that ride the host's ship without being part of it
(everything hanging at any depth from the vehicle, default 0xB010000A, that is neither a gimmick nor a vehicle: the
crew, a mount, a player), as each copy has them: on the host its parent and local position; on the guest whether it
is there, what it hangs from there, its pending request type (TS+0x5BC) and how far it is drawn from its place (the
guest's own copy of the host's parent composed with the host's local pose). Read-only (3663)."""
import math
import os as _os
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE + '/../livetest')
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
hp, gp, secs = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3])
period = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
ROOT = int(sys.argv[5], 16) if len(sys.argv) > 5 else 0xB010000A


def rot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    cx, cy, cz = y * vz - z * vy, z * vx - x * vz, x * vy - y * vx
    ccx, ccy, ccz = y * cz - z * cy, z * cx - x * cz, x * cy - y * cx
    return (vx + 2 * w * cx + 2 * ccx, vy + 2 * w * cy + 2 * ccy, vz + 2 * w * cz + 2 * ccz)


class Copy:
    def __init__(self, pid):
        self.r = r = Reader(pid)
        world = r.ptr(B + WORLD_SLOT)
        self.mgr = mgr = r.ptr(world + 0x30)
        t = r.ptr(mgr + 8)
        self.n = r.unpack(t + 0x88, '<I')[0]
        self.buckets = r.ptr(t + 0x98)
        self.nodes = r.ptr(t + 0xA0)
        self.user = r.unpack(r.ptr(mgr + 0x58) + 0x60, '<I')[0] if r.ptr(mgr + 0x58) else 0

    def actor(self, key):
        r = self.r
        b = self.buckets + (key % self.n) * 0x100
        c = r.unpack(b, '<I')[0]
        for i in range(min(c, 31)):
            k, idx = r.unpack(b + 8 + i * 8, '<II')
            if k == key:
                return r.ptr(r.ptr(self.nodes + idx * 8) + 8)
        return 0

    def all(self):
        r = self.r
        out = {}
        for bi in range(self.n):
            base = self.buckets + bi * 0x100
            try:
                c = r.unpack(base, '<I')[0]
                for i in range(min(c, 31)):
                    k, idx = r.unpack(base + 8 + i * 8, '<II')
                    node = r.ptr(self.nodes + idx * 8)
                    a = r.ptr(node + 8) if node else 0
                    if a:
                        out[k] = a
            except Exception:
                pass
        return out

    def parts(self, a):
        """(type, controller, TransformSync, hierarchy parent, attach type) of an actor."""
        r = self.r
        comps = r.ptr(a + 0x68)
        info = r.ptr(a + 0x88)
        ts = r.ptr(comps + 0x1A0) if comps else 0
        h = r.ptr(comps + 0x1B8) if comps else 0
        return (r.unpack(info + 1, '<B')[0] if info else -1, r.unpack(a + 0x90, '<I')[0], ts,
                r.unpack(h + 0x1C, '<I')[0] if h else 0, r.unpack(h + 0x18, '<B')[0] if h else 0)

    def local(self, key):
        a = self.actor(key)
        if not a:
            return None
        typ, ctl, ts, par, att = self.parts(a)
        if not ts:
            return None
        r = self.r
        p = r.unpack(ts + 0xB4, '<3f')
        s = r.unpack(ts + 0xC0, '<hh')
        q = r.unpack(ts + 0xA4, '<4f')
        return par, (p[0] + s[0] * 1000, p[1], p[2] + s[1] * 1000), q, ts

    def world(self, key, depth=0):
        l = self.local(key)
        if not l:
            return None
        par, p, q, _ = l
        if not par or depth > 6:
            return p, q
        up = self.world(par, depth + 1)
        if not up:
            return None
        pp, pq = up
        w = rot(pq, p)
        x, y, z, ww = pq
        a, b, c, d = q
        qq = (ww * a + x * d + y * c - z * b, ww * b - x * c + y * d + z * a, ww * c + x * b - y * a + z * d,
              ww * d - x * a - y * b - z * c)
        return (pp[0] + w[0], pp[1] + w[1], pp[2] + w[2]), qq


H, G = Copy(hp), Copy(gp)


def riders():
    """The host's actors under ROOT that are not ship parts: key -> (type, controller)."""
    acts = H.all()
    info = {}
    for k, a in acts.items():
        try:
            info[k] = H.parts(a)
        except Exception:
            pass
    out = {}
    for k, (typ, ctl, ts, par, att) in info.items():
        if typ in (5, 7) or not par:
            continue
        up, depth = par, 0
        while up and up != ROOT and depth < 8:
            up = info.get(up, (0, 0, 0, 0, 0))[3]
            depth += 1
        if up == ROOT:
            out[k] = (typ, ctl, att)
    return out


crew = riders()
print(f'host user {H.user:#x}, guest user {G.user:#x}; riders of {ROOT:#x} on the host: '
      + (', '.join(f'{k:08X} type {t} ctl {c:08X} attach {a}' for k, (t, c, a) in sorted(crew.items())) or 'none'))
t0 = time.time()
prev = None
last_scan = t0
while time.time() - t0 < secs:
    now = time.time()
    if now - last_scan > 10:
        try:
            crew = riders()
        except Exception:
            pass
        last_scan = now
    try:
        ship = H.world(ROOT)[0]
        gship = G.world(ROOT)
        sp = math.hypot(ship[0] - prev[0], ship[2] - prev[2]) / period if prev else 0
        prev = ship
        line = f'{now - t0:6.1f} ship {sp:4.1f} m/s (guest {math.dist(ship, gship[0]) if gship else -1:4.1f} m off)'
        for k in sorted(crew):
            hl = H.local(k)
            if not hl:
                line += f' | {k:08X} gone on the host'
                continue
            hpar, hloc, hq, _ = hl
            gl = G.local(k)
            if not gl:
                line += f' | {k:08X} on {hpar & 0xFFFF:04X} NOT on the guest'
                continue
            gpar, gloc, gq, gts = gl
            base = G.world(hpar)
            drawn = G.world(k)
            req = G.r.unpack(gts + 0x5BC, '<B')[0]
            if base and drawn:
                w = rot(base[1], hloc)
                want = (base[0][0] + w[0], base[0][1] + w[1], base[0][2] + w[2])
                err = math.dist(want, drawn[0])
                line += (f' | {k:08X} on {hpar & 0xFFFF:04X} (guest: on {gpar & 0xFFFF:04X}, req {req}) '
                         f'{err:6.2f} m off (dy {drawn[0][1] - want[1]:+.2f})')
            else:
                line += f' | {k:08X} on {hpar & 0xFFFF:04X}: the guest has no {hpar:08X}'
        print(line, flush=True)
    except Exception as e:  # a copy mid-load, an actor going away
        print('err', e, flush=True)
    time.sleep(period)
