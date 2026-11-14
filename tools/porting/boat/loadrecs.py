"""loadrecs.py capture.bin key_lo key_hi [t_from t_to] : every actor record of the ActorLoadDataAck batches in a .bin
capture whose key is in [key_lo, key_hi] (hex): capture time, the batch's size and thread, the record's type, key,
length, and its family (parent, children) read from the record's tail (tools/scripts/analyze_capture.py). Also every
RemoveActorAck-like packet (any op) of at most 64 bytes that names one of those keys, to see what goes away.
Read-only. The capture is the sniffer's own (bin64/cdmp_packets.<role>_*.bin)."""
import collections
import mmap
import struct
import sys
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/scripts')
from analyze_capture import REC, actor_records, record_family

path = sys.argv[1]
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
ta = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
tb = float(sys.argv[5]) if len(sys.argv) > 5 else 1e12
f = open(path, 'rb')
m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
assert m[:4] == b'CDPK'
pos = 12
t0 = None
seen = collections.Counter()
small = collections.Counter()
while pos + REC.size <= len(m):
    t, d, _, size, copied, _, sess, thread = REC.unpack_from(m, pos)
    pos += REC.size
    pkt = m[pos:pos + copied]
    pos += copied
    if t0 is None:
        t0 = t
    tt = (t - t0) / 1e6
    if tt < ta or len(pkt) < 12:
        continue
    if tt > tb:
        break
    op, slot, plen = struct.unpack_from('<HbH', pkt, 0)
    payload = pkt[5:]
    if d == 2 and len(payload) <= 64:
        for i in range(0, len(payload) - 3):
            k = struct.unpack_from('<I', payload, i)[0]
            if lo <= k <= hi and op not in (3008, 2697):
                small[(op, k)] += 1
                break
    if d != 2 or len(payload) < 600:
        continue
    recs = actor_records(payload)
    if not recs or sum(len(r) + 2 for r in recs) != len(payload):
        continue
    hit = [r for r in recs if len(r) >= 5 and lo <= struct.unpack_from('<I', r, 1)[0] <= hi]
    if not hit:
        continue
    print(f't={tt:9.3f} op {op} size {size} copied {copied} thread {thread}: {len(recs)} record(s)')
    for r in recs:
        k = struct.unpack_from('<I', r, 1)[0]
        fam = record_family(r) if r[0] else None
        mark = '*' if lo <= k <= hi else ' '
        seen[k] += 1
        fs = f'parent {fam[0]:08X} kids {[f"{c:08X}" for c in fam[1]]}' if fam else 'no family read'
        print(f'   {mark} type {r[0]} key {k:08X} len {len(r)} {fs}')
print()
print('small S>C packets naming those keys (op, key): count')
for (op, k), n in sorted(small.items()):
    print(f'   op {op} key {k:08X}: {n}')
