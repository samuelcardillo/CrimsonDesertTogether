"""interdump.py pid key : an actor's interaction entries on a copy (3663). The actor's components (actor+0x68, a block
of pointers) are searched for the one that keeps its entries the way CommonInteractionActorComponent does: a begin /
end pointer pair at +0x90 / +0x98 with 0x38 bytes an entry (session2/interaction_condition.cpp). Printed: the
component's slot and vtable, and each entry as hex with the fields the serializer writes (+0x08 condition holder,
+0x10, +0x14, +0x18, +0x20 stage, +0x28, +0x2C, +0x2E parent index, +0x30). To compare the same interaction ("Ride" on
a mount) between the copy where it works and the one where the prompt is grey (16-nov). Read-only."""
import os as _os
import struct
import sys

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
r = Reader(int(sys.argv[1]))
key = int(sys.argv[2], 16)
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
t = r.ptr(mgr + 8)
n = r.unpack(t + 0x88, '<I')[0]
b = r.ptr(t + 0x98) + (key % n) * 0x100
a = 0
for i in range(min(r.unpack(b, '<I')[0], 31)):
    k, idx = r.unpack(b + 8 + i * 8, '<II')
    if k == key:
        a = r.ptr(r.ptr(r.ptr(t + 0xA0) + idx * 8) + 8)
if not a:
    raise SystemExit(f'{key:08X} is not in this copy\'s actor table')
comps = r.ptr(a + 0x68)


def q(addr):
    try:
        return r.unpack(addr, '<Q')[0]
    except Exception:
        return None


found = 0
for off in range(0, 0x400, 8):
    c = q(comps + off)
    if not c or c < 0x10000 or c >> 47:
        continue
    vt = q(c)
    if not vt or not (B <= vt < B + 0x8000000):
        continue
    beg, end = q(c + 0x90), q(c + 0x98)
    if not beg or not end or end < beg or (end - beg) % 0x38 or not (0 < (end - beg) // 0x38 <= 64):
        continue
    cnt = (end - beg) // 0x38
    try:
        raw = r.read(beg, cnt * 0x38)
    except Exception:
        continue
    found += 1
    print(f'component at comps+0x{off:X}: 0x{c:X} vtable exe+0x{vt - B:X}, {cnt} entries')
    for i in range(cnt):
        e = raw[i * 0x38:(i + 1) * 0x38]
        holder, = struct.unpack_from('<Q', e, 0x08)
        f10, f14, f18 = struct.unpack_from('<III', e, 0x10)
        stage, = struct.unpack_from('<Q', e, 0x20)
        f28, = struct.unpack_from('<I', e, 0x28)
        f2c, f2e = struct.unpack_from('<HH', e, 0x2C)
        f30, = struct.unpack_from('<Q', e, 0x30)
        h0 = q(holder) if holder else None
        print(f'  [{i:2d}] +00 {struct.unpack_from("<Q", e, 0)[0]:016X} holder {"null" if not holder else "set"}'
              f'{"" if not holder else f" (first {h0:X})" if h0 is not None else " (unreadable)"} +10 {f10:08X} +14 '
              f'{f14:08X} +18 {f18:08X} stage {"0" if not stage else "set"} +28 {f28:08X} +2C {f2c:04X} parent(+2E) '
              f'{f2e:04X} +30 {f30:X}')
if not found:
    print('no component with 0x38-byte entries at +0x90/+0x98 among the first 0x400 bytes of its components')
