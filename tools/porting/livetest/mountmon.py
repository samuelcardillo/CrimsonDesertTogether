"""mountmon.py owner_pid observer_pid seconds [csv] | mountmon.py --csv file : how the OBSERVER's copy draws the owner's character and what it
rides (a horse, a cart, anything the character's hierarchy parent is), against where the owner's own client has them.

Both copies are sampled a few hundred times a second (world position of the character and of its mount on each).
Reported for the time the owner moves at 1 m/s or more:
- the delay: the time shift at which the observer's copy best matches the owner's path (0 to 2 s), and how far it is
  from that path at that delay (mean, 95 %, worst): a steady delay with a small rest is a smooth copy; a large rest is
  a copy that jumps, stalls or cuts corners;
- how evenly the copy moves: its frames (position changes), the ones that go BACK along its course, the time it stands
  still (no change for 150 ms or more) and the distance covered in each quarter of a second against the 1.25 s around;
- the seat: how far the copy of the character is from where it sits on the copy of the mount (the owner's offset
  between the two, turned with the mount), which is what shows as a rider sliding off its horse.
csv: every sample goes to that file (t, then x y z of owner character, owner mount, observer character, observer
mount). Read-only (3663)."""
import math
import os as _os
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000


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


def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])


class Copy:
    def __init__(self, pid):
        self.r = r = Reader(pid)
        world = r.ptr(B + WORLD_SLOT)
        self.mgr = mgr = r.ptr(world + 0x30)
        t = r.ptr(mgr + 8)
        self.n = r.unpack(t + 0x88, '<I')[0]
        self.buckets = r.ptr(t + 0x98)
        self.nodes = r.ptr(t + 0xA0)
        self.cache = {}

    def own_key(self):
        a = self.r.ptr(self.mgr + 0x50)
        return self.r.unpack(a + 0x60, '<I')[0]

    def find(self, key):
        if key in self.cache:
            return self.cache[key]
        r = self.r
        b = self.buckets + (key % self.n) * 0x100
        c = r.unpack(b, '<I')[0]
        for i in range(min(c, 31)):
            k, idx = r.unpack(b + 8 + i * 8, '<II')
            if k == key:
                a = r.ptr(r.ptr(self.nodes + idx * 8) + 8)
                comps = r.ptr(a + 0x68)
                self.cache[key] = (r.ptr(comps + 0x1A0), r.ptr(comps + 0x1B8))
                return self.cache[key]
        return (0, 0)

    def local(self, key):
        ts, h = self.find(key)
        if not ts:
            return None
        r = self.r
        par = r.unpack(h + 0x1C, '<I')[0] if h else 0
        p = r.unpack(ts + 0xB4, '<3f')
        s = r.unpack(ts + 0xC0, '<hh')
        q = r.unpack(ts + 0xA4, '<4f')
        return par, (p[0] + s[0] * 1000, p[1], p[2] + s[1] * 1000), q

    def world(self, key, depth=0):
        got = self.local(key)
        if got is None:
            return None
        par, p, q = got
        if not par or depth > 5:
            return par, p, q
        up = self.world(par, depth + 1)
        if up is None:
            return par, p, q
        _, pp, pq = up
        w = rot(pq, p)
        return par, (pp[0] + w[0], pp[1] + w[1], pp[2] + w[2]), qmul(pq, q)


def lerp_at(series, t):
    """series: list of (t, (x, y, z)); linear interpolation at t, None outside."""
    lo, hi = 0, len(series) - 1
    if hi < 1 or t < series[0][0] or t > series[hi][0]:
        return None
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if series[mid][0] <= t:
            lo = mid
        else:
            hi = mid
    t0, a = series[lo]
    t1, b = series[hi]
    f = (t - t0) / (t1 - t0) if t1 > t0 else 0
    return tuple(a[i] + (b[i] - a[i]) * f for i in range(3))


