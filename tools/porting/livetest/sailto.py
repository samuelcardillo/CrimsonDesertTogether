"""sailto.py host_pid x z [x z ...] : sail the ship the copy's character is steering through the world points given and
furl the sail at the last one (host at the wheel). Options before the points: -r metres (how near counts as there:
60), -g holds (LSHIFT holds at the start: 1; 0 when the ship is already under way), -keep (do not furl at the end).

The helm of this ship STAYS where it was left: after a hold of A the bow keeps swinging until D is held as long
(16-nov: sailaway.py reached its heading with the helm hard over and the ship went on round in a circle). So every
look measures the bow twice, a second apart, with no key held: where it points and how fast it is turning, and the
next hold answers both (the heading wanted against the heading the ship will have four seconds on). D turns the bow
towards higher headings (+x) and A towards lower ones; that is only changed when a long hold plainly did the opposite
(the first version learnt it from every hold, and whole-degree headings taught it wrong: that run ended on a beach).
The gear goes in again when the ship stands still. At the last point: ALT for 4 s (shorter does not furl).

FRONT=<pid> in the environment: after every hold that copy's window is brought to the front again (ui.ps1 shot), for
runs in which the OTHER copy has to stay the one in front (a passenger whose copy is behind falls off a fast ship);
the steering copy then runs slowly and so does its ship.
Exit code 0 at the last point, 1 if it never got there. CDMP_SHIP=<key> for another ship. Input through ui.ps1."""
import math
import os as _os
import subprocess
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
SP = _HERE.replace('/', '\\')
pid = sys.argv[1]
args = sys.argv[2:]
stop_m, gears, furl = 60.0, 1, True
pts = []
i = 0
while i < len(args):
    if args[i] == '-r':
        stop_m = float(args[i + 1]); i += 2
    elif args[i] == '-g':
        gears = int(args[i + 1]); i += 2
    elif args[i] == '-keep':
        furl = False; i += 1
    else:
        pts.append((float(args[i]), float(args[i + 1]))); i += 2
FRONT = _os.environ.get('FRONT')
AHEAD_S = 4.0


def head():
    out = subprocess.run(['python', '-I', _HERE + '/shiphead.py', pid], capture_output=True, text=True).stdout.split()
    if len(out) < 3:
        raise SystemExit('no ship (CDMP_SHIP?)')
    return float(out[3]) if len(out) > 3 else float(out[0]), float(out[1]), float(out[2])


def ui(target, *more):
    subprocess.run(['pwsh', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', SP + r'\ui.ps1', '-GamePid', str(target)]
                   + list(more), capture_output=True, text=True)


def hold(key, ms):
    ui(pid, '-Do', 'hold', '-Keys', key, '-HoldMs', str(int(ms)))
    if FRONT:
        ui(FRONT, '-Do', 'shot', '-Out', SP + r'\shots\front_tmp.png')


def diff(a, b):  # from b to a, in (-180, 180]
    return (a - b + 180.0) % 360.0 - 180.0


def look():
    """heading, turning rate (degrees a second), position, speed (m/s): two readings a second apart, no key held."""
    h1, x1, z1 = head()
    t1 = time.time()
    time.sleep(1.0)
    h2, x2, z2 = head()
    dt = max(time.time() - t1, 0.2)
    return h2, diff(h2, h1) / dt, x2, z2, math.hypot(x2 - x1, z2 - z1) / dt


for _ in range(gears):
    hold('LSHIFT', 1500)
    time.sleep(1.5)
raises = 'D'
wrong = 0
still = 0
last = None  # (key, seconds, rate before the hold)
at = 0
for step in range(400):
    h, rate, x, z, speed = look()
    if last and last[1] >= 1.5 and abs(rate - last[2]) >= 3.0:
        # A long hold, and the turning rate moved by a lot: which way?
        went_up = rate > last[2]
        if (last[0] == raises) != went_up:
            wrong += 1
            if wrong >= 2:
                raises = 'A' if raises == 'D' else 'D'
                wrong = 0
                print(f'{step}: two long holds turned the other way: {raises} is the key that raises the heading', flush=True)
        else:
            wrong = 0
    last = None
    tx, tz = pts[at]
    dist = math.hypot(tx - x, tz - z)
    if dist <= stop_m:
        if at + 1 < len(pts):
            at += 1
            print(f'{step}: point {at} passed at ({x:.0f}, {z:.0f})', flush=True)
            continue
        if furl:
            hold('ALT', 4000)
            time.sleep(4.0)
            h, rate, x, z, speed = look()
        print(f'there: bow {h:+.0f} at ({x:.0f}, {z:.0f}), {math.hypot(tx - x, tz - z):.0f} m from the point, {speed:.1f} m/s',
              flush=True)
        raise SystemExit(0)
    if speed < 0.4:
        still += 1
        if still in (2, 6):
            print(f'{step}: the ship stands still at ({x:.0f}, {z:.0f}): the gear again', flush=True)
            hold('LSHIFT', 1500)
            time.sleep(1.5)
            continue
        if still > 10:
            print(f'the ship does not move at ({x:.0f}, {z:.0f}): aground?', flush=True)
            raise SystemExit(1)
    else:
        still = 0
    want = math.degrees(math.atan2(tx - x, tz - z))
    err = diff(want, h + rate * AHEAD_S)  # against where the bow will point if nothing is done
    line = (f'{step}: bow {h:+.0f} turning {rate:+.1f}/s, {speed:.1f} m/s at ({x:.0f}, {z:.0f}); point {at + 1} is at '
            f'{want:+.0f}, {dist:.0f} m')
    if abs(err) <= 6.0:
        print(line + ': as it goes', flush=True)
        time.sleep(1.0)
        continue
    key = raises if err > 0 else ('A' if raises == 'D' else 'D')
    secs = min(2.5, max(0.3, abs(err) / 35.0))
    print(line + f': {key} {secs:.1f} s', flush=True)
    hold(key, secs * 1000)
    last = (key, secs, rate)
print('not there after 400 looks', flush=True)
raise SystemExit(1)
