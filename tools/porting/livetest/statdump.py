"""statdump.py pid : the stat entries of the copy's own character, as the mod's PlayerReader walks them
(src/mod/game/player_reader.cpp: [[comps+0x20]+0x18] -> entries at +0x58, count at +0x60, 0x90 bytes each: +0 id,
+8 current, +0x10 regen, +0x18 max, int64 values in thousandths). Read-only.

Why: on 1.0.0.3663 the reader finds health (id 0: 1650/1650) but not stamina (id 22 in the builds up to 3576: the
panel shows "Stamina 0/0"). The entry whose max is a small round number and whose current drops while sprinting is
the one; run this standing still and again right after a sprint."""
import os as _os
import sys

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
r = Reader(int(sys.argv[1]))
world = r.ptr(B + WORLD_SLOT)
mgr = r.ptr(world + 0x30)
actor = r.ptr(mgr + 0x50)
comps = r.ptr(actor + 0x68)
status = r.ptr(comps + 0x20)
container = r.ptr(status + 0x18) if status else 0
entries = r.ptr(container + 0x58) if container else 0
count = r.unpack(container + 0x60, '<I')[0] if container else 0
print(f'actor {actor:#x} status {status:#x} (vtable rva {r.ptr(status) - B:#x}) container {container:#x} '
      f'entries {entries:#x} count {count}')
if not entries or not count or count > 256:
    raise SystemExit('the stat container is not where the reader expects it')
for i in range(count):
    e = entries + i * 0x90
    sid, cur, regen, mx = r.unpack(e, '<Qqqq')
    mark = '  <- health (id 0)' if sid == 0 else '  <- stamina in builds up to 3576 (id 22)' if sid == 22 else ''
    print(f'  [{i:2d}] id {sid:<6d} current {cur / 1000:10.1f} max {mx / 1000:10.1f} regen {regen / 1000:8.2f}{mark}')
