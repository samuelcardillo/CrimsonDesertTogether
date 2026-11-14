"""hashrefs.py [hex8 ...] : where the installed exe (1.0.0.3663, on disk) holds each 4-byte value, given as the bytes
appear in a packet (e.g. 89746c86 = the default branch state of every gimmick). For each hit: the section's flags, the
rva, and, in code, the instruction that carries it (found by walking back up to 12 bytes to an instruction boundary
that ends exactly after the value). Read-only; the protected (writable + executable) section is skipped: its bytes on
disk are not the code.

Without arguments: the branch states and gimmick events seen on the ship's sail on 2026-11-14."""
import struct
import sys

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
import p3663lib as L  # noqa: E402

DEFAULT = ['89746c86', 'cf9b0252', '7d8452b9', '4dd95dbe', 'a0f2a008', 'e480ff9b', 'ea7cd220', '3f7594b0',
           '0795c60c', 'f5af9c8c', 'b6bcab80', '59cc3e00', 'a88d73c3', '196a7892', '7561a529', 'e9068b12']


def carrier(rva):
    """The instruction whose last 4 bytes are at rva (an imm32), or None."""
    for back in range(1, 13):
        code = L.rd(rva - back, back + 4)
        if not code:
            continue
        ins = next(L.MD.disasm(code, L.BASE + rva - back), None)
        if ins and ins.size == back + 4:
            return rva - back, f'{ins.mnemonic} {ins.op_str}'
    return None


def main():
    wanted = sys.argv[1:] or DEFAULT
    for h in wanted:
        needle = bytes.fromhex(h)
        hits = []
        for va, vs, ra, rs, ch in L.SECS:
            if ch & 0x20000000 and ch & 0x80000000:
                continue  # the protected section
            blob = L.DATA[ra:ra + rs]
            i = blob.find(needle)
            while i >= 0:
                hits.append((va + i, ch))
                i = blob.find(needle, i + 1)
        print(f'{h} (u32 0x{struct.unpack("<I", needle)[0]:08X}): {len(hits)} hit(s)')
        for rva, ch in hits[:40]:
            kind = 'code' if ch & 0x20000000 else 'data'
            line = f'   {kind} rva 0x{rva:X}'
            if kind == 'code':
                c = carrier(rva)
                line += f'  <- 0x{c[0]:X}: {c[1]}' if c else '  (no instruction ends here)'
            print(line)


if __name__ == '__main__':
    main()
