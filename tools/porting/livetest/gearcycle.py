"""gearcycle.py host_pid guest_pid [cycles] : the player's sail through its gears and back, as both copies have it
(host at the wheel; the ship may be moored or aground, it only needs its sail). For each step: a hold of ALT (down)
or LSHIFT (up) is started on the host's window and, WHILE it is held, the sail's branch state is read on both copies
several times a second until the host's has settled in another gear or six seconds have passed. One line a step:
which key, how long until the host's sail left its gear and until it settled, the gear on each copy at the end, and
how long the guest's copy was in a different state from the host's. A last line counts the steps that did nothing
(16-nov: ALT holds from scripts sometimes did nothing; this tells whether the game ignored the key or the sail went
back) and the steps after which the two copies differed. CDMP_SHIP=<key> for another ship. Read-only but for the
key holds (ui.ps1, the game's window only)."""
import os as _os
import subprocess
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
SP = _HERE.replace('/', '\\')
H, G = sys.argv[1], sys.argv[2]
cycles = int(sys.argv[3]) if len(sys.argv) > 3 else 2
GEARS = {'89746c86': 0, '4dd95dbe': 1, 'e480ff9b': 2, '3f7594b0': 3}
NAMES = {0: 'furled', 1: 'first gear', 2: 'second gear', 3: 'third gear'}


def run(*a):
    return subprocess.run(['python', '-I', _HERE + '/sailstate.py'] + list(a), capture_output=True, text=True).stdout.split()


key = run(H, 'key')[0]


def state(pid):
    out = run(pid, key)
    return out[0] if out else 'none'


def step(k):
    # The wheel's sail controls are not offered after the game's window lost the focus until a steering key is
    # pressed (16-nov: ten holds in a row did nothing; one hold of D brought the five prompts back). A tap each way.
    for tap in ('A', 'D'):
        subprocess.run(['pwsh', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', SP + r'\ui.ps1', '-GamePid', H, '-Do',
                        'hold', '-Keys', tap, '-HoldMs', '200'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    h0 = state(H)
    p = subprocess.Popen(['pwsh', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', SP + r'\ui.ps1', '-GamePid', H, '-Do',
                          'hold', '-Keys', k, '-HoldMs', '4500'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    left = settled = None
    apart = 0.0
    last = t0
    h = g = h0
    while time.time() - t0 < 6.5:
        h, g = state(H), state(G)
        now = time.time()
        if h != g:
            apart += now - last
        last = now
        if left is None and h != h0:
            left = now - t0
        if left is not None and settled is None and h in GEARS and h != h0:
            settled = now - t0
        if settled is not None and h == g and p.poll() is not None:
            break
    p.wait()
    time.sleep(1.0)
    h, g = state(H), state(G)
    return h0, h, g, left, settled, apart


nothing = differ = steps = 0
print(f'sail {key}: host {state(H)}, guest {state(G)}', flush=True)
for c in range(cycles):
    for k in ('ALT', 'ALT', 'ALT', 'LSHIFT', 'LSHIFT'):
        h0, h, g, left, settled, apart = step(k)
        if h0 == '89746c86' and k == 'ALT' or h0 == '3f7594b0' and k == 'LSHIFT':
            print(f'  {k}: already {NAMES[GEARS[h0]]}: skipped', flush=True)
            continue
        steps += 1
        did = h != h0
        nothing += 0 if did else 1
        differ += 0 if h == g else 1
        print(f'  {k}: host {NAMES.get(GEARS.get(h0), h0)} -> {NAMES.get(GEARS.get(h), h)}'
              + (f' (left its gear after {left:.1f} s, settled after {settled:.1f} s)' if left and settled else
                 ' (NOTHING happened)' if not did else '')
              + f' | guest {NAMES.get(GEARS.get(g), g)}' + ('' if h == g else '  <-- DIFFERENT')
              + f' | copies apart for {apart:.1f} s on the way', flush=True)
print(f'{steps} steps: {nothing} did nothing, {differ} left the two copies different')
