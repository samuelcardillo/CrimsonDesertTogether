"""hier_writers3663.py : where 1.0.0.3663's code writes a byte at [r+0x18] and a dword at [r+0x1C] of the same register
within 40 bytes (the hierarchy component's type and parent key): 138 sites on 2026-11-15, among them 0x187BBEC inside
0x187B960, Hierarchy::SetParent (docs/research/port-3663.md, "Barco, mañana del 15-nov"); the four at 0x247D18B...
are constructors (type 6, parent 0). Prints the sites; fn3663.py shows each. Static, read-only (p3663lib); `python -I`."""
import re
import sys

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
import p3663lib as p  # noqa: E402

va, vs, ra, rs, ch = p.CODE
code = p.DATA[ra:ra + rs]
print(f'code section rva {va:#x} size {rs:#x}')
# mov [reg+1Ch], r32 : (REX)? 89 modrm(01 rrr bbb, bbb != 100) 1C ; mov byte [reg+18h], r8 : (REX)? 88 modrm 18 ;
# mov byte [reg+18h], imm8 : (REX)? C6 modrm(01 000 bbb) 18 ib
hits = []
for m in re.finditer(rb'\x89[\x40-\x7f]\x1c', code, re.S):
    modrm = code[m.start() + 1]
    if modrm & 7 == 4:
        continue
    base = modrm & 7
    rex = code[m.start() - 1] if m.start() and 0x40 <= code[m.start() - 1] <= 0x4F else 0
    baseb = base | (8 if rex & 1 else 0)
    lo, hi = max(0, m.start() - 40), m.start() + 40
    win = code[lo:hi]
    ok = False
    for n in re.finditer(rb'(\x88[\x40-\x7f]\x18|\xc6[\x40-\x47]\x18.)', win, re.S):
        mr = win[n.start() + 1]
        if mr & 7 == 4:
            continue
        r2 = win[n.start() - 1] if n.start() and 0x40 <= win[n.start() - 1] <= 0x4F else 0
        if ((mr & 7) | (8 if r2 & 1 else 0)) == baseb:
            ok = True
            break
    if ok:
        hits.append(va + m.start())
print(len(hits), 'sites')
for h in hits[:400]:
    print(f'{h:#x}')
