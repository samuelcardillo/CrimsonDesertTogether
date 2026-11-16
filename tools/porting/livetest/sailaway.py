"""sailaway.py host_pid [heading [seconds_on_course]] : from the Vellua mooring to open water (host at the wheel), looking
at what the ship does after every hold instead of assuming it (sailout.sh assumed "A lowers the heading" and lost a
ship on 16-nov when it did not). One gear (LSHIFT), then the helm in holds of 1.5 to 4 s: after each one the heading
and the position are read; a hold that took the bow further from `heading` (105: east-south-east, open water) flips
which key is used; a ship that neither moves nor turns for three looks gets the gear again once, then the script
gives up ("aground"). Once on course it keeps the bow there for `seconds_on_course` (40), correcting the drift.
Prints one line per look. Exit code 0 on course, 1 otherwise. CDMP_SHIP=<key> when the ship is not 0xB010000A.
Input goes through ui.ps1 (to the game's window only)."""
import math
import os as _os
import subprocess
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
SP = _HERE.replace('/', '\\')
pid = sys.argv[1]
want = float(sys.argv[2]) if len(sys.argv) > 2 else 105.0
keep = float(sys.argv[3]) if len(sys.argv) > 3 else 40.0
TOL = 12.0


def head():
    out = subprocess.run(['python', '-I', _HERE + '/shiphead.py', pid], capture_output=True, text=True).stdout.split()
    if len(out) < 3:
        raise SystemExit('no ship (CDMP_SHIP?)')
    return float(out[0]), float(out[1]), float(out[2])


def hold(key, ms):
    subprocess.run(['pwsh', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', SP + r'\ui.ps1', '-GamePid', pid, '-Do',
                    'hold', '-Keys', key, '-HoldMs', str(int(ms))], capture_output=True, text=True)


def diff(a, b):  # from b to a, in (-180, 180]
    return (a - b + 180.0) % 360.0 - 180.0


h, x, z = head()
print(f'start: bow {h:+.0f} at ({x:.0f}, {z:.0f}), wanted {want:+.0f}', flush=True)
hold('LSHIFT', 1500)
time.sleep(2.0)
key = 'A'          # the first guess; the first hold says whether it was right
sign = {}          # key -> +1 / -1: which way that key turned the heading the last time it was seen to turn
still = 0
geared_again = False
t_on = None
for step in range(60):
    h, x, z = head()
    d = diff(want, h)
    if abs(d) <= TOL:
        if t_on is None:
            t_on = time.time()
            print(f'{step}: bow {h:+.0f} at ({x:.0f}, {z:.0f}): on course', flush=True)
        if time.time() - t_on >= keep:
            print(f'{step}: bow {h:+.0f} at ({x:.0f}, {z:.0f}): kept for {keep:.0f} s', flush=True)
            raise SystemExit(0)
        if abs(d) <= 4:
            time.sleep(2.0)
            h2, x2, z2 = head()
            if math.hypot(x2 - x, z2 - z) < 0.5:
                still += 1
                if still >= 3:
                    print(f'{step}: on course but the ship stands still at ({x2:.0f}, {z2:.0f}): aground or no gear', flush=True)
                    raise SystemExit(1)
            else:
                still = 0
            continue
    # which key takes the bow towards `want`
    for k in ('A', 'D'):
        if k in sign and (sign[k] > 0) == (d > 0):
            key = k
            break
    else:
        if key in sign:  # the key in use is known to go the wrong way and the other is not known yet
            key = 'D' if key == 'A' else 'A'
    secs = min(4.0, max(1.0, abs(d) / 25.0)) if t_on is None else min(1.5, max(0.5, abs(d) / 25.0))
    hold(key, secs * 1000)
    time.sleep(0.4)
    h2, x2, z2 = head()
    turned = diff(h2, h)
    moved = math.hypot(x2 - x, z2 - z)
    print(f'{step}: bow {h:+.0f} ({d:+.0f} to go) at ({x:.0f}, {z:.0f}): {key} {secs:.1f} s -> {h2:+.0f} '
          f'({turned:+.0f}), moved {moved:.1f} m', flush=True)
    if abs(turned) >= 2.0:
        sign[key] = 1 if turned > 0 else -1
        still = 0
    elif moved < 0.5:
        still += 1
        if still >= 3:
            if geared_again:
                print('the ship neither moves nor turns: aground', flush=True)
                raise SystemExit(1)
            geared_again = True
            still = 0
            print('the ship neither moves nor turns: the gear once more', flush=True)
            hold('LSHIFT', 1500)
            time.sleep(2.0)
    else:
        still = 0
        # it moves but this key does not turn it: try the other one
        key = 'D' if key == 'A' else 'A'
print('not on course after 60 looks', flush=True)
raise SystemExit(1)
