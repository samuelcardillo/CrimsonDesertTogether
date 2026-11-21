"""The lead's cross-check of the module groups' work (3663 -> 3732, in place).
1. Every group's map.json: the same old address must have the same new one in every group; a function must start a
   .pdata function; a vtable must be the one of the same RTTI class; nothing may point outside its section.
2. Leftovers: an old address of any map that still stands as a literal in the CODE of src/ (not in a comment), and
   every address-like literal on a line whose code mentions 3663 that is the same as in HEAD and is in no map.
3. Runs each group's verify.py."""
import glob
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')
from p3732lib import *  # noqa: E402,F403

HERE = os.path.dirname(os.path.abspath(__file__)).replace('\\', '/')
REPO = r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi'
IMAGE = 0x13C7E000


def num(v):
    if v is None:
        return None
    if isinstance(v, int):
        return v
    v = str(v).strip()
    try:
        return int(v, 16) if v.lower().startswith('0x') else int(v, 16)
    except ValueError:
        return None


rows = []
for path in sorted(glob.glob(HERE + '/g*/map.json')):
    g = path.replace('\\', '/').split('/')[-2]
    try:
        data = json.load(open(path, encoding='utf-8'))
    except Exception as e:
        print(f'{g}: map.json unreadable: {e}')
        continue
    if isinstance(data, dict):
        data = data.get('map') or data.get('entries') or data.get('values') or []
    for r in data:
        r['group'] = g
        r['o'], r['n'] = num(r.get('old')), num(r.get('new'))
        rows.append(r)
print(f'{len(rows)} map entries from {len({r["group"] for r in rows})} groups')
by_group = {}
for r in rows:
    by_group.setdefault(r['group'], []).append(r)
for g, rs in sorted(by_group.items()):
    kinds = {}
    for r in rs:
        kinds[r.get('kind')] = kinds.get(r.get('kind'), 0) + 1
    print(f'  {g}: {len(rs)} entries {kinds}; unresolved {sum(1 for r in rs if r["n"] is None)}')

print('--- 1a. the same old address with different new ones')
by_old = {}
for r in rows:
    if r['o'] is not None and r['n'] is not None and r.get('kind') not in ('offset', 'array'):
        by_old.setdefault(r['o'], set()).add(r['n'])
bad = 0
for o, ns in sorted(by_old.items()):
    if len(ns) > 1:
        bad += 1
        who = [(r['group'], r.get('file', '').split('/')[-1], r.get('name')) for r in rows if r['o'] == o]
        print(f'  {o:#x} -> {[hex(n) for n in sorted(ns)]}  {who}')
print(f'  {bad} conflicts')

print('--- 1b. by kind')
for r in rows:
    o, n, kind = r['o'], r['n'], r.get('kind')
    tag = f"{r['group']} {r.get('file', '').split('/')[-1]} {r.get('name')}"
    if n is None:
        print(f'  UNRESOLVED {tag}: old {r.get("old")} ({str(r.get("evidence"))[:110]})')
        continue
    if kind == 'function' and not is_start(n):
        print(f'  NOT A FUNCTION START {tag}: {n:#x} (in {func(n)})')
    if kind in ('function', 'site') and not in_code(n):
        print(f'  NOT IN THE CODE {tag}: {n:#x}')
    if kind == 'vtable' and o is not None:
        want = vt3732(o)
        if want is not None and want != n:
            print(f'  VTABLE MISMATCH {tag}: {o:#x} -> {n:#x}, by RTTI name {want:#x}')
        if want is None:
            print(f'  vtable not found by name {tag}: {o:#x} -> {n:#x} ({vt_name(n)})')
    if kind == 'global' and not (0x5618000 <= n < 0x78AA000):
        print(f'  GLOBAL OUTSIDE THE DATA {tag}: {n:#x}')
    if kind == 'rwx' and not (RWX[0] <= n < RWX[0] + RWX[1]):
        print(f'  RWX OUTSIDE THE SECTION {tag}: {n:#x}')
    if n >= IMAGE and kind != 'offset':
        print(f'  BEYOND THE IMAGE {tag}: {n:#x}')

print('--- 2. leftovers in the code of src/')
olds = {r['o'] for r in rows if r['o'] is not None and r['n'] is not None and r['o'] != r['n']
        and r.get('kind') not in ('offset', 'array') and r['o'] >= 0x10000}
known = {r['o'] for r in rows if r['o'] is not None} | {r['n'] for r in rows if r['n'] is not None}
LIT = re.compile(r'0[xX]([0-9A-Fa-f]{5,9})\b')


def code_of(line):
    i = line.find('//')
    return line if i < 0 else line[:i]


left = stale = 0
for root, _, files in os.walk(REPO + '/src'):
    for f in files:
        if not f.endswith(('.h', '.cpp', '.inc')):
            continue
        p = os.path.join(root, f).replace('\\', '/')
        rel = p[len(REPO) + 1:]
        text = open(p, encoding='utf-8-sig', errors='replace').read()
        if '3663' not in text:
            continue
        head = subprocess.run(['git', '-C', REPO, 'show', 'HEAD:' + rel], capture_output=True).stdout.decode('utf-8', 'replace')
        head_lines = set(head.replace('\r', '').split('\n'))
        for i, line in enumerate(text.replace('\r', '').split('\n')):
            code = code_of(line)
            for m in LIT.finditer(code):
                v = int(m[1], 16)
                if v in olds and '3527' not in code and '3576' not in code and '2976' not in code:
                    left += 1
                    print(f'  OLD ADDRESS STILL THERE {rel}:{i + 1}: {v:#x}: {code.strip()[:110]}')
                elif '3663' in code and v >= 0x100000 and v < 0x14500000 and line in head_lines and v not in known:
                    stale += 1
                    print(f'  unchanged, in no map {rel}:{i + 1}: {v:#x}: {code.strip()[:110]}')
print(f'  {left} old addresses still in code; {stale} unchanged literals on 3663 lines that no map names')

print('--- 3. verify.py of each group')
for path in sorted(glob.glob(HERE + '/g*/verify.py')):
    g = path.replace('\\', '/').split('/')[-2]
    r = subprocess.run(['python', '-I', path], capture_output=True, text=True, cwd=os.path.dirname(path))
    tail = (r.stdout.strip().splitlines() or ['(no output)'])[-1]
    print(f'  {g}: exit {r.returncode}: {tail[:160]}')
