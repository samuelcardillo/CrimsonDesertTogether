"""Shared helpers for the 3663 -> 3732 port (read-only on the installed 1.0.0.3732 exe; docs/research/port-3732.md).

  from p3732lib import *
  rd(rva, n)  q(rva)  dis(rva, n)  show(rva, n)      bytes, a pointer, capstone lines, the same printed
  func(rva)  is_start(rva)  starts_between(a, b)    the function (.pdata) an address is in; function starts
  shift_est(old)                                    (shift below, shift above) from the nearest anchors (3663 -> 3732)
  guess(old)                                        old + each of those shifts, and the function starts in between
  near(old, data)  whole(data, old)                 matches of a 3663 byte array, relative operands masked
  operands(rva, n)                                  the relative operands of the code at rva: (rva, kind, target)
  vt3732(vt3663)  vt_name(vt)  slot(vt, k)          a vtable by RTTI name; a vtable's k-th function
  refs(target)  count_refs(target)  callers(fn)     RIP-relative references to a global; direct calls to a function

The 3663 exe is gone: what is known of it is the source's own byte arrays and addresses, data/rtti_3663.json and
data/anchors_3732.json (tools/porting/anchors_3732.py).
"""
import bisect
import json
import re
import struct
import sys

ROOT = r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi'
sys.path.insert(0, ROOT + '/tools/porting')
sys.path.insert(0, ROOT + '/tools/scripts')
from capstone import Cs, CS_ARCH_X86, CS_MODE_64  # noqa: E402
from signature_map import relative_mask  # noqa: E402

EXE = r'D:/SteamLibrary/steamapps/common/Crimson Desert/bin64/CrimsonDesert.exe'
BASE = 0x140000000
DATA = open(EXE, 'rb').read()
_pe = struct.unpack_from('<I', DATA, 0x3c)[0]
if struct.unpack_from('<I', DATA, _pe + 8)[0] != 0x6AFDC0CF:
    raise SystemExit('p3732lib: the installed exe is not 1.0.0.3732')
_n = struct.unpack_from('<H', DATA, _pe + 6)[0]
_opt = struct.unpack_from('<H', DATA, _pe + 20)[0]
SECS = []
for _i in range(_n):
    _o = _pe + 24 + _opt + _i * 40
    _vs, _va, _rs, _ra = struct.unpack_from('<IIII', DATA, _o + 8)
    _ch = struct.unpack_from('<I', DATA, _o + 36)[0]
    SECS.append((_va, _vs, _ra, _rs, _ch))
CODE = next(s for s in SECS if s[4] & 0x20000000 and not s[4] & 0x80000000)  # R-X, not RWX
RWX = next(s for s in SECS if s[4] & 0x20000000 and s[4] & 0x80000000)
MD = Cs(CS_ARCH_X86, CS_MODE_64)
MD.detail = False


def off(rva):
    for va, vs, ra, rs, ch in SECS:
        if va <= rva < va + rs:
            return ra + rva - va
    return None


def rd(rva, n):
    o = off(rva)
    return DATA[o:o + n] if o is not None else None


def q(rva):
    b = rd(rva, 8)
    return struct.unpack('<Q', b)[0] if b else None


def in_code(rva):
    return CODE[0] <= rva < CODE[0] + CODE[3]


def dis(rva, n=40, stop_ret=True):
    out = []
    code = rd(rva, 0x1000)
    for k, ins in enumerate(MD.disasm(code, BASE + rva)):
        out.append((ins.address - BASE, ins.size, ins.mnemonic, ins.op_str, bytes(ins.bytes)))
        if k + 1 >= n or (stop_ret and ins.mnemonic in ('ret', 'int3')):
            break
    return out


def show(rva, n=40, stop_ret=True):
    for a, size, mn, ops, raw in dis(rva, n, stop_ret):
        print(f'  {a:#09x}  {raw.hex(" "):<32} {mn} {ops}')


def rip_target(rva, ins_size, disp_off):
    disp = struct.unpack_from('<i', rd(rva + disp_off, 4))[0]
    return rva + ins_size + disp


