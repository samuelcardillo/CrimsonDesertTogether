"""nearmem.py pid : the memory right below a game copy's exe image (the last 16 MB before 0x140000000) and right
above it, region by region: where the trampolines of the mod's hooks live. The mod takes three 64 KB blocks there at
its start (game::reserve_trampoline_memory, src/mod/game/hook_prepare.h); a committed, private, executable region
that is not one of them is a block a later hook had to find by itself, or somebody else's. Read-only
(VirtualQueryEx)."""
import ctypes
import ctypes.wintypes as wt
import sys

PROCESS_QUERY_INFORMATION = 0x0400
MEM_COMMIT, MEM_RESERVE, MEM_FREE, MEM_PRIVATE = 0x1000, 0x2000, 0x10000, 0x20000
PROT = {0x10: 'X', 0x20: 'RX', 0x40: 'RWX', 0x80: 'WCX', 0x02: 'R', 0x04: 'RW', 0x08: 'WC', 0x01: '-'}


class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_ulonglong), ('AllocationBase', ctypes.c_ulonglong),
                ('AllocationProtect', wt.DWORD), ('PartitionId', wt.WORD), ('RegionSize', ctypes.c_ulonglong),
                ('State', wt.DWORD), ('Protect', wt.DWORD), ('Type', wt.DWORD)]


k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.OpenProcess.restype = wt.HANDLE
k32.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_ulonglong, ctypes.POINTER(MBI), ctypes.c_size_t]
k32.VirtualQueryEx.restype = ctypes.c_size_t
h = k32.OpenProcess(PROCESS_QUERY_INFORMATION, False, int(sys.argv[1]))
if not h:
    raise SystemExit('cannot open the process')
BASE = 0x140000000


def walk(lo, hi):
    out = []
    a = lo
    while a < hi:
        m = MBI()
        if not k32.VirtualQueryEx(h, a, ctypes.byref(m), ctypes.sizeof(m)):
            break
        out.append(m)
        a = m.BaseAddress + m.RegionSize
    return out


below = walk(BASE - (16 << 20), BASE)
exec_private = [m for m in below if m.State == MEM_COMMIT and m.Type == MEM_PRIVATE and (m.Protect & 0xF0)]
free = sum(m.RegionSize for m in below if m.State == MEM_FREE)
print(f'the 16 MB below the image: {len(below)} regions, {free >> 10} KB free, {len(exec_private)} committed private '
      f'executable region(s):')
for m in exec_private:
    print(f'  0x{m.BaseAddress:X} +0x{m.RegionSize:X} {PROT.get(m.Protect & 0xFF, hex(m.Protect))} (allocation base '
          f'0x{m.AllocationBase:X}, {(BASE - m.BaseAddress) >> 10} KB below the image)')
k32.CloseHandle(h)
