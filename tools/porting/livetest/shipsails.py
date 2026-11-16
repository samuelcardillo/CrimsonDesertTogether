"""shipsails.py host_pid guest_pid [seconds [period]] : every ship's sail on both copies (3663). On the host: each
vehicle with two or more gimmicks hanging directly from it, one of them carrying others (an AI ship seen from far has
four such parts, from near thirteen; the player's has fourteen), and of the LEAF gimmicks that hang directly from it
the ones whose branch state (+0x270 of the gimmick component) is one of a sail's on either copy and not the resting
one on both (89746c86: a sail never set, but also every lantern and cannon at rest). For each: the state on the host, and on the guest its state
there, its controller there, or "no such actor". A line ends in DIFFERENT when the two copies do not agree: that is
"the NPC boats don't show their sail status" (16-nov). With seconds: a look every `period` (4) with the time, one
line per ship. Read-only."""
import os as _os
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
REST = '89746c86'
NAMES = {REST: 'furled', '4dd95dbe': 'first gear', 'e480ff9b': 'second gear', '3f7594b0': 'third gear',
         '7d8452b9': 'going to first gear', 'a0f2a008': 'going to second gear', 'ea7cd220': 'going to third gear',
         '0795c60c': 'changing', 'f5af9c8c': 'changing', 'b6bcab80': 'changing', '528b3c3a': 'going down to first gear',
         '853c63b7': 'furled (taken in)', 'a0ba7f37': 'changing'}


class Copy:
    def __init__(self, r):
        self.r = r
        world = r.ptr(B + WORLD_SLOT)
        mgr = r.ptr(world + 0x30)
        t = r.ptr(mgr + 8)
        self.n = r.unpack(t + 0x88, '<I')[0]
        self.buckets, self.nodes = r.ptr(t + 0x98), r.ptr(t + 0xA0)
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

    def pos(self, key):
        r = self.r
        comps = r.ptr(self.acts[key][0] + 0x68)
        ts = r.ptr(comps + 0x1A0) if comps else 0
        if not ts:
            return (0.0, 0.0, 0.0)
        p = r.unpack(ts + 0xB4, '<3f')
        s = r.unpack(ts + 0xC0, '<hh')
        return (p[0] + s[0] * 1000, p[1], p[2] + s[1] * 1000)

    def parts(self, v):
        """gimmicks hanging directly from v; 0 when none of them carries anything."""
        direct = [k for k in self.children.get(v, []) if self.acts[k][1] == 7]
        return len(direct) if any(k in self.children for k in direct) else 0

    def leaves(self, v):
        return [k for k in self.children.get(v, []) if self.acts[k][1] == 7 and k not in self.children]

    def state(self, key):
        if key not in self.acts:
            return None
        r = self.r
        try:
            comps = r.ptr(self.acts[key][0] + 0x68)
            g = r.ptr(comps + 0x30) if comps else 0
            return r.read(g + 0x270, 4).hex() if g else None
        except Exception:
            return None


def name(s):
    return 'no such actor' if s is None else NAMES.get(s, s)


hr, gr = Reader(int(sys.argv[1])), Reader(int(sys.argv[2]))
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 0
period = float(sys.argv[4]) if len(sys.argv) > 4 else 4.0
t0 = time.time()
looks = different_looks = 0
while True:
    H, G = Copy(hr), Copy(gr)
    stamp = time.strftime('%H:%M:%S')
    ships = different = 0
    for v, (a, typ, ctl, par) in sorted(H.acts.items()):
        if typ != 5 or H.parts(v) < 2:
            continue
        rows = []
        for k in H.leaves(v):
            hs, gs = H.state(k), G.state(k)
            if hs in (None, REST) and gs in (None, REST):
                continue
            # Only a sail's states count (a lit lantern or a loaded cannon has a state of its own, never relayed).
            if not any(x in NAMES and x != REST for x in (hs, gs)):
                continue
            ok = hs == gs
            different += 0 if ok else 1
            rows.append(f'{k:08X} host {name(hs)} | guest {name(gs)}'
                        + (f' (its controller there {G.acts[k][2]:08X})' if k in G.acts and G.acts[k][2] != ctl else '')
                        + ('' if ok else '  <-- DIFFERENT'))
        if not rows:
            continue
        ships += 1
        p = H.pos(v)
        print(f'{stamp} ship {v:08X} (controller {ctl:08X}) at ({p[0]:.0f}, {p[2]:.0f}), parts host {H.parts(v)} guest '
              + (str(G.parts(v)) if v in G.acts else 'no such actor') + ': ' + '; '.join(rows), flush=True)
    looks += 1
    different_looks += 1 if different else 0
    if not ships:
        print(f'{stamp} no ship with a sail set on either copy', flush=True)
    if time.time() - t0 >= secs:
        break
    time.sleep(period)
if secs:
    print(f'{looks} looks, {different_looks} with a sail that differs')