# ---------------------------------------------------------------- functions (.pdata)
_dir = struct.unpack_from('<II', DATA, _pe + 24 + 112 + 3 * 8)
_po = off(_dir[0])
FUNCS = sorted({struct.unpack_from('<II', DATA, _po + i)[:2] for i in range(0, _dir[1] - 11, 12)})
FUNCS = [f for f in FUNCS if f[0] < f[1] and in_code(f[0])]
_FS = [f[0] for f in FUNCS]


def func(rva):
    """(begin, end) of the .pdata function that holds rva, or None. A function may have several .pdata chunks."""
    i = bisect.bisect_right(_FS, rva) - 1
    return FUNCS[i] if i >= 0 and FUNCS[i][0] <= rva < FUNCS[i][1] else None


def is_start(rva):
    i = bisect.bisect_left(_FS, rva)
    return i < len(_FS) and _FS[i] == rva


def starts_between(a, b):
    return _FS[bisect.bisect_left(_FS, a):bisect.bisect_right(_FS, b)]


# ---------------------------------------------------------------- anchors and the shift
try:
    _anch = json.load(open(ROOT + '/tools/porting/data/anchors_3732.json'))['anchors']
except OSError:
    _anch = []
_A = sorted((a['old'], a['new']) for a in _anch if a['old'] < 0x5613000 and a['kind'] in ('array', 'call'))
_D = sorted((a['old'], a['new']) for a in _anch if a['old'] >= 0x5613000)


def shift_est(old):
    """(shift of the nearest anchor below, of the nearest above) for a 3663 address; code and data have their own."""
    table = _A if old < 0x5613000 else _D
    keys = [a[0] for a in table]
    i = bisect.bisect_right(keys, old)
    lo = table[i - 1] if i > 0 else None
    hi = table[i] if i < len(table) else None
    return (lo[1] - lo[0] if lo else None), (hi[1] - hi[0] if hi else None)


def guess(old):
    """Where a 3663 code address may be: old + each neighbouring shift, and every function start between them."""
    lo, hi = shift_est(old)
    s = [x for x in (lo, hi) if x is not None]
    if not s:
        return [], []
    a, b = old + min(s), old + max(s)
    return sorted({old + x for x in s}), starts_between(a - 0x40, b + 0x40)


def pattern(data, old_addr=0):
    mask, _, _ = relative_mask(data, old_addr)
    return re.compile(b''.join(re.escape(bytes([b])) if m else b'.' for b, m in zip(data, mask)), re.S)


def near(old, data, window=0x8000, exact=False):
    """Matches of a 3663 byte array around old + the anchors' shift (relative operands masked unless exact),
    nearest first, and the two shifts used."""
    lo, hi = shift_est(old)
    shifts = [s for s in (lo, hi) if s is not None] or [0x2800]
    pat = re.compile(re.escape(bytes(data)), re.S) if exact else pattern(bytes(data), old)
    a = max(CODE[0], old + min(shifts) - window)
    b = min(CODE[0] + CODE[3] - 1, old + max(shifts) + window)
    o1, o2 = off(a), off(b)
    hits = [a + m.start() - o1 for m in pat.finditer(DATA, o1, o2)]
    mid = old + sum(shifts) // len(shifts)
    return sorted(hits, key=lambda h: abs(h - mid)), shifts


def whole(data, old_addr=0, cap=20, exact=False):
    pat = re.compile(re.escape(bytes(data)), re.S) if exact else pattern(bytes(data), old_addr)
    va, vs, ra, rs, ch = CODE
    res = []
    for m in pat.finditer(DATA, ra, ra + rs):
        res.append(va + m.start() - ra)
        if len(res) >= cap:
            break
    return res


def operands(rva, n=64):
    """[(rva of the instruction, 'call'|'jump'|'rip', target rva)] for the n bytes of code at rva."""
    import capstone
    from capstone.x86 import X86_OP_MEM, X86_REG_RIP
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    out = []
    for ins in md.disasm(rd(rva, n), rva):
        if any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in ins.operands):
            disp = struct.unpack_from('<i', bytes(ins.bytes), ins.disp_offset)[0]
            out.append((ins.address, 'rip', ins.address + ins.size + disp))
        if (ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)) and ins.imm_size:
            out.append((ins.address, 'call' if ins.group(capstone.CS_GRP_CALL) else 'jump', ins.operands[0].imm))
    return out


