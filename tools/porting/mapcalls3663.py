"""mapcalls3663.py [rva] : every call of the constant-buffer map routine (0x36bebb0) in the 3663 exe, with the member the
resource comes from and the size (the 12 instructions before the call that load r9 or write [rsp+0x28]). Static."""
import re
import struct
import sys

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
import p3663lib as p  # noqa: E402
import fn3663_funcs as ff  # noqa: E402

TARGET = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x36bebb0
D = p.DATA
va, vs, ra, rs, ch = p.CODE
code = D[ra:ra + rs]
sites = []
for m in re.finditer(rb'\xe8', code):
    s = m.start()
    if s + 5 > len(code):
        break
    rel = struct.unpack_from('<i', code, s + 1)[0]
    if va + s + 5 + rel == TARGET:
        sites.append(va + s)
done = {}
for site in sites:
    f = ff.func_of(site)
    if not f:
        continue
    if f not in done:
        body = p.rd(f[0], f[1] - f[0])
        done[f] = [(i.address - p.BASE, i.mnemonic, i.op_str) for i in p.MD.disasm(body, p.BASE + f[0])]
    ins = done[f]
    k = next((j for j, x in enumerate(ins) if x[0] == site), None)
    if k is None:
        print(f'{site:#x} in fn {f[0]:#x}: not on an instruction boundary')
        continue
    back = ins[max(0, k - 12):k]
    r9 = [x for x in back if x[2].startswith('r9,')]
    size = [x for x in back if 'rsp + 0x28' in x[2]]
    print(f'{site:#x} fn {f[0]:#x}: ' + ' ; '.join(f'{x[1]} {x[2]}' for x in r9[-2:] + size[-1:]))
