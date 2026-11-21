"""vehwatch.py host guest seconds [period] [min_parts] : every `period` seconds, for each ship of the host's world (a
type-5 vehicle actor with at least min_parts gimmicks hanging from it directly): how fast the host has it moving, how
far the guest's copy is from the host's, and how far the guest's last network pose of it (TS+0x550) is. 'ABSENT': the
guest has no such actor. One line per sample. Read-only (3663)."""
import collections
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
period = float(sys.argv[4]) if len(sys.argv) > 4 else 3.0
MIN = int(sys.argv[5]) if len(sys.argv) > 5 else 6


class Copy:
    def __init__(self, pid):
        self.r = Reader(pid)

    def table(self):
        r = self.r
        world = r.ptr(B + WORLD_SLOT)
        mgr = r.ptr(world + 0x30)
        t = r.ptr(mgr + 8)
        return r.unpack(t + 0x88, '<I')[0], r.ptr(t + 0x98), r.ptr(t + 0xA0)

    def all(self):
        r = self.r
        n, buckets, nodes = self.table()
        out = {}
        for bi in range(n):
            base = buckets + bi * 0x100
            try:
                c = r.unpack(base, '<I')[0]
                for i in range(min(c, 31)):
                    k, idx = r.unpack(base + 8 + i * 8, '<II')
                    if (k >> 28) != 0xB:
                        continue
                    node = r.ptr(nodes + idx * 8)
                    a = r.ptr(node + 8) if node else 0
                    if a:
                        out[k] = a
            except Exception:
                pass
        return out

    def actor(self, key):
        r = self.r
        n, buckets, nodes = self.table()
        b = buckets + (key % n) * 0x100
        for i in range(min(r.unpack(b, '<I')[0], 31)):
            k, idx = r.unpack(b + 8 + i * 8, '<II')
            if k == key:
                return r.ptr(r.ptr(nodes + idx * 8) + 8)
        return 0

    def kind(self, a):
        r = self.r
        comps = r.ptr(a + 0x68)
        inf = r.ptr(a + 0x88)
        h = r.ptr(comps + 0x1B8) if comps else 0
        return (r.unpack(inf + 1, '<B')[0] if inf else -1, r.unpack(h + 0x1C, '<I')[0] if h else 0, r.ptr(a) - B,
                r.unpack(a + 0x90, '<I')[0])

    def pos(self, a):
        r = self.r
        ts = r.ptr(r.ptr(a + 0x68) + 0x1A0)
        p = r.unpack(ts + 0xB4, '<3f')
        s = r.unpack(ts + 0xC0, '<hh')
        return (p[0] + s[0] * 1000, p[1], p[2] + s[1] * 1000), r.unpack(ts + 0x550, '<3f'), r.unpack(ts + 0x5BC, '<B')[0]


H, G = Copy(hp), Copy(gp)
ships = {}
last_scan = 0.0
prev = {}
t0 = time.time()
while time.time() - t0 < secs:
    now = time.time()
    try:
        if now - last_scan > 15:
            last_scan = now
            acts = H.all()
            kids = collections.Counter()
            kinds = {}
            for k, a in acts.items():
                try:
                    kinds[k] = H.kind(a)
                except Exception:
                    continue
                if kinds[k][0] == 7 and kinds[k][1]:
                    kids[kinds[k][1]] += 1
            ships = {k: kinds[k] for k, n in kids.items() if n >= MIN and k in kinds and kinds[k][0] == 5}
        line = f'{time.strftime("%H:%M:%S")}'
        for k, (typ, par, vt, ctl) in sorted(ships.items()):
            a = H.actor(k)
            if not a:
                line += f' | {k:08X} gone'
                continue
            p, _, _ = H.pos(a)
            sp = math.hypot(p[0] - prev[k][0][0], p[2] - prev[k][0][2]) / (now - prev[k][1]) if k in prev else 0.0
            prev[k] = (p, now)
            ga = G.actor(k)
            if not ga:
                line += f' | {k:08X} {"player" if vt == 0x59961d0 else "NPC"} {sp:4.1f} m/s at ({p[0]:.0f},{p[2]:.0f}) ABSENT on the guest'
                continue
            gpos, gnet, req = G.pos(ga)
            line += (f' | {k:08X} {"player" if vt == 0x59961d0 else "NPC"} {sp:4.1f} m/s at ({p[0]:.0f},{p[2]:.0f}) guest copy '
                     f'{math.dist(p, gpos):6.1f} m off, its network pose {math.dist(p, gnet):6.1f} m off, req {req}')
        print(line, flush=True)
    except Exception as e:
        print(time.strftime('%H:%M:%S'), 'err', e, flush=True)
    time.sleep(period)
