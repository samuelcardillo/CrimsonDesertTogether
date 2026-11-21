"""seacmp.py host_pid guest_pid [seconds] : the three constant buffers of the sea (1.0.0.3663) on both copies, word by
word: which words differ between the copies and which change with time. They are statics of the exe that the sea's
update (0x3fcf410) copies to the GPU every frame: exe+0x75ACF30 (0x410 bytes), exe+0x75AD350 (0x770 bytes,
"OceanWaveConstantBuffer": 48 waves of 0x20 bytes, then the rest) and exe+0x6F4CBD0 (0x50 bytes). For "the sea washes
over the ship on the guest" (16-nov): the hull is in the same place on both, so the waves are not. Read-only."""
import os as _os
import struct
import sys
import time

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from live_reader_check import Reader  # noqa: E402

# 3732 (2026-11-21; 3663: 0x75ACF30, 0x75AD350, 0x6F4CBD0, update 0x3FCF410): the wave buffer is the operand of the
# same instruction of the fill routine (0x3FD2C40); the grid is 0x420 before it as it was; the third one is the
# update's (0x3FD3900) nearest operand and is NOT checked: look at what it holds before trusting it.
BUFS = (('grid', 0x75B3170, 0x410), ('wave', 0x75B3590, 0x770), ('misc', 0x6F52BA0, 0x50))


def base(r):
    return r.base if hasattr(r, 'base') else 0x140000000


def take(r):
    return {n: r.read(base(r) + a, s) for n, a, s in BUFS}


def words(b):
    return struct.unpack(f'<{len(b) // 4}I', b)


def f(u):
    v = struct.unpack('<f', struct.pack('<I', u))[0]
    return f'{v:.5g}' if (u == 0 or 1e-6 < abs(v) < 1e9) else f'0x{u:08X}'


h, g = Reader(int(sys.argv[1])), Reader(int(sys.argv[2]))
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0
h0, g0 = take(h), take(g)
time.sleep(secs)
h1, g1 = take(h), take(g)
for n, a, s in BUFS:
    wh0, wg0, wh1, wg1 = words(h0[n]), words(g0[n]), words(h1[n]), words(g1[n])
    print(f'--- {n} (exe+{a:#x}, {s:#x} bytes)')
    for i in range(len(wh0)):
        moves_h, moves_g, differs = wh0[i] != wh1[i], wg0[i] != wg1[i], wh1[i] != wg1[i]
        if not (moves_h or moves_g or differs):
            continue
        tag = ('DIFFERS ' if differs else 'same    ') + ('moves' if (moves_h or moves_g) else 'still')
        print(f'  +{i * 4:#05x} {tag}  host {f(wh0[i]):>12} -> {f(wh1[i]):>12}   guest {f(wg0[i]):>12} -> {f(wg1[i]):>12}')
