"""shipsails.py host_pid guest_pid : every ship's sail on both copies (3663). On the host: each vehicle that looks like
a ship (six or more gimmicks hang directly from it and one of them carries others: the rule of gimmick_relay.cpp)
and, of the leaf gimmicks that hang directly from it, the ones whose branch state (+0x270 of the gimmick component)
is one of the sail's (furled, first, second, third gear or a change between them). For each: the ship's key,
controller and distance from each player, the sail's state on the host, and on the guest: its state there, or why
there is none (the guest has no such actor; its copy of the ship has fewer parts). "the NPC boats don't show their
sail status" (16-nov) is a line that says DIFFERENT. Read-only."""
import math
import os as _os
import sys

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
NAMES = {'89746c86': 'furled', '4dd95dbe': 'first gear', 'e480ff9b': 'second gear', '3f7594b0': 'third gear',
         '7d8452b9': 'going to first gear', 'a0f2a008': 'going to second gear', 'ea7cd220': 'going to third gear',
         '0795c60c': 'changing', 'f5af9c8c': 'changing', 'b6bcab80': 'changing'}


class Copy:
    def __init__(self, pid):
        self.r = r = Reader(pid)
        world = r.ptr(B + WORLD_SLOT)
        mgr = r.ptr(world + 0x30)
        t = r.ptr(mgr + 8)
        self.n = r.unpack(t + 0x88, '<I')[0]
        self.buckets, self.nodes = r.ptr(t + 0x98), r.ptr(t + 0xA0)
        own = r.ptr(mgr + 0x50)
        self.own_pos = self.world_pos(own) if own else None
        self.acts = {}
        for bi in range(self.n):
            base = self.buckets + bi * 0x100
            try:
                c = r.unpack(base, '<I')[0]
                for i in range(min(c, 31)):
                    k, idx = r.unpack(base + 8 + i * 8, '<II')
                    a = r.ptr(r.ptr(self.nodes + idx * 8) + 8)
                    if not a or r.unpack(a + 0x60, '<I')[0] != k:
                        continue
                    info = r.ptr(a + 0x88)
                    comps = r.ptr(a + 0x68)
                    h = r.ptr(comps + 0x1B8) if comps else 0
                    self.acts[k] = (a, r.unpack(info + 1, '<B')[0] if info else -1, r.unpack(a + 0x90, '<I')[0],
                                    r.unpack(h + 0x1C, '<I')[0] if h else 0)
            except Exception:
                continue
        self.children = {}
        for k, (a, typ, ctl, par) in self.acts.items():
            if par:
                self.children.setdefault(par, []).append(k)

    def world_pos(self, a):
        r = self.r
        comps = r.ptr(a + 0x68)
        ts = r.ptr(comps + 0x1A0) if comps else 0
        if not ts:
            return None
        p = r.unpack(ts + 0xB4, '<3f')
        s = r.unpack(ts + 0xC0, '<hh')
        return (p[0] + s[0] * 1000, p[1], p[2] + s[1] * 1000)

    def ship_like(self, v):
        direct = [k for k in self.children.get(v, []) if self.acts[k][1] == 7]
        return len(direct) >= 6 and any(k in self.children for k in direct), len(direct)

    def state(self, key):
        if key not in self.acts:
            return None
        r = self.r
        comps = r.ptr(self.acts[key][0] + 0x68)
        g = r.ptr(comps + 0x30) if comps else 0
        if not g:
            return None
        try:
            return r.read(g + 0x270, 4).hex()
        except Exception:
            return None


H, G = Copy(int(sys.argv[1])), Copy(int(sys.argv[2]))


def far(p, q):
    return math.hypot(p[0] - q[0], p[2] - q[2]) if p and q else float('nan')


ships = 0
different = 0
for v, (a, typ, ctl, par) in sorted(H.acts.items()):
    if typ != 5:
        continue
    like, parts = H.ship_like(v)
    if not like:
        continue
    pos = H.world_pos(a)
    sails = [(k, H.state(k)) for k in H.children.get(v, []) if H.acts[k][1] == 7 and k not in H.children]
    sails = [(k, s) for k, s in sails if s in NAMES]
    if not sails:
        continue
    ships += 1
    g_like, g_parts = G.ship_like(v) if v in G.acts else (False, 0)
    print(f'ship {v:08X} (controller {ctl:08X}, {parts} parts) at ({pos[0]:.0f}, {pos[2]:.0f}): {far(pos, H.own_pos):.0f} m '
          f'from the host, {far(pos, G.own_pos):.0f} m from the guest; on the guest: '
          + ('no such actor' if v not in G.acts else f'{g_parts} parts' + ('' if g_like else ' (not a whole ship there)')))
    for k, s in sails:
        gs = G.state(k)
        same = gs == s
        different += 0 if same or gs is None else 1
        print(f'    sail {k:08X}: host {NAMES[s]} | guest '
              + ('no such actor' if k not in G.acts else NAMES.get(gs, f'state {gs}'))
              + ('' if same else '   <-- DIFFERENT' if gs is not None else ''))
print(f'{ships} ship(s) with a sail on the host; sails that differ on the guest: {different}')
