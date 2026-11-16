"""passive_watch.py host_pid guest_pid [out_file] : a READ-ONLY recorder for when the owner sails the two copies
themself (16-nov: "stop using it": no input to any window, nothing launched or closed). While the host's character
is on a ship it writes, about every half minute, one block to out_file (default shots/passive_watch.txt):
  - the ship (its key, where it is, how fast it went in that window);
  - the crew: how far the guest draws each crew member from its post (boat/npcmon.py five times a second for 20 s:
    the median, the 90th and 99th percentile and the worst, and how many samples had one missing on the guest);
  - whether the guest's character is on that ship (aboard.py);
  - the sea: the twelve wave trains of both copies (seawaves.py: direction and origin the same or not, the largest
    phase difference);
  - every ship's sail on both copies (shipsails.py), the lines that differ in full.
It only reads the two processes' memory through the other tools and ends by itself when either copy is gone."""
import os as _os
import re
import subprocess
import sys
import time

_HERE = _os.path.dirname(_os.path.abspath(__file__)).replace('\\', '/')
sys.path.insert(0, _HERE)
sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from cdbuild import WORLD_SLOT  # noqa: E402
from live_reader_check import Reader  # noqa: E402

B = 0x140000000
H, G = sys.argv[1], sys.argv[2]
OUT = sys.argv[3] if len(sys.argv) > 3 else _HERE + '/shots/passive_watch.txt'
BOAT = _HERE + '/../boat'


def alive(pid):
    out = subprocess.run(['tasklist', '/FI', f'PID eq {pid}', '/FO', 'CSV', '/NH'], capture_output=True, text=True).stdout
    return 'CrimsonDesert' in out


def ship_of_host():
    """The key of the vehicle the host's character hangs from (through its parents), or 0."""
    try:
        r = Reader(int(H))
        world = r.ptr(B + WORLD_SLOT)
        mgr = r.ptr(world + 0x30)
        t = r.ptr(mgr + 8)
        n = r.unpack(t + 0x88, '<I')[0]
        buckets, nodes = r.ptr(t + 0x98), r.ptr(t + 0xA0)

        def actor(key):
            b = buckets + (key % n) * 0x100
            for i in range(min(r.unpack(b, '<I')[0], 31)):
                k, idx = r.unpack(b + 8 + i * 8, '<II')
                if k == key:
                    return r.ptr(r.ptr(nodes + idx * 8) + 8)
            return 0

        a = r.ptr(mgr + 0x50)
        for _ in range(8):
            if not a:
                return 0
            info = r.ptr(a + 0x88)
            if info and r.unpack(info + 1, '<B')[0] == 5:
                return r.unpack(a + 0x60, '<I')[0]
            comps = r.ptr(a + 0x68)
            h = r.ptr(comps + 0x1B8) if comps else 0
            par = r.unpack(h + 0x1C, '<I')[0] if h else 0
            if not par:
                return 0
            a = actor(par)
    except Exception:
        return 0
    return 0


def run(args, env=None, timeout=90):
    try:
        return subprocess.run(['python', '-I'] + args, capture_output=True, text=True, timeout=timeout,
                              env=dict(_os.environ, **(env or {}))).stdout
    except Exception:
        return ''


def say(text):
    with open(OUT, 'a', encoding='utf-8') as f:
        f.write(text + '\n')


say(f'=== {time.strftime("%Y-%m-%d %H:%M:%S")} passive_watch started: host {H}, guest {G}; read-only')
idle = 0
while alive(H) and alive(G):
    key = ship_of_host()
    if not key:
        idle += 1
        if idle % 24 == 1:
            say(f'{time.strftime("%H:%M:%S")} the host is not on a ship')
        time.sleep(5)
        continue
    idle = 0
    env = {'CDMP_SHIP': f'0x{key:08X}'}
    crew = run([BOAT + '/npcmon.py', H, G, '20', '0.2', f'{key:08X}'], env)
    errs, speeds, riders, absent = [], [], set(), 0
    for line in crew.splitlines():
        m = re.match(r'\s*[\d.]+ ship\s+([\d.]+) m/s', line)
        if not m:
            continue
        speeds.append(float(m.group(1)))
        for k, e in re.findall(r'(\w{8}) on \w{4} \(guest: on \w{4}, req \d+\)\s+([\d.]+) m off', line):
            errs.append(float(e))
            riders.add(k)
        absent += len(re.findall(r'NOT on the guest|the guest has no', line))
    head = run([_HERE + '/shiphead.py', H], env).split()
    where = f'({head[1]}, {head[2]}) bow {head[0]}' if len(head) >= 3 else 'unreadable'
    block = [f'--- {time.strftime("%H:%M:%S")} ship 0x{key:08X} at {where}']
    if errs:
        errs.sort()
        n = len(errs)
        block.append(f'crew: {len(riders)} members, {n} samples, ship {min(speeds):.1f} to {max(speeds):.1f} m/s: off their posts '
                     f'by {errs[n // 2]:.2f} m (median), {errs[n * 9 // 10]:.2f} m (90 %), {errs[n * 99 // 100]:.2f} m (99 %), '
                     f'{errs[-1]:.2f} m at most; missing on the guest in {absent} samples')
    else:
        block.append('crew: no samples (no crew under that ship, or the guest does not have it)')
    aboard = run([_HERE + '/aboard.py', G], env).strip().splitlines()
    block.append('guest: ' + (aboard[-1][:110] if aboard else 'unreadable'))
    sea = run([_HERE + '/seawaves.py', H, G, '6', '1']).strip().splitlines()
    block += ['sea: ' + s[:200] for s in sea[-2:]]
    sails = run([_HERE + '/shipsails.py', H, G]).strip().splitlines()
    differ = [s for s in sails if 'DIFFERENT' in s]
    block.append(f'sails: {len([s for s in sails if " ship " in s])} ship(s) with a sail set, {len(differ)} that differ')
    block += ['   ' + s[:230] for s in differ[:4]]
    say('\n'.join(block))
say(f'=== {time.strftime("%H:%M:%S")} a copy is gone: passive_watch ends')
