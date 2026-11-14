"""cr_extract.py capture.bin out.stream : for cr_replay.cpp, the packets create_retry listens to (ActorLoadDataAck 2005, RemoveActorAck 2516,
MoveFieldAck 2174; native opcodes of build 3663), as [u64 t_us][u16 logical][u8 dir][u8 truncated][u32 len][payload].
Read-only on the capture."""
import mmap
import re
import struct
import sys

ROOT = r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi'
REC = struct.Struct("<QBBHHHII")  # t_us dir pad size copied pad session thread (tools/scripts/analyze_capture.py)
WANT = {'ActorLoadDataAck': 2005, 'RemoveActorAck': 2516, 'MoveFieldAck': 2174}
native = {}
on = False
for line in open(ROOT + '/src/common/native_opcode_table.inc', encoding='utf-8'):
    if line.startswith('constexpr'):
        on = 'kEntries3663' in line
        continue
    mm = re.match(r'\s*\{(\d+), (\d+), \d+U, "(\w+)"\}', line)
    if mm and on and mm.group(3) in WANT:
        assert int(mm.group(1)) == WANT[mm.group(3)], line
        native[int(mm.group(2))] = int(mm.group(1))
assert len(native) == 3, native

f = open(sys.argv[1], 'rb')
m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
out = open(sys.argv[2], 'wb')
pos = 12
t0 = None
count = {}
trunc = 0
last = 0
while pos + REC.size <= len(m):
    t, d, _, size, copied, _, sess, thread = REC.unpack_from(m, pos)
    pos += REC.size
    if t0 is None:
        t0 = t
    if copied >= 5:
        op = struct.unpack_from('<H', m, pos)[0]
        logical = native.get(op)
        if logical:
            payload = m[pos + 5:pos + copied]
            cut = 1 if copied < size else 0
            trunc += cut
            out.write(struct.pack('<QHBBI', t - t0, logical, d, cut, len(payload)))
            out.write(payload)
            count[(logical, d)] = count.get((logical, d), 0) + 1
    pos += copied
    last = t - t0
out.close()
print(f'{sys.argv[1].split("/")[-1]}: {last / 1e6:.0f} s; (logical, dir) -> packets {sorted(count.items())}; truncated {trunc}')