# ---------------------------------------------------------------- vtables by RTTI name
_VT = None


def _vt():
    global _VT
    if _VT is None:
        try:
            _VT = json.load(open(ROOT + '/tools/porting/data/vtable_map_3663_3732.json'))
        except OSError:
            a = json.load(open(ROOT + '/tools/porting/data/rtti_3663.json'))['classes']
            b = json.load(open(ROOT + '/tools/porting/data/rtti_3732.json'))['classes']
            m, names = {}, {}
            for mangled, ca in a.items():
                cb = b.get(mangled)
                if not cb or len(ca['vtables']) != len(cb['vtables']):
                    continue
                for va, vb in zip(ca['vtables'], cb['vtables']):
                    if va['offset'] == vb['offset']:
                        m[va['rva']] = vb['rva']
                        names[vb['rva']] = ca['name'] + (f"+{va['offset']:#x}" if va['offset'] else '')
            _VT = {'map': m, 'names': names}
            json.dump(_VT, open(ROOT + '/tools/porting/data/vtable_map_3663_3732.json', 'w'), indent=0)
    return _VT


def vt3732(vt3663):
    """The 3732 address of the vtable that was at vt3663 (same RTTI class, same sub-object), or None."""
    v = _vt()['map'].get(hex(vt3663))
    return int(v, 16) if v else None


def vt_name(vt3732_rva):
    return _vt()['names'].get(hex(vt3732_rva))


def slot(vt_rva, k):
    p = q(vt_rva + 8 * k)
    return p - BASE if p else None


# ---------------------------------------------------------------- references
def refs(target, cap=5000):
    """Sites in the code section with a RIP-relative operand that points at `target` (any instruction form:
    the four bytes before every possible instruction end are tried, then the instruction is decoded to confirm)."""
    va, vs, ra, rs, ch = CODE
    out = []
    # disp32 followed by 0, 1 or 4 immediate bytes: target = site_of_disp + 4 + tail + disp
    for tail in (0, 1, 4):
        pos = ra
        while len(out) < cap:
            # search every position whose disp32 resolves to target is O(n); do it with a rolling compare
            break
    data = DATA
    end = ra + rs - 8
    i = ra
    find = data.find
    # Fast path: common forms 48/4C/44/40-less mov/lea/cmp with modrm rip (mod=00 rm=101)
    pat = re.compile(rb'(?:[\x40-\x4F])?(?:\x0F[\x10\x11\x28\x29\x2E\x2F\x6F\x7F\xB6\xB7\xBE\xBF]|[\x01\x03\x29\x2B\x39\x3B\x63\x80\x81\x83\x84\x85\x88\x89\x8A\x8B\x8D\xC6\xC7\xFF])[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]', re.S)
    for m in pat.finditer(data, ra, end):
        d = m.end()
        disp = struct.unpack_from('<i', data, d)[0]
        site = va + (m.start() - ra)
        base = va + (d - ra) + 4
        if base + disp == target or base + 1 + disp == target or base + 4 + disp == target:
            ins = next(MD.disasm(data[m.start():m.start() + 16], BASE + site), None)
            if ins and ('rip' in ins.op_str):
                tgt = None
                mm = re.search(r'rip ([+-]) (0x[0-9a-f]+)', ins.op_str)
                if mm:
                    tgt = site + ins.size + (int(mm[2], 16) if mm[1] == '+' else -int(mm[2], 16))
                if tgt == target:
                    out.append(site)
                    if len(out) >= cap:
                        break
    return out


def count_refs(target, cap=5000):
    return len(refs(target, cap))


def callers(fn, cap=2000):
    """Sites of `call rel32` (E8) and `jmp rel32` (E9) in the code section whose target is fn."""
    va, vs, ra, rs, ch = CODE
    out = []
    for op in (b'\xE8', b'\xE9'):
        i = DATA.find(op, ra)
        end = ra + rs - 5
        while 0 <= i < end:
            disp = struct.unpack_from('<i', DATA, i + 1)[0]
            if va + (i - ra) + 5 + disp == fn:
                out.append(va + (i - ra))
                if len(out) >= cap:
                    return out
            i = DATA.find(op, i + 1)
    return sorted(out)
