"""tagmon.py host guest seconds [period] : where the mod's name tag for the OTHER player is against where the game draws
that player, on both copies (3663). Until 2026-11-15 the tag was drawn at the position the other player's mod sent:
its PlayerReader position (the character's physics position plus the chunk origin, src/mod/game/player_reader.cpp).
The character is drawn where this copy's game has its actor (the TransformSync chain up to the ship). For each copy
this prints
  reader - own : how far the position PlayerReader gives is from its own character's place (a stale physics position
                 while standing on a moving ship shows here),
  the tag at the position sent (what the mod drew until 15-nov): how far from the character the OTHER copy draws,
  the tag now  : the same for what the mod draws since then while the character stands on a ship part: the other
                 copy's own copy of that part with the character's pose local to it
                 (session2::vehicle_follow_peer_world); '-' when it stands on nothing,
and the ship's speed on the host. Read-only."""
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
CHUNK_ORIGIN = 0x7237470  # 3732 (src/mod/game/build_profile.h; 3663: 0x7231330)
hp, gp, secs = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3])
period = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0


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


class Copy:
    def __init__(self, pid):
        self.r = r = Reader(pid)
        world = r.ptr(B + WORLD_SLOT)
        self.mgr = mgr = r.ptr(world + 0x30)
        t = r.ptr(mgr + 8)
        self.n = r.unpack(t + 0x88, '<I')[0]
        self.buckets = r.ptr(t + 0x98)
        self.nodes = r.ptr(t + 0xA0)
        self.own = r.ptr(mgr + 0x50)
        self.key = r.unpack(self.own + 0x60, '<I')[0]

    def actor(self, key):
        r = self.r
        b = self.buckets + (key % self.n) * 0x100
        for i in range(min(r.unpack(b, '<I')[0], 31)):
            k, idx = r.unpack(b + 8 + i * 8, '<II')
            if k == key:
                return r.ptr(r.ptr(self.nodes + idx * 8) + 8)
        return 0

    def world(self, key, depth=0):
        r = self.r
        a = self.actor(key)
        comps = r.ptr(a + 0x68)
        ts, h = r.ptr(comps + 0x1A0), r.ptr(comps + 0x1B8)
        par = r.unpack(h + 0x1C, '<I')[0] if h else 0
        p = r.unpack(ts + 0xB4, '<3f')
        s = r.unpack(ts + 0xC0, '<hh')
        q = r.unpack(ts + 0xA4, '<4f')
        p = (p[0] + s[0] * 1000, p[1], p[2] + s[1] * 1000)
        if not par or depth > 5:
            return p, q, par
        pp, pq, _ = self.world(par, depth + 1)
        w = rot(pq, p)
        # The rotation that goes up is the WORLD one (until 15-nov 03:35 this returned the actor's own local
        # rotation: right for an actor directly under the ship's vehicle, wrong by the ship's heading for anything
        # two levels down, a character on the hull included).
        return (pp[0] + w[0], pp[1] + w[1], pp[2] + w[2]), qmul(pq, q), par

    def local(self, key):  # an actor's pose local to its parent, and the parent's key
        r = self.r
        comps = r.ptr(self.actor(key) + 0x68)
        ts, h = r.ptr(comps + 0x1A0), r.ptr(comps + 0x1B8)
        par = r.unpack(h + 0x1C, '<I')[0] if h else 0
        p = r.unpack(ts + 0xB4, '<3f')
        q = r.unpack(ts + 0xA4, '<4f')
        return p, q, par

    def reader(self):  # what PlayerReader::read gives for this copy's own character
        r = self.r
        comps = r.ptr(self.own + 0x68)
        control = r.ptr(comps + 0x40)
        inner = r.ptr(control + 0x140)
        physics = r.ptr(inner + 0x2D0)
        p = r.unpack(physics + 0x90, '<3f')
        o = r.unpack(B + CHUNK_ORIGIN, '<3f')
        return (p[0] + o[0], p[1] + o[1], p[2] + o[2])


H, G = Copy(hp), Copy(gp)
print(f'host character {H.key:08X}, guest character {G.key:08X}')
t0 = time.time()
prev = None
while time.time() - t0 < secs:
    try:
        ship, _, _ = H.world(0xB010000A)
        sp = math.hypot(ship[0] - prev[0], ship[2] - prev[2]) / period if prev else 0
        prev = ship
        hr, gr = H.reader(), G.reader()
        hown, _, hpar = H.world(H.key)
        gown, _, gpar = G.world(G.key)
        g_draws_h, _, _ = G.world(H.key)
        h_draws_g, _, _ = H.world(G.key)

        def now_tag(owner, viewer, par):
            # the viewer's copy of what the owner's character hangs from, with the owner's pose local to it
            if not par:
                return None
            lp, _lq, _ = owner.local(owner.key)
            pp, pq, _ = viewer.world(par)
            w = rot(pq, lp)
            return (pp[0] + w[0], pp[1] + w[1], pp[2] + w[2])

        hn, gn = now_tag(H, G, hpar), now_tag(G, H, gpar)
        hn_s = f'{math.dist(hn, g_draws_h):5.2f} m' if hn else '    -'
        gn_s = f'{math.dist(gn, h_draws_g):5.2f} m' if gn else '    -'
        print(f'{time.time() - t0:5.1f} ship {sp:4.1f} m/s | host (on {hpar:#x}): reader - own {math.dist(hr, hown):5.2f} m; '
              f'the guest draws its tag {math.dist(hr, g_draws_h):5.2f} m from its character at the position sent '
              f'(dy {hr[1] - g_draws_h[1]:+.2f}), now {hn_s} | guest (on {gpar:#x}): reader - own '
              f'{math.dist(gr, gown):5.2f} m; the host draws its tag {math.dist(gr, h_draws_g):5.2f} m from its '
              f'character at the position sent (dy {gr[1] - h_draws_g[1]:+.2f}), now {gn_s}', flush=True)
    except Exception as e:  # an actor between tables
        print('err', e, flush=True)
    time.sleep(period)
