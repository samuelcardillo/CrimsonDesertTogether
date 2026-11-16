"""fn3663_funcs.py: the function table (.pdata) of the installed 1.0.0.3663 exe, for other scripts: `func_of(rva)`
gives (begin, end) of the function that holds rva, or None. Static, read-only (p3663lib)."""
import bisect
import struct
import sys

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
import p3663lib as p  # noqa: E402

_D = p.DATA
_pe = struct.unpack_from('<I', _D, 0x3c)[0]
_exc_rva, _exc_size = struct.unpack_from('<II', _D, _pe + 24 + 112 + 3 * 8)
_o = p.off(_exc_rva)
FUNCS = []
for _i in range(_exc_size // 12):
    _b, _e, _u = struct.unpack_from('<III', _D, _o + _i * 12)
    if _b:
        FUNCS.append((_b, _e))
FUNCS.sort()


def func_of(rva):
    i = bisect.bisect_right(FUNCS, (rva, 0xFFFFFFFF)) - 1
    if i >= 0 and FUNCS[i][0] <= rva < FUNCS[i][1]:
        return FUNCS[i]
    return None
