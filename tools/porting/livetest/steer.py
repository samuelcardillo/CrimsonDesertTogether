"""steer.py pid heading [tolerance] : turn the ship the copy's character is steering until its bow points to
`heading` (degrees as shiphead.py gives them: 90 = +x, the east) within `tolerance` (15). The host must be at the
wheel and the ship under way (it does not turn standing still). It first learns which of A and D raises the heading
(a two-second hold of D), then holds the right key in bursts sized by the turn rate it measured, looking at the
heading after each. Prints the heading as it goes; exit code 0 when it got there. Input goes through ui.ps1 (to the
game's window only)."""
import math
import os as _os
import subprocess
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
SP = _HERE.replace('/', '\\')
pid, want = sys.argv[1], float(sys.argv[2])
tol = float(sys.argv[3]) if len(sys.argv) > 3 else 15.0


def head():
    out = subprocess.run(['python', '-I', _HERE + '/shiphead.py', pid], capture_output=True, text=True).stdout.split()
    return float(out[0]), float(out[1]), float(out[2])


def hold(key, ms):
    subprocess.run(['pwsh', '-NoProfile', '-File', SP + r'\ui.ps1', '-GamePid', pid, '-Do', 'hold', '-Keys', key,
                    '-HoldMs', str(int(ms))], capture_output=True, text=True)


def diff(a, b):  # from b to a, in (-180, 180]
    return (a - b + 180.0) % 360.0 - 180.0


h0, x, z = head()
print(f'bow at {h0:+.0f}, ship at ({x:.0f}, {z:.0f}), wanted {want:+.0f}')
if abs(diff(want, h0)) <= tol:
    raise SystemExit(0)
hold('D', 2000)
time.sleep(0.5)
h1, _, _ = head()
rate = diff(h1, h0) / 2.0  # degrees a second with D held (its sign says which way D turns)
print(f'D for 2 s: {h0:+.0f} -> {h1:+.0f} ({rate:+.1f} degrees a second)')
if abs(rate) < 0.5:
    print('the ship does not turn (not under way, or aground)')
    raise SystemExit(1)
for i in range(10):
    h, x, z = head()
    d = diff(want, h)
    if abs(d) <= tol:
        print(f'bow at {h:+.0f}, ship at ({x:.0f}, {z:.0f}): there')
        raise SystemExit(0)
    key = 'D' if (d > 0) == (rate > 0) else 'A'
    secs = min(6.0, max(0.6, abs(d) / abs(rate) * 0.8))
    print(f'{i}: bow at {h:+.0f}, {d:+.0f} to go: {key} for {secs:.1f} s')
    hold(key, secs * 1000)
    time.sleep(0.5)
print('not there after ten holds')
raise SystemExit(1)
