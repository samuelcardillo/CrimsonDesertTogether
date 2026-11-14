"""branchfind.py host_pid guest_pid key_hex [hash_hex ...] : where a gimmick's branch state lives (3663, read-only).
For the actor on both copies: its components (actor+0x68 slots) with their RTTI class names, and every place in each
component object (first 0x1000 bytes, and one pointer level down, 0x400 bytes) that holds one of the known branch
hashes (default: the sail's, from the capture of 2026-11-14: 89746c86 furled, 7d8452b9, 4dd95dbe, a0f2a008, e480ff9b,
ea7cd220, 3f7594b0, 0795c60c, f5af9c8c, b6bcab80). Hashes are given as the packet shows them (little-endian bytes)."""
import json
import os
import struct
import sys
_HERE = os.path.dirname(os.path.abspath(__file__)).replace('\\', '/')
R = r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/'
sys.path.insert(0, R + 'tools/porting')
sys.path.insert(0, _HERE + '/../livetest')
from live_reader_check import Reader
from cdbuild import WORLD_SLOT

BASE = 0x140000000
rt = json.load(open(R + 'tools/porting/data/rtti_3663.json', encoding='utf-8'))
names = {}
for c in rt['classes'].values() if isinstance(rt.get('classes'), dict) else rt.get('classes', []):
    for v in c.get('vtables', []):
        names[int(v['rva'], 0)] = c['name']
key = int(sys.argv[3], 16)
hashes = sys.argv[4:] or ['89746c86', '7d8452b9', '4dd95dbe', 'a0f2a008', 'e480ff9b', 'ea7cd220', '3f7594b0',
                          '0795c60c', 'f5af9c8c', 'b6bcab80']
pats = {h: bytes.fromhex(h) for h in hashes}


def scan(pid, label):
    r = Reader(pid)

    def rp(a):
        try:
            return r.ptr(a)
        except Exception:
            return 0

    def rd(a, n):
        try:
            return r.read(a, n)
        except Exception:
            return b''

    def cls(p):
        vt = rp(p)
        return names.get(vt - BASE) if vt else None
    world = rp(BASE + WORLD_SLOT); mgr = rp(world + 0x30); t = rp(mgr + 8)
    n = r.unpack(t + 0x88, '<I')[0]
    b = rp(t + 0x98) + (key % n) * 0x100
    c = r.unpack(b, '<I')[0]
    a = 0
    for i in range(min(c, 31)):
        k, idx = r.unpack(b + 8 + i * 8, '<II')
        if k == key:
            a = rp(rp(rp(t + 0xA0) + idx * 8) + 8)
    if not a:
        print(label, 'actor not in the table')
        return
    comps = rp(a + 0x68)
    print(f'== {label}: actor {a:#x} {cls(a)} comps {comps:#x}')
    for off in range(0, 0x300, 8):
        p = rp(comps + off)
        if not (0x10000 < p < 0x7fffffffffff):
            continue
        nm = cls(p)
        if not nm:
            continue
        buf = rd(p, 0x1000)
        hits = []
        for h, pat in pats.items():
            i = buf.find(pat)
            while i >= 0:
                hits.append(f'+{i:#x}={h}')
                i = buf.find(pat, i + 1)
        sub = []
        for q in range(8, min(len(buf), 0x400), 8):
            pp = struct.unpack_from('<Q', buf, q)[0]
            if 0x10000 < pp < 0x7fffffffffff and pp != p:
                sb = rd(pp, 0x400)
                for h, pat in pats.items():
                    i = sb.find(pat)
                    while i >= 0:
                        sub.append(f'[+{q:#x}]+{i:#x}={h}({cls(pp) or "?"})')
                        i = sb.find(pat, i + 1)
        if hits or sub:
            print(f'  comps+{off:#05x} {p:#x} {nm}: {" ".join(hits)} {" ".join(sub[:12])}')
        else:
            print(f'  comps+{off:#05x} {nm}')


scan(int(sys.argv[1]), 'host')
scan(int(sys.argv[2]), 'guest')