def speed_at(series, t, half=0.25):
    a, b = lerp_at(series, t - half), lerp_at(series, t + half)
    if a is None or b is None:
        return None
    return math.hypot(b[0] - a[0], b[2] - a[2]) / (2 * half)


def pct(v, p):
    if not v:
        return float('nan')
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p))]


def clean(series, far=50.0, short=0.05):
    """Without the looks that caught the game in the middle of a frame: when a character takes or leaves a parent the
    game writes the new parent and the new local position at different moments of the same frame, and for a few
    milliseconds the two do not belong together (the composed position is then kilometres away: world + world, or a
    local position read as a world one). The owner's own copy shows the same thing. A run of positions more than
    `far` from the last good one that lasts less than `short` seconds was never drawn: dropped. Returns (series,
    number dropped)."""
    out = []
    run = []
    dropped = 0
    for t, p in series:
        if not out:
            out.append((t, p))
            continue
        g = out[-1][1]
        if math.hypot(p[0] - g[0], p[2] - g[2]) > far:
            run.append((t, p))
            if run[-1][0] - run[0][0] >= short:  # it stays there: it is where the copy is
                out.extend(run)
                run = []
            continue
        dropped += len(run)
        run = []
        out.append((t, p))
    dropped += len(run)
    return out, dropped


def nearest_delay(own, t, p, lo=0.0, hi=2.0, step=0.01):
    """The delay d in [lo, hi] at which the owner's path point own(t - d) is nearest to p: (d, distance)."""
    best = None
    d = lo
    while d <= hi + 1e-9:
        o = lerp_at(own, t - d)
        if o is not None:
            e = math.hypot(p[0] - o[0], p[2] - o[2])
            if best is None or e < best[1]:
                best = (d, e)
        d += step
    return best


def delay_series(own, obs, min_speed=1.0, far=50.0):
    """For every FRAME of the copy (a change of position) while the owner moves: the delay at which the copy is
    nearest to the owner's path, and how far from the path it is there. Samples further than `far` from the owner
    are not the copy moving: counted apart (the copy drawn somewhere else altogether)."""
    out, away = [], []
    last = None
    for t, p in obs:
        cur = (p[0], p[2])
        if cur == last:
            continue
        last = cur
        o = lerp_at(own, t)
        if o is None:
            continue
        if math.hypot(p[0] - o[0], p[2] - o[2]) > far:
            away.append(t)
            continue
        sp = speed_at(own, t - 0.4)
        if sp is None or sp < min_speed:
            continue
        got = nearest_delay(own, t, p)
        if got:
            out.append((t, got[0], got[1], sp))
    return out, away


def evenness(obs, min_speed=1.0):
    """Frames of the copy while it moves: back steps, standstills, quarter-second speed against the 1.25 s around."""
    frames = []
    last = None
    for t, p in obs:
        cur = (p[0], p[2])
        if cur != last:
            frames.append((t, p[0], p[2]))
            last = cur
    if len(frames) < 20:
        return None
    moving = back = 0
    back_max = still_s = moved_s = 0.0
    j0 = j1 = 0
    for i in range(1, len(frames)):
        ti, xi, zi = frames[i]
        while frames[j0][0] < ti - 0.5:
            j0 += 1
        while j1 + 1 < len(frames) and frames[j1][0] < ti + 0.5:
            j1 += 1
        span = frames[j1][0] - frames[j0][0]
        dx, dz = frames[j1][1] - frames[j0][1], frames[j1][2] - frames[j0][2]
        dist = math.hypot(dx, dz)
        if span < 0.3 or dist / span < min_speed:
            continue
        dt = ti - frames[i - 1][0]
        along = ((xi - frames[i - 1][1]) * dx + (zi - frames[i - 1][2]) * dz) / dist
        moving += 1
        moved_s += dt
        if along < -0.05:
            back += 1
            back_max = max(back_max, -along)
        if dt >= 0.15:
            still_s += dt
    ratios = []
    t = frames[0][0] + 0.625
    series = [(f[0], (f[1], 0.0, f[2])) for f in frames]
    while t < frames[-1][0] - 0.625:
        a, b = lerp_at(series, t - 0.125), lerp_at(series, t + 0.125)
        c, d = lerp_at(series, t - 0.625), lerp_at(series, t + 0.625)
        if a and b and c and d:
            q = math.hypot(b[0] - a[0], b[2] - a[2]) / 0.25
            w = math.hypot(d[0] - c[0], d[2] - c[2]) / 1.25
            if w >= min_speed:
                ratios.append(q / w)
        t += 0.25
    return dict(frames=len(frames), secs=frames[-1][0] - frames[0][0], moving=moving, moved_s=moved_s, back=back,
                back_max=back_max, still_s=still_s, ratios=ratios)


