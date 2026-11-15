"""deckdrift.py host guest seconds [period] : does a character standing still on a ship's deck stay where it stands?
Every `period` seconds (2): the speed of the host's ship on the host (0xB010000A), and each copy's OWN character's
position local to what it stands on, with how far it has moved from where it was at the start. On 15-nov the guest's
character slid 5.3 m aft along the deck in a minute and a half at 5-7 m/s while its copy's window was behind (and
went over the stern at 18 m/s): this is the tool that tells whether it also slides with its window in front.
Read-only (3663)."""
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
period = float(sys.argv[4]) if len(sys.argv) > 4 else 2.0


class Copy:
    def __init__(self, pid):
        self.r = r = Reader(pid)
        world = r.ptr(B + WORLD_SLOT)
        self.mgr = mgr = r.ptr(world + 0x30)
        t = r.ptr(mgr + 8)
        self.n = r.unpack(t + 0x88, '<I')[0]
        self.buckets = r.ptr(t + 0x98)
        self.nodes = r.ptr(t + 0xA0)

    def actor(self, key):
        r = self.r
        b = self.buckets + (key % self.n) * 0x100
        for i in range(min(r.unpack(b, '<I')[0], 31)):
            k, idx = r.unpack(b + 8 + i * 8, '<II')
            if k == key:
                return r.ptr(r.ptr(self.nodes + idx * 8) + 8)
        return 0

    def own_local(self):
        r = self.r
        comps = r.ptr(r.ptr(self.mgr + 0x50) + 0x68)
        ts, h = r.ptr(comps + 0x1A0), r.ptr(comps + 0x1B8)
        par = r.unpack(h + 0x1C, '<I')[0] if h else 0
        return r.unpack(ts + 0xB4, '<3f'), par

    def ship(self):
        r = self.r
        ts = r.ptr(r.ptr(self.actor(0xB010000A) + 0x68) + 0x1A0)
        p = r.unpack(ts + 0xB4, '<3f')
        s = r.unpack(ts + 0xC0, '<hh')
        return (p[0] + s[0] * 1000, p[2] + s[1] * 1000)


H, G = Copy(hp), Copy(gp)
h0, _ = H.own_local()
g0, _ = G.own_local()
prev = H.ship()
tp = t0 = time.time()
while time.time() - t0 < secs:
    time.sleep(period)
    try:
        now = time.time()
        s = H.ship()
        v = math.hypot(s[0] - prev[0], s[1] - prev[1]) / (now - tp)
        prev, tp = s, now
        hl, hpar = H.own_local()
        gl, gpar = G.own_local()
        print(f'{now - t0:5.1f} ship {v:4.1f} m/s | host on {hpar:#x} local ({hl[0]:.2f}, {hl[1]:.2f}, {hl[2]:.2f}) moved '
              f'{math.dist(hl, h0):.2f} m | guest on {gpar:#x} local ({gl[0]:.2f}, {gl[1]:.2f}, {gl[2]:.2f}) moved '
              f'{math.dist(gl, g0):.2f} m', flush=True)
    except Exception as e:
        print('err', e, flush=True)
