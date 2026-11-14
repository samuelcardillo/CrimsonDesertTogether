#!/bin/bash
# smoothtest.sh H G : with the host at the ship's wheel and the guest on deck (shipsession.sh done, kit profiles), what
# release b2 added and nobody had played: the vehicle poses that go from mod to mod ten times a second, and the NPC
# vehicles that are followed. The host sets sail and holds its course for a minute; meanwhile, on the GUEST (brought
# to the front: the copy behind runs at ~7 frames a second), boat/smoothmon.py measures how its copy of the host's
# ship moves (frames going back, time standing still), boat/vehwatch.py logs every ship on both copies, and at the end
# the guest's own summary says how many of its samples came from the other mod.
cd "$(dirname "$0")"; mkdir -p shots
SP="$(cygpath -w "$(pwd)")"
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
T="D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/boat"
H=$1; G=$2
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"
speed() {  # the host's ship, m/s over 2 s
  python -I - <<EOF
import subprocess, re, time, math
def pos():
    o = subprocess.run(['python', '-I', r'$T/actorinfo.py', '$H', 'B010000A'], capture_output=True, text=True).stdout
    m = re.search(r'local \(([^)]*)\)', o)
    return [float(x) for x in m.group(1).split(',')] if m else None
a = pos(); t = time.time(); time.sleep(2.0); b = pos()
print(f'{math.hypot(b[0]-a[0], b[2]-a[2]) / (time.time() - t):.1f}' if a and b else '0')
EOF
}
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8)"
(python -I "$T/vehwatch.py" $H $G 150 2 1 > shots/vehwatch_smooth.txt 2>&1 &)
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
V=$(speed); echo "  after LSHIFT: $V m/s"
if python -I -c "import sys; sys.exit(0 if float('$V') < 2.5 else 1)"; then
  echo "  no gear: wheel released and taken again, LSHIFT once more"
  $UH -Do key -Keys R -HoldMs 150 >/dev/null 2>&1; sleep 3
  $UH -Do key -Keys E -HoldMs 150 >/dev/null 2>&1; sleep 4
  $UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
  V=$(speed); echo "  after the second LSHIFT: $V m/s"
fi
# The guest's window in front for the measurement (a screenshot does that and sends no key).
$UG -Do shot -Out "$SP\\shots\\smooth_g.png" >/dev/null 2>&1
echo "  the guest's copy of the host's ship, 35 s:"
timeout 60 python -I "$T/smoothmon.py" $G B010000A 35 2>&1 | sed 's/^/    /'
echo "  the host's own ship on the host, 20 s (what smooth looks like there; its window is behind: fewer frames):"
timeout 40 python -I "$T/smoothmon.py" $H B010000A 20 2>&1 | sed 's/^/    /'
$UH -Do hold -Keys ALT -HoldMs 2500 >/dev/null 2>&1; sleep 6
echo "  the guest's follower:"
grep -a "VehicleFollow summary" "$B/cdmp.guest.log" | tail -1 | sed -E 's/.*(\| 0xB010000A[^|]*).*/    \1/' | cut -c1-330
grep -a "VehicleFollow summary" "$B/cdmp.guest.log" | tail -1 | sed -E 's/.*(\| vehicle poses:[^|]*).*/    \1/' | cut -c1-330
grep -a -E "VehicleFollow: (listed vehicles|[0-9]+ vehicle\(s\) this client controls)" "$B/cdmp.guest.log" "$B/cdmp.host.log" | tail -4 | cut -c1-360 | sed 's/^/    /'
echo "  ships on both copies while it sailed (boat/vehwatch.py, every 2 s):"
python -I - <<'EOF'
import re, collections
rows = [l for l in open('shots/vehwatch_smooth.txt', encoding='utf-8', errors='replace').read().splitlines() if ' | ' in l]
stat = collections.defaultdict(lambda: [0, 0, 0.0, 0.0, ''])
for l in rows:
    for part in l.split(' | ')[1:]:
        m = re.search(r'^(\w{8}) (player|NPC)\s+([\d.]+) m/s at \(([-\d]+),([-\d]+)\) guest copy\s+([\d.]+) m off', part)
        if not m: continue
        s = stat[m.group(1)]; s[4] = m.group(2); s[0] += 1
        if 1.0 < float(m.group(3)) < 60: s[1] += 1; s[2] += float(m.group(6)); s[3] = max(s[3], float(m.group(6)))
for k, s in sorted(stat.items()):
    print(f'    {k} {s[4]:6s}: {s[0]} samples, {s[1]} moving; guest copy while it moved: mean {s[2] / s[1] if s[1] else 0:.1f} m off, max {s[3]:.1f} m')
EOF
echo "DONE at $(date +%T): ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log"); crash captures host $(grep -a -c 'crash capture (' "$B/cdmp.host.log") guest $(grep -a -c 'crash capture (' "$B/cdmp.guest.log")"
