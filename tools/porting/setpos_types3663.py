"""setpos_types3663.py [rva] : the callers of TransformSync's position setter on 1.0.0.3663 (0xB39E40 by default; give
0xB3A240 for the rotation setter) and the request type each passes (the value put in r8b / r8d before the call, when
it is a constant within the 24 instructions before it). The setter keeps a request only when no pending one has a
LOWER type number (cmp [TS+0x5BC], type ; jb skip): 7 is a vehicle's near request, 12 a character's own movement.
Written on 2026-11-15 to see which types rank above a character's own movement (docs/research/port-3663.md, "Barco,
mañana del 15-nov"). Static, read-only on the installed exe (p3663lib); run with `python -I` from tools/porting."""
import bisect
import re
import struct
import sys

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
import p3663lib as p  # noqa: E402

target = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0xB39E40
D = p.DATA
pe = struct.unpack_from('<I', D, 0x3c)[0]
exc_rva, exc_size = struct.unpack_from('<II', D, pe + 24 + 112 + 3 * 8)
o = p.off(exc_rva)
FUNCS = sorted(struct.unpack_from('<III', D, o + i * 12)[:2] for i in range(exc_size // 12))


def func_of(rva):
    i = bisect.bisect_right(FUNCS, (rva, 0xFFFFFFFF)) - 1
    return FUNCS[i] if i >= 0 and FUNCS[i][0] <= rva < FUNCS[i][1] else None


va, vs, ra, rs, ch = p.CODE
code = D[ra:ra + rs]
sites = []
for m in re.finditer(rb'[\xe8\xe9]', code):
    s = m.start()
    if s + 5 > len(code):
        break
    if va + s + 5 + struct.unpack_from('<i', code, s + 1)[0] == target:
        sites.append((va + s, code[s]))
print(f'{len(sites)} call/jmp sites land on {target:#x}')
by_type = {}
for site, op in sites:
    f = func_of(site)
    if not f:
        print(f'{site:#x}: no function')
        continue
    lines = []
    for ins in p.MD.disasm(p.rd(f[0], site - f[0] + 8), p.BASE + f[0]):
        lines.append((ins.address - p.BASE, ins.mnemonic, ins.op_str))
        if ins.address - p.BASE >= site:
            break
    typ = '?'
    for a, mn, ops in reversed(lines[-25:-1]):
        dst = ops.split(',')[0].strip()
        if dst in ('r8b', 'r8d', 'r8', 'r8w') and mn in ('mov', 'movzx', 'xor', 'lea'):
            src = ops.split(',', 1)[1].strip() if ',' in ops else ''
            if mn == 'xor' and src == dst:
                typ = '0'
            elif mn == 'mov' and re.fullmatch(r'(0x[0-9a-f]+|\d+)', src):
                typ = str(int(src, 0))
            else:
                typ = f'({mn} {ops})'
            break
    by_type.setdefault(typ, []).append((site, f[0], 'call' if op == 0xE8 else 'jmp'))
for typ in sorted(by_type, key=lambda t: (not t.isdigit(), int(t) if t.isdigit() else 0, t)):
    v = by_type[typ]
    print(f'type {typ}: {len(v)} site(s): ' + ', '.join(f'{s:#x} ({k} in {fn:#x})' for s, fn, k in v[:14]) + (' ...' if len(v) > 14 else ''))
