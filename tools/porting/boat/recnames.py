"""recnames.py capture.bin key_lo key_hi [t_from t_to] : for every ActorLoadDataAck record whose key is in
[key_lo, key_hi] (hex): its type, key, controller (+0x12), the name's length (+0x1E, UTF-16 characters) and the name
(+0x22), and the two copies of the parent after the name (3527 layout, also 3663: name end +0xB9 and +0xD7), next to
what sits at the fixed +0xF5 / +0x113 (where those are for a 13-character name). Read-only."""
import mmap
import struct
import sys

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/scripts')
from analyze_capture import REC, actor_records  # noqa: E402

path = sys.argv[1]
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
ta = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
tb = float(sys.argv[5]) if len(sys.argv) > 5 else 1e12
NATIVE_2005 = None
import re  # noqa: E402
for line in open(r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/src/common/native_opcode_table.inc',
                 encoding='utf-8'):
    if line.startswith('constexpr'):
        on = 'kEntries3663' in line
        continue
    mm = re.match(r'\s*\{(\d+), (\d+), \d+U, "ActorLoadDataAck"\}', line)
    if mm and on:
        NATIVE_2005 = int(mm.group(2))
f = open(path, 'rb')
m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
pos = 12
t0 = None


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0] if o + 4 <= len(b) else None


def fmt(v):
    return '--------' if v is None else f'{v:08X}'


while pos + REC.size <= len(m):
    t, d, _, size, copied, _, sess, thread = REC.unpack_from(m, pos)
    pos += REC.size
    pkt = m[pos:pos + copied]
    pos += copied
    if t0 is None:
        t0 = t
    tt = (t - t0) / 1e6
    if tt > tb:
        break
    if tt < ta or len(pkt) < 9 or struct.unpack_from('<H', pkt, 0)[0] != NATIVE_2005:
        continue
    for rec in actor_records(bytes(pkt[5:])):
        body = rec if isinstance(rec, (bytes, bytearray)) else rec[-1]
        if len(body) < 0x26:
            continue
        key = u32(body, 1)
        if not (lo <= key <= hi):
            continue
        n = u32(body, 0x1E)
        name = body[0x22:0x22 + 2 * n].decode('utf-16-le', 'replace') if n is not None and n <= 64 else '?'
        ne = 0x22 + 2 * (n if n is not None and n <= 64 else 0)
        print(f't={tt:8.3f} type {body[0]} key {key:08X} ctl {fmt(u32(body, 0x12))} name[{n}] {name!r:28s} '
              f'ne+0xB9 {fmt(u32(body, ne + 0xB9))} ne+0xD7 {fmt(u32(body, ne + 0xD7))} | '
              f'+0xF5 {fmt(u32(body, 0xF5))} +0x113 {fmt(u32(body, 0x113))}')
