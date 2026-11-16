"""helmprompts.py host_pid guest_pid [looks] : are the wheel's controls offered to the host, and who controls the sail
on the guest's copy at that moment? Each look: a screenshot of the host's prompt list (bottom right of its window,
ui.ps1 shot with UI_CROP) in which the rows of white text are counted (five at the wheel: Start Underwater
Detection, Aim Portside, Dismount, Accelerate, Decelerate; one when only the first is offered), the sail's gear on
the host, and on the guest the sail's controller (the guest's own user while gimmick_take holds it). Written on
16-nov, when ALT and LSHIFT did nothing at the wheel for minutes: if the rows go when the guest holds the sail, the
take is what removes them. CDMP_SHIP=<key> for another ship. Read-only but for bringing the host's window to the
front."""
import os as _os
import subprocess
import sys
import time

from PIL import Image

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

SP = _HERE.replace('/', '\\')
H, G = sys.argv[1], sys.argv[2]
looks = int(sys.argv[3]) if len(sys.argv) > 3 else 20
B = 0x140000000
NAMES = {'89746c86': 'furled', '4dd95dbe': 'first gear', 'e480ff9b': 'second gear', '3f7594b0': 'third gear'}


def table(r):
    world = r.ptr(B + WORLD_SLOT)
    mgr = r.ptr(world + 0x30)
    t = r.ptr(mgr + 8)
    user = r.ptr(mgr + 0x58)
    return t, (r.unpack(user + 0x60, '<I')[0] if user else 0)


def actor(r, t, key):
    n = r.unpack(t + 0x88, '<I')[0]
    b = r.ptr(t + 0x98) + (key % n) * 0x100
    for i in range(min(r.unpack(b, '<I')[0], 31)):
        k, idx = r.unpack(b + 8 + i * 8, '<II')
        if k == key:
            return r.ptr(r.ptr(r.ptr(t + 0xA0) + idx * 8) + 8)
    return 0


def rows(path):
    """Rows of near-white text in the crop: runs of image lines that hold at least 25 near-white pixels."""
    im = Image.open(path).convert('L')
    w, h = im.size
    px = im.load()
    n = 0
    inside = False
    gap = 0
    for y in range(h):
        white = sum(1 for x in range(0, w, 2) if px[x, y] >= 235)
        if white >= 12:
            if not inside:
                n += 1
            inside = True
            gap = 0
        else:
            gap += 1
            if gap >= 6:
                inside = False
    return n


key = int(subprocess.run(['python', '-I', _HERE + '/sailstate.py', H, 'key'], capture_output=True, text=True).stdout.split()[0], 16)
hr, gr = Reader(int(H)), Reader(int(G))
env = dict(_os.environ, UI_CROP='2300,1500,1540,660')
shot = SP + r'\shots\helm_tmp.png'
few = 0
for i in range(looks):
    subprocess.run(['pwsh', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', SP + r'\ui.ps1', '-GamePid', H, '-Do', 'shot',
                    '-Out', shot], capture_output=True, text=True, env=env)
    ht, _ = table(hr)
    gt, guser = table(gr)
    ha, ga = actor(hr, ht, key), actor(gr, gt, key)
    hs = hr.read(hr.ptr(hr.ptr(ha + 0x68) + 0x30) + 0x270, 4).hex() if ha else 'none'
    gctl = gr.unpack(ga + 0x90, '<I')[0] if ga else 0
    hctl = hr.unpack(ha + 0x90, '<I')[0] if ha else 0
    n = rows(shot)
    few += 1 if n < 4 else 0
    print(f'{time.strftime("%H:%M:%S")} prompt rows {n} | host: sail {NAMES.get(hs, hs)}, its controller {hctl:08X} | guest: the '
          f'sail\'s controller {gctl:08X}' + (' (the guest holds it)' if gctl == guser else ''), flush=True)
    time.sleep(1.0)
print(f'{looks} looks: the wheel\'s controls were missing in {few}')
