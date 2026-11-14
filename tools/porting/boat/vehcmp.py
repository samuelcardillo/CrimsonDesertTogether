"""vehcmp.py host guest [min_parts] : every vehicle actor (type 5) on the host that at least min_parts (default 6)
gimmicks hang from directly (a ship), as each copy has it: its key, vtable, controller, where the host has it, and on
the guest whether it is there, how far its copy is from the host's, its last network pose (TS+0x550) against the
host's position, and its pending request type (TS+0x5BC). To see which ships the guest does not follow. Read-only."""
import collections
import math
import os as _os
import sys

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE + '/../livetest')
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
MIN = int(sys.argv[3]) if len(sys.argv) > 3 else 6


class Copy:
    def __init__(self, pid):
        self.r = r = Reader(pid)
        world = r.ptr(B + WORLD_SLOT)
        self.mgr = mgr = r.ptr(world + 0x30)
        t = r.ptr(mgr + 8)
        self.n = r.unpack(t + 0x88, '<I')[0]
        self.buckets = r.ptr(t + 0x98)
        self.nodes = r.ptr(t + 0xA0)
        self.user = r.unpack(r.ptr(mgr + 0x58) + 0x60, '<I')[0]
        own = r.ptr(mgr + 0x50)
        self.own = self.pos(own) if own else None

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

    def info(self, a):
        r = self.r
        comps = r.ptr(a + 0x68)
        inf = r.ptr(a + 0x88)
        h = r.ptr(comps + 0x1B8) if comps else 0
        return (r.unpack(inf + 1, '<B')[0] if inf else -1, r.unpack(a + 0x90, '<I')[0],
                r.unpack(h + 0x1C, '<I')[0] if h else 0, r.unpack(h + 0x18, '<B')[0] if h else 0, r.ptr(a) - B)

    def pos(self, a):
        r = self.r
        ts = r.ptr(r.ptr(a + 0x68) + 0x1A0)
        p = r.unpack(ts + 0xB4, '<3f')
        s = r.unpack(ts + 0xC0, '<hh')
        net = r.unpack(ts + 0x550, '<3f')
        return (p[0] + s[0] * 1000, p[1], p[2] + s[1] * 1000), net, r.unpack(ts + 0x5BC, '<B')[0]


H, G = Copy(int(sys.argv[1])), Copy(int(sys.argv[2]))
ha, ga = H.all(), G.all()
kids = collections.Counter()
info = {}
for k, a in ha.items():
    try:
        info[k] = H.info(a)
    except Exception:
        continue
    typ, ctl, par, att, vt = info[k]
    if typ == 7 and par:
        kids[par] += 1
print(f'host user {H.user:#x}, guest user {G.user:#x}; host character at ({H.own[0][0]:.0f}, {H.own[0][2]:.0f})'
      if H.own else 'no own character')
rows = []
for k, n in kids.items():
    if n < MIN or k not in info or info[k][0] != 5:
        continue
    typ, ctl, par, att, vt = info[k]
    hp, hnet, _ = H.pos(ha[k])
    d_host = math.dist(hp, H.own[0]) if H.own else -1
    if k in ga:
        gp, gnet, req = G.pos(ga[k])
        gi = G.info(ga[k])
        guest = (f'guest copy {math.dist(hp, gp):7.1f} m from the host\'s; its network pose {math.dist(hp, gnet):7.1f} m from it '
                 f'(req {req}, ctl {gi[1]:08X})')
    else:
        guest = 'NOT on the guest'
    rows.append((d_host, f'{k:08X} vt {vt:#x} ctl {ctl:08X} parent {par:08X} {n:2d} parts, {d_host:6.0f} m from the host\'s '
                         f'character at ({hp[0]:.0f}, {hp[2]:.0f}) | {guest}'))
for _, line in sorted(rows):
    print(line)
