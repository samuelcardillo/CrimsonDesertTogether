"""pitch.py pid degrees : tilt a game copy's camera to a pitch (negative: looking down), reading it from memory and
moving the mouse through ui.ps1 until it is within a degree. The mouse looks of ui.ps1 are relative, so a script that
only adds "look -Dy 20" ends lower every time it is run again; this sets the tilt itself. (Written on 15-nov for
Hopkins' stand, where the prompt went to a wheat sack instead of to him; a tilt of -7 degrees did not bring his
prompt back there, so the tilt alone is not what decides it.)"""
import math
import os as _os
import subprocess
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import CAMERA_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
pid, want = int(sys.argv[1]), float(sys.argv[2])
r = Reader(pid)
SP = _HERE.replace('/', '\\')


def pitch():
    blk = r.ptr(r.ptr(B + CAMERA_SLOT) + 0x430)
    fy = r.unpack(blk + 0x94, '<f')[0]
    return math.degrees(math.asin(max(-1.0, min(1.0, fy))))


for i in range(6):
    have = pitch()
    d = want - have
    print(f'{i} pitch {have:+.1f} want {want:+.1f}')
    if abs(d) < 1.0:
        break
    # about ten pixels of mouse a degree; down on the screen is down for the camera
    subprocess.run(['pwsh', '-NoProfile', '-File', SP + r'\ui.ps1', '-GamePid', str(pid), '-Do', 'look', '-Dx', '0', '-Dy',
                    str(int(-d * 8)), '-Steps', '4'], capture_output=True, text=True)
    time.sleep(0.4)
