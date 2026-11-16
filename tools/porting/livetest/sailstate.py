"""sailstate.py pid key|KEY : the ship's sail on one copy (3663).
  sailstate.py pid key  : the sail's actor key: the gimmick that hangs directly from the vehicle 0xB010000A at local
                          (0, 0, 0) and that nothing hangs from (hull key + 0xA in every session so far; the hull
                          is the other one at local 0, 0, 0).
  sailstate.py pid KEY  : its branch state: the four bytes at +0x270 of its gimmick component ([[actor+0x68]+0x30]),
                          as the packets show them, with the state's name when it is one of the sail's known ones
                          (docs/research/port-3663.md: 89746c86 furled, 4dd95dbe first gear, e480ff9b second,
                          3f7594b0 third; 7d8452b9 / a0f2a008 / ea7cd220 are the changes on the way to each). When
                          +0x270 holds none of them, the first place in the component's first 0x400 bytes that does.
Read-only."""
import os as _os
import sys

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
VEHICLE = int(__import__("os").environ.get("CDMP_SHIP", "0xB010000A"), 16)
NAMES = {'89746c86': 'furled', '4dd95dbe': 'first gear', 'e480ff9b': 'second gear', '3f7594b0': 'third gear',
         '7d8452b9': 'going to first gear', 'a0f2a008': 'going to second gear', 'ea7cd220': 'going to third gear',
         '0795c60c': 'known sail hash', 'f5af9c8c': 'known sail hash', 'b6bcab80': 'known sail hash'}
r = Reader(int(sys.argv[1]))
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
t = r.ptr(mgr + 8)
n = r.unpack(t + 0x88, '<I')[0]
buckets, nodes = r.ptr(t + 0x98), r.ptr(t + 0xA0)


def actor(key):
    b = buckets + (key % n) * 0x100
    for i in range(min(r.unpack(b, '<I')[0], 31)):
        k, idx = r.unpack(b + 8 + i * 8, '<II')
        if k == key:
            return r.ptr(r.ptr(nodes + idx * 8) + 8)
    return 0


if sys.argv[2] == 'key':
    # Two gimmicks hang directly from the vehicle at local (0, 0, 0): the hull, which everything else hangs from, and
    # the sail, which nothing hangs from. (The first version took the first of the two: the hull, 15-nov 06:44.)
    cands, parents = [], set()
    for bi in range(n):
        base = buckets + bi * 0x100
        try:
            c = r.unpack(base, '<I')[0]
            for i in range(min(c, 31)):
                k, idx = r.unpack(base + 8 + i * 8, '<II')
                a = r.ptr(r.ptr(nodes + idx * 8) + 8)
                if not a or r.unpack(a + 0x60, '<I')[0] != k:
                    continue
                info = r.ptr(a + 0x88)
                comps = r.ptr(a + 0x68)
                h = r.ptr(comps + 0x1B8) if comps else 0
                ts = r.ptr(comps + 0x1A0) if comps else 0
                if not h:
                    continue
                par = r.unpack(h + 0x1C, '<I')[0]
                if par:
                    parents.add(par)
                if par != VEHICLE or not info or not ts or r.unpack(info + 1, '<B')[0] != 7:
                    continue
                p = r.unpack(ts + 0xB4, '<3f')
                if max(abs(v) for v in p) < 0.01:
                    cands.append(k)
        except Exception:
            continue
    leaves = [k for k in cands if k not in parents]
    if leaves:
        print(f'{max(leaves):08X}')
    raise SystemExit(0)

key = int(sys.argv[2], 16)
a = actor(key)
if not a:
    print('absent (this copy has no such actor)')
    raise SystemExit(0)
comp = r.ptr(r.ptr(a + 0x68) + 0x30)
if not comp:
    print('no-gimmick-component')
    raise SystemExit(0)
at = r.read(comp + 0x270, 4).hex()
if at in NAMES:
    print(f'{at} ({NAMES[at]})')
else:
    blob = r.read(comp, 0x400)
    found = ''
    for off in range(0, 0x400 - 3, 4):
        h = blob[off:off + 4].hex()
        if h in NAMES:
            found = f'{h} ({NAMES[h]}, at +{off:#x})'
            break
    print(found or f'{at} (not a known sail state; +0x270)')
