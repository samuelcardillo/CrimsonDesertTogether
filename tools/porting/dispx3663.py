"""dispx3663.py hex [lo hi] : instructions in the 3663 exe's code whose bytes hold the 32-bit displacement/immediate `hex`,
decoded from the start of their function; optional lo..hi limits the search to that rva range. Static."""
import re
import struct
import sys

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
import p3663lib as p  # noqa: E402
import fn3663_funcs as ff  # noqa: E402

D = p.DATA
va, vs, ra, rs, ch = p.CODE
code = D[ra:ra + rs]
val = int(sys.argv[1], 16)
lo = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0
hi = int(sys.argv[3], 16) if len(sys.argv) > 3 else 1 << 40
pat = struct.pack('<I', val)
seen = set()
for m in re.finditer(re.escape(pat), code):
    r = va + m.start()
    if not lo <= r < hi:
        continue
    f = ff.func_of(r)
    if not f:
        continue
    # decode the function up to the hit
    body = p.rd(f[0], f[1] - f[0])
    for ins in p.MD.disasm(body, p.BASE + f[0]):
        a = ins.address - p.BASE
        if a <= r < a + ins.size:
            if a not in seen:
                seen.add(a)
                print(f'fn {f[0]:#x}  {a:#x}: {ins.mnemonic} {ins.op_str}')
            break
        if a > r:
            break
