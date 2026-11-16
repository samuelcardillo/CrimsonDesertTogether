"""strx3663.py text [text...] : where the 3663 exe's code takes the address of each string (any rip-relative disp32 that
lands on it), with the function it is in. Also prints 8-byte pointers to the string in the file. Static.
`strx3663.py @0x1234 ...` takes an rva as the target instead of a text."""
import re
import struct
import sys

import numpy as np

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
import p3663lib as p  # noqa: E402
import fn3663_funcs as ff  # noqa: E402

D = p.DATA
va, vs, ra, rs, ch = p.CODE
code = D[ra:ra + rs]


def rva_of(o):
    for sva, svs, sra, srs, sch in p.SECS:
        if sra <= o < sra + srs:
            return sva + o - sra
    return None


targets = {}
for text in sys.argv[1:]:
    if text.startswith('@'):
        targets[int(text[1:], 16)] = text
        continue
    pat = text.encode()
    for m in re.finditer(re.escape(pat), D):
        srva = rva_of(m.start())
        if srva is None:
            continue
        end = D[m.start():m.start() + 80].split(b'\0')[0]
        targets[srva] = f'"{end.decode("latin1")}" (byte before {D[m.start() - 1]:#x})'

want = np.array(sorted(targets), dtype=np.int64)
found = {t: [] for t in targets}
arr = np.frombuffer(code, dtype=np.uint8)
n = len(arr)
CH = 8 << 20
for s in range(0, n - 4, CH):
    e = min(n - 4, s + CH)
    a = arr[s:e + 3].astype(np.int64)
    d = a[:e - s] | (a[1:e - s + 1] << 8) | (a[2:e - s + 2] << 16) | (a[3:e - s + 3] << 24)
    d = np.where(d >= 2**31, d - 2**32, d)
    tgt = d + va + 4 + np.arange(s, e, dtype=np.int64)
    hit = np.nonzero(np.isin(tgt, want))[0]
    for i in hit:
        found[int(tgt[i])].append(va + s + int(i))

for t in sorted(targets):
    print(f'{targets[t]} at {t:#x}')
    for r in found[t]:
        f = ff.func_of(r)
        print(f'    ref disp at {r:#x} in fn {f[0]:#x}..{f[1]:#x}' if f else f'    ref disp at {r:#x} (no fn)')
    q = struct.pack('<Q', p.BASE + t)
    for m2 in re.finditer(re.escape(q), D):
        print(f'    ptr at {rva_of(m2.start()):#x}')
