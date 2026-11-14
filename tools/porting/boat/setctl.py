"""setctl.py pid key_hex [own | controller_hex] [--write] : EXPERIMENT (3663): an actor's controller on one copy.
Without --write it only prints: the copy's own user key ([mgr+0x18]... read through the own actor's controller), the
actor's controller (actor+0x90) and its hierarchy parent. With --write it stores the given controller ('own' = this
copy's own user) into actor+0x90 and prints before/after.

Why (2026-11-14, the sails): a client runs a gimmick's state machine only when its own user is the gimmick's controller
(docs/research/s25/gimmicks-dueno.md). The host's sail on the guest has the host as controller, so the guest's client
never changes its branch. If the sail's branch is LINKED to the vehicle's state (GameData_GimmickLinkedState_BranchData)
and that state reaches the guest (its buffs do), making the guest the sail's controller on its own copy may be all it
takes. This writes game memory on a TEST copy: back up the saves first (the launcher does)."""
import ctypes
import os
import struct
import sys
_HERE = os.path.dirname(os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
sys.path.insert(0, _HERE + '/../livetest')
from live_reader_check import Reader
from cdbuild import WORLD_SLOT

B = 0x140000000
args = [a for a in sys.argv[1:] if a != '--write']
write = '--write' in sys.argv[1:]
pid, key = int(args[0]), int(args[1], 16)
want = args[2] if len(args) > 2 else None
r = Reader(pid)
world = r.ptr(B + WORLD_SLOT); mgr = r.ptr(world + 0x30); t = r.ptr(mgr + 8)
own_actor = r.ptr(mgr + 0x50)
own_user = r.unpack(own_actor + 0x90, '<I')[0]
n = r.unpack(t + 0x88, '<I')[0]
b = r.ptr(t + 0x98) + (key % n) * 0x100
c = r.unpack(b, '<I')[0]
a = 0
for i in range(min(c, 31)):
    k, idx = r.unpack(b + 8 + i * 8, '<II')
    if k == key:
        a = r.ptr(r.ptr(r.ptr(t + 0xA0) + idx * 8) + 8)
if not a:
    print(f'{key:#010x} is not in the actor table of {pid}')
    sys.exit(1)
if r.unpack(a + 0x60, '<I')[0] != key:
    print('the actor found does not carry that key: nothing done')
    sys.exit(1)
ctl = r.unpack(a + 0x90, '<I')[0]
comps = r.ptr(a + 0x68)
h = r.ptr(comps + 0x1B8) if comps else 0
par = r.unpack(h + 0x1C, '<I')[0] if h else 0
print(f'pid {pid}: own user {own_user:#010x} | actor {key:#010x} at {a:#x}: controller {ctl:#010x} parent {par:#010x}')
if not write or want is None:
    sys.exit(0)
new = own_user if want == 'own' else int(want, 16)
if (new >> 28) != 0x9:
    print(f'{new:#010x} is not a user key (0x9...): nothing written')
    sys.exit(1)
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
hp = k32.OpenProcess(0x0020 | 0x0008 | 0x0010, False, pid)
nw = ctypes.c_size_t()
data = struct.pack('<I', new)
ok = k32.WriteProcessMemory(ctypes.c_void_p(hp), ctypes.c_void_p(a + 0x90), data, len(data), ctypes.byref(nw))
k32.CloseHandle(hp)
print(f'write {bool(ok)} ({nw.value} bytes): controller now {r.unpack(a + 0x90, "<I")[0]:#010x}')