def report(name, own, obs):
    if len(own) < 50 or len(obs) < 50:
        print(f'{name}: too few samples (owner {len(own)}, observer {len(obs)})')
        return
    own, d_own = clean(own)
    obs, d_obs = clean(obs)
    if d_own or d_obs:
        print(f'{name}: {d_own} looks at the owner and {d_obs} at the copy caught a frame half written (a parent taken '
              f'or left): not counted')
    ser, away = delay_series(own, obs)
    if away:
        print(f'{name}: the copy was drawn more than 50 m from the owner in {len(away)} of its frames '
              f'(first at {away[0]:.1f} s, last at {away[-1]:.1f} s)')
    if len(ser) < 10:
        print(f'{name}: the owner moved at 1 m/s or more for only {len(ser)} frames of the copy')
        return
    d = [x[1] * 1000 for x in ser]
    e = [x[2] for x in ser]
    top = max(x[3] for x in ser)
    print(f'{name}: {len(ser)} frames of the copy while the owner moved (up to {top:.1f} m/s): it shows the owner '
          f'{pct(d, 0.5):.0f} ms late (5 % {pct(d, 0.05):.0f}, 95 % {pct(d, 0.95):.0f}, worst {max(d):.0f}); off the '
          f"owner's path by {pct(e, 0.5):.2f} m median, {pct(e, 0.95):.2f} m at 95 %, {max(e):.2f} m worst")
    # How steadily the copy plays: the delay's change between frames about half a second apart, as a playback
    # speed (1 = the owner's own pace; 0.5 = half speed; above 1 = catching up).
    rates = []
    j = 0
    for i in range(len(ser)):
        while j < i and ser[i][0] - ser[j][0] > 0.6:
            j += 1
        dt = ser[i][0] - ser[j][0]
        if 0.35 <= dt <= 0.6:
            rates.append(1 - (ser[i][1] - ser[j][1]) / dt)
    if rates:
        sd = (sum((x - 1) ** 2 for x in rates) / len(rates)) ** 0.5
        slow = sum(1 for x in rates if x < 0.7) / len(rates)
        fast = sum(1 for x in rates if x > 1.3) / len(rates)
        print(f'{name}: playback speed over half a second: sd {sd:.2f}, {min(rates):.2f} to {max(rates):.2f}; below '
              f'0.7 in {slow * 100:.0f} % of the time, above 1.3 in {fast * 100:.0f} %')
    ev = evenness(obs)
    if ev:
        print(f"{name}: the copy drew {ev['frames']} frames in {ev['secs']:.1f} s; while moving ({ev['moved_s']:.1f} s): "
              f"{ev['back']} frames went back (worst {ev['back_max']:.2f} m), stood still {ev['still_s']:.1f} s")


def from_csv(path):
    """mountmon.py --csv file : the report again from a file a run wrote."""
    oc, om, bc, bm = [], [], [], []
    for line in open(path):
        v = [float(x) for x in line.split(',')]
        t = v[0]
        oc.append((t, tuple(v[1:4])))
        bc.append((t, tuple(v[7:10])))
        if v[4] or v[6]:
            om.append((t, tuple(v[4:7])))
            bm.append((t, tuple(v[10:13])))
    report('character', oc, bc)
    if om:
        report('mount', om, bm)


