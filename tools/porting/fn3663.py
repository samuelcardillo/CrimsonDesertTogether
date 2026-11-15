"""fn3663.py rva [n] : disassemble the installed 1.0.0.3663 exe from the start of the function that holds `rva` (by
.pdata) for n instructions (60). `fn3663.py rva around k`: k instructions either side of rva, aligned from the
function's start. `fn3663.py callers rva`: the call and jmp sites (E8 / E9 rel32) that land on rva, with the function
each is in. Static, read-only (p3663lib). Run it with `python -I` from tools/porting. Written on 2026-11-15 to find who
sets a character's hierarchy parent: `fn3663.py callers 0x187b960` (Hierarchy::SetParent, docs/research/port-3663.md)."""
import struct
import sys

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
import p3663lib as p  # noqa: E402

D = p.DATA
pe = struct.unpack_from('<I', D, 0x3c)[0]
opt = pe + 24
# exception directory (index 3) of PE32+
exc_rva, exc_size = struct.unpack_from('<II', D, opt + 112 + 3 * 8)
o = p.off(exc_rva)
FUNCS = []
for i in range(exc_size // 12):
    b, e, u = struct.unpack_from('<III', D, o + i * 12)
    if b:
        FUNCS.append((b, e))
FUNCS.sort()


def func_of(rva):
    import bisect
    i = bisect.bisect_right(FUNCS, (rva, 0xFFFFFFFF)) - 1
    if i >= 0 and FUNCS[i][0] <= rva < FUNCS[i][1]:
        return FUNCS[i]
    return None


def show(start, n, mark=None, end=None):
    code = p.rd(start, 0x4000)
    k = 0
    for ins in p.MD.disasm(code, p.BASE + start):
        a = ins.address - p.BASE
        if end and a >= end:
            break
        print(f"{'>' if a == mark else ' '} {a:#x}: {ins.mnemonic} {ins.op_str}")
        k += 1
        if k >= n:
            break


if sys.argv[1] == 'callers':
    target = int(sys.argv[2], 16)
    va, vs, ra, rs, ch = p.CODE
    code = D[ra:ra + rs]
    import re
    out = []
    for m in re.finditer(rb'[\xe8\xe9]', code):
        s = m.start()
        if s + 5 > len(code):
            break
        rel = struct.unpack_from('<i', code, s + 1)[0]
        if va + s + 5 + rel == target:
            f = func_of(va + s)
            out.append((va + s, code[s], f))
    for site, op, f in out:
        print(f"{site:#x} {'call' if op == 0xE8 else 'jmp '} in function {f[0]:#x}..{f[1]:#x}" if f else f'{site:#x} (no function)')
    raise SystemExit(0)

rva = int(sys.argv[1], 16)
f = func_of(rva)
if len(sys.argv) > 2 and sys.argv[2] == 'around':
    k = int(sys.argv[3])
    # walk from the function start to keep instruction alignment
    start = f[0] if f else rva - 0x40
    code = p.rd(start, max(0x4000, rva - start + 0x400))
    lines = []
    for ins in p.MD.disasm(code, p.BASE + start):
        a = ins.address - p.BASE
        lines.append((a, f'{ins.mnemonic} {ins.op_str}'))
        if a > rva + 0x200:
            break
    idx = next((i for i, l in enumerate(lines) if l[0] >= rva), len(lines) - 1)
    print(f'function {f[0]:#x}..{f[1]:#x}' if f else 'no function')
    for a, t in lines[max(0, idx - k):idx + k]:
        print(f"{'>' if a == rva else ' '} {a:#x}: {t}")
else:
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    print(f'function {f[0]:#x}..{f[1]:#x}' if f else 'no function')
    show(f[0] if f else rva, n, rva, f[1] if f else None)
