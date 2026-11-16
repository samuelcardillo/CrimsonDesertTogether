"""ownerinfo.py pid key [key ...] : who a copy thinks an actor belongs to (3663): the actor's vtable, its controller
key (actor+0x90), the byte at +0x94, its owner object (actor+0xA0) with that object's key (+0x60), the "controlled
here" byte (+0xC8: 0 a remote copy, 1 / 2 controlled by this client), its type ([actor+0x88]+1), and this client's own
user object ([[world]+0x30]+0x58) with its key. For "why can the guest not ride its own mount" (16-nov). Read-only."""
import os as _os
import sys

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
r = Reader(int(sys.argv[1]))
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
t = r.ptr(mgr + 8)
n = r.unpack(t + 0x88, '<I')[0]


def u(a, f):
    try:
        return r.unpack(a, f)[0]
    except Exception:
        return None


def find(key):
    b = r.ptr(t + 0x98) + (key % n) * 0x100
    for i in range(min(r.unpack(b, '<I')[0], 31)):
        k, idx = r.unpack(b + 8 + i * 8, '<II')
        if k == key:
            return r.ptr(r.ptr(r.ptr(t + 0xA0) + idx * 8) + 8)
    return 0


def h(v, w=8):
    return 'unreadable' if v is None else f'0x{v:0{w}X}'


user = u(mgr + 0x58, '<Q')
own = u(mgr + 0x50, '<Q')
print(f"this client's user object {h(user, 0)} key {h(u(user + 0x60, '<I') if user else None)}; own character "
      f"{h(u(own + 0x60, '<I') if own else None)}")
for ks in sys.argv[2:]:
    key = int(ks, 16)
    a = find(key)
    if not a:
        print(f'{key:08X}: not in this copy')
        continue
    owner = u(a + 0xA0, '<Q')
    tt = u(a + 0x88, '<Q')
    print(f"{key:08X}: vtable exe+{h((u(a, '<Q') or B) - B, 0)} controller(+0x90) {h(u(a + 0x90, '<I'))} +0x94 "
          f"{u(a + 0x94, '<B')} owner(+0xA0) {h(owner, 0)} (key {h(u(owner + 0x60, '<I') if owner else None)}"
          f"{', this client' if owner and owner == user else ''}) controlled here(+0xC8) {u(a + 0xC8, '<B')} type "
          f"{u(tt + 1, '<B') if tt else None}")
    # The vehicle component (comps+0x48): a mount's seat 0 (occupant +0x48, reservation +0x88, riders +0xC8, "has
    # seats" +0xEB) and a rider's link ([vc+0x18]: reserved vehicle +0x08 / seat +0x0C, current +0x10 / +0x14).
    comps = u(a + 0x68, '<Q')
    vc = u(comps + 0x48, '<Q') if comps else None
    if vc:
        link = u(vc + 0x18, '<Q')
        lk = (f"link: reserved {h(u(link + 0x08, '<I'))}/{u(link + 0x0C, '<b')} current {h(u(link + 0x10, '<I'))}/"
              f"{u(link + 0x14, '<b')}") if link else 'no link'
        print(f"          vehicle component: seat 0 occupant {h(u(vc + 0x48, '<I'))} reservation "
              f"{h(u(vc + 0x88, '<I'))} riders {u(vc + 0xC8, '<B')} has seats {u(vc + 0xEB, '<B')} | {lk}")
