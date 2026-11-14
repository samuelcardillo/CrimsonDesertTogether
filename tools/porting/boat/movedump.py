"""movedump.py capture.bin key [t_from t_to] [max] : the MoveActorAck packets of one actor (3527..3663 layout, 239
bytes), decoded: capture time, parent (u64 at +68; ffff.. = none), local position (+4), world position (+76), and
whether the packet is blank (all zeros after the key). For several keys: key_lo-key_hi. Read-only."""
import mmap
import re
import struct
import sys

ROOT = r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi'
REC = struct.Struct("<QBBHHHII")  # t_us dir pad size copied pad session thread
native = None
on = False
for line in open(ROOT + '/src/common/native_opcode_table.inc', encoding='utf-8'):
    if line.startswith('constexpr'):
        on = 'kEntries3663' in line
        continue
    mm = re.match(r'\s*\{(\d+), (\d+), \d+U, "MoveActorAck"\}', line)
    if mm and on:
        native = int(mm.group(2))
path = sys.argv[1]
ks = sys.argv[2].split('-')
lo, hi = int(ks[0], 16), int(ks[-1], 16)
ta = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
tb = float(sys.argv[4]) if len(sys.argv) > 4 else 1e12
mx = int(sys.argv[5]) if len(sys.argv) > 5 else 60
f = open(path, 'rb')
m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
pos = 12
t0 = None
shown = 0
while pos + REC.size <= len(m) and shown < mx:
    t, d, _, size, copied, _, sess, thread = REC.unpack_from(m, pos)
    pos += REC.size
    pkt = m[pos:pos + copied]
    pos += copied
    if t0 is None:
        t0 = t
    tt = (t - t0) / 1e6
    if tt > tb:
        break
    if tt < ta or len(pkt) != 5 + 239 or struct.unpack_from('<H', pkt, 0)[0] != native:
        continue
    p = bytes(pkt[5:])
    key = struct.unpack_from('<I', p, 0)[0]
    if not (lo <= key <= hi):
        continue
    parent = struct.unpack_from('<Q', p, 68)[0]
    lp = struct.unpack_from('<3f', p, 4)
    wp = struct.unpack_from('<3f', p, 76)
    ws = struct.unpack_from('<2h', p, 88 + 16) if False else (0, 0)
    blank = not any(p[4:])
    par = 'none' if parent == 0xFFFFFFFFFFFFFFFF else f'{parent:08X}'
    print(f't={tt:9.3f} {key:08X} parent {par:>8s} local ({lp[0]:9.2f},{lp[1]:8.2f},{lp[2]:9.2f}) '
          f'world ({wp[0]:9.2f},{wp[1]:8.2f},{wp[2]:9.2f}){" BLANK" if blank else ""}')
    shown += 1