def main():
    if sys.argv[1] == '--csv':
        return from_csv(sys.argv[2])
    own_c, obs_c = Copy(int(sys.argv[1])), Copy(int(sys.argv[2]))
    secs = float(sys.argv[3])
    csv = open(sys.argv[4], 'w') if len(sys.argv) > 4 else None
    key = own_c.own_key()
    print(f"owner's character 0x{key:08X}")
    s_own_c, s_own_m, s_obs_c, s_obs_m, seat = [], [], [], [], []
    parents = {}
    seated = 0
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < secs:
        try:
            t = time.perf_counter() - t0
            a = own_c.world(key)
            par = a[0]
            m = own_c.world(par) if par else None
            t_obs = time.perf_counter() - t0
            b = obs_c.world(key)
            n = obs_c.world(par) if par else None
        except Exception:  # a read that failed (an actor going away): skip the sample
            time.sleep(0.01)
            own_c.cache.clear()
            obs_c.cache.clear()
            continue
        if a is None or b is None:
            time.sleep(0.05)
            continue
        parents[(par, b[0])] = parents.get((par, b[0]), 0) + 1
        # A copy that is seated on its mount (the observer's own hierarchy parent) is carried by it: its place is
        # the mount's plus a seat offset the game writes later in the frame than the mount's position, so "its path"
        # is the mount's and what matters is the seat. Counted as a character only while it has no parent there.
        if b[0] == 0:
            s_own_c.append((t, a[1]))
            s_obs_c.append((t_obs, b[1]))
        else:
            seated += 1
        if m and n:
            s_own_m.append((t, m[1]))
            s_obs_m.append((t_obs, n[1]))
            # the seat: the owner's character in its mount's frame, put on the observer's copy of the mount
            d = (a[1][0] - m[1][0], a[1][1] - m[1][1], a[1][2] - m[1][2])
            loc = rot(qconj(m[2]), d)
            w = rot(n[2], loc)
            want = (n[1][0] + w[0], n[1][1] + w[1], n[1][2] + w[2])
            seat.append((math.hypot(b[1][0] - want[0], b[1][2] - want[2]), b[1][1] - want[1]))
        if csv:
            row = [f'{t:.4f}']
            for v in (a[1], m[1] if m else (0, 0, 0), b[1], n[1] if n else (0, 0, 0)):
                row += [f'{v[0]:.3f}', f'{v[1]:.3f}', f'{v[2]:.3f}']
            csv.write(','.join(row) + '\n')
        time.sleep(0.002)
    print(f'{len(s_own_c)} samples in {secs:.0f} s; (owner parent, observer parent) seen: ' +
          ', '.join(f'(0x{p:X}, 0x{q:X}) x{c}' for (p, q), c in sorted(parents.items(), key=lambda kv: -kv[1])))
    if seated:
        print(f'character: seated on its mount on the observer in {seated} looks (carried by it: see "mount" and '
              f'"seat"); what follows is the rest (on foot, or not seated there)')
    report('character', s_own_c, s_obs_c)
    if s_own_m:
        report('mount', s_own_m, s_obs_m)
        seat = [s for s in seat if s[0] < 50.0] or seat  # without the half-written frames (see clean())
        h = [s[0] for s in seat]
        v = [abs(s[1]) for s in seat]
        print(f'seat: the copy of the character is {sum(h) / len(h):.2f} m (95 %: {pct(h, 0.95):.2f}, worst '
              f'{max(h):.2f}) across and {sum(v) / len(v):.2f} m (worst {max(v):.2f}) up or down from its place on '
              f'the copy of the mount ({len(seat)} samples)')
    else:
        print('mount: the character had no parent in this time (on foot)')


main()
