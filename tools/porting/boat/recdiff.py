"""recdiff.py capture.bin keyA keyB [t_from t_to] : the ActorLoadDataAck records of two actors (hex keys) from a .bin
capture, compared byte by byte when they have the same length: the offsets that differ, as hex, u32 and float.
With one more argument 'dump', prints the first 160 and the last 96 bytes of each instead. Read-only."""
import mmap
import struct
import sys
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/scripts')
from analyze_capture import REC, actor_records

path = sys.argv[1]
ka, kb = int(sys.argv[2], 16), int(sys.argv[3], 16)
args = [a for a in sys.argv[4:] if a != 'dump']
dump = 'dump' in sys.argv[4:]
ta = float(args[0]) if len(args) > 0 else 0.0
tb = float(args[1]) if len(args) > 1 else 1e12
f = open(path, 'rb')
m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
pos = 12
t0 = None
found = {}
while pos + REC.size <= len(m) and len(found) < 2:
    t, d, _, size, copied, _, sess, thread = REC.unpack_from(m, pos)
    pos += REC.size
    pkt = m[pos:pos + copied]
    pos += copied
    if t0 is None:
        t0 = t
    tt = (t - t0) / 1e6
    if tt < ta or d != 2 or len(pkt) < 605:
        continue
    if tt > tb:
        break
    payload = pkt[5:]
    recs = actor_records(payload)
    if not recs or sum(len(r) + 2 for r in recs) != len(payload):
        continue
    for r in recs:
        if len(r) >= 5:
            k = struct.unpack_from('<I', r, 1)[0]
            if k in (ka, kb) and k not in found:
                found[k] = (tt, bytes(r))
for k in (ka, kb):
    if k not in found:
        print(f'{k:08X}: no record found')
        sys.exit(1)
a, b = found[ka][1], found[kb][1]
print(f'{ka:08X}: t={found[ka][0]:.3f} len {len(a)} | {kb:08X}: t={found[kb][0]:.3f} len {len(b)}')
if dump:
    for k, r in ((ka, a), (kb, b)):
        print(f'{k:08X} head: {r[:160].hex(" ")}')
        print(f'{k:08X} tail: {r[-96:].hex(" ")}')
    sys.exit(0)
if len(a) != len(b):
    print('different lengths: compare with dump')
    sys.exit(0)
i = 0
n = 0
while i < len(a):
    if a[i] != b[i]:
        j = i
        while j < len(a) and (a[j] != b[j] or (j + 1 < len(a) and a[j + 1] != b[j + 1]) or (j + 2 < len(a) and a[j + 2] != b[j + 2])):
            j += 1
        sa, sb = a[i:j], b[i:j]
        extra = ''
        if len(sa) == 4:
            extra = f' | u32 {struct.unpack("<I", sa)[0]:#x} / {struct.unpack("<I", sb)[0]:#x} | f {struct.unpack("<f", sa)[0]:.4g} / {struct.unpack("<f", sb)[0]:.4g}'
        print(f'+{i:#06x} ({j - i:3d}): {sa[:24].hex(" ")} | {sb[:24].hex(" ")}{extra}')
        n += 1
        i = j
    else:
        i += 1
print(n, 'differing runs')
