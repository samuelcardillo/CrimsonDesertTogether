#!/bin/bash
# smooth2.sh H G [npc_key ...] : how EVEN the guest sees ships move, with the host's copy behind (host at the wheel,
# guest on deck). Two copies on one PC: the copy that is not in front draws 5-7 frames a second and its world runs
# slow (a ship in first gear does 3.3 m/s with the host in front and ~1 m/s with it behind: smoothtest.sh, 15-nov,
# found nothing "moving"). So: second gear with the host in front, then the guest in front and the measurements:
# boat/smoothmon.py on the guest's copy of the host's ship (30 s) and of each NPC vehicle given (20 s each), and
# boat/tagmon.py (how far each copy draws the other player's name tag from its character). Then the brake.
cd "$(dirname "$0")"; mkdir -p shots
SP="$(cygpath -w "$(pwd)")"
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
T="D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/boat"
H=$1; G=$2; shift 2
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"
speed() {  # the host's ship on the host, m/s over 2 s
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
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
V=$(speed); echo "  first gear, host in front: $V m/s"
if python -I -c "import sys; sys.exit(0 if float('$V') < 2.5 else 1)"; then
  echo "  no gear: wheel released and taken again, LSHIFT once more"
  $UH -Do key -Keys R -HoldMs 150 >/dev/null 2>&1; sleep 3
  $UH -Do key -Keys E -HoldMs 150 >/dev/null 2>&1; sleep 4
  $UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
  echo "  first gear at the second try: $(speed) m/s"
fi
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
echo "  second gear, host in front: $(speed) m/s"
$UG -Do shot -Out "$SP\\shots\\smooth2_g.png" >/dev/null 2>&1; sleep 3
echo "  guest in front (the host's copy behind): $(speed) m/s"
echo "  the guest's copy of the host's ship, 30 s:"
timeout 60 python -I "$T/smoothmon.py" $G B010000A 30 0.5 2>&1 | sed 's/^/    /'
for K in "$@"; do
  echo "  the guest's copy of the NPC vehicle $K, 20 s:"
  timeout 50 python -I "$T/smoothmon.py" $G $K 20 0.5 2>&1 | sed 's/^/    /'
done
echo "  name tags, 10 s (the largest and the mean distance from the character each copy draws):"
timeout 30 python -I "$T/tagmon.py" $H $G 10 0.5 2>&1 | python -I -c "
import re, sys
g, h, sp = [], [], []
for l in sys.stdin:
    m = re.search(r'ship\s+([\d.]+) m/s .*the guest draws its tag\s+([\d.]+) m .*the host draws its tag\s+([\d.]+) m', l)
    if m: sp.append(float(m.group(1))); g.append(float(m.group(2))); h.append(float(m.group(3)))
if g: print(f'    ship {sum(sp)/len(sp):.1f} m/s on the host; the guest draws the host\'s tag {sum(g)/len(g):.2f} m off on average (max {max(g):.2f}); the host draws the guest\'s {sum(h)/len(h):.2f} m off (max {max(h):.2f})')
else: print('    no samples')"
$UH -Do hold -Keys ALT -HoldMs 2500 >/dev/null 2>&1; sleep 6
echo "  the guest's follower:"
grep -a "VehicleFollow summary" "$B/cdmp.guest.log" | tail -1 | tr '|' '\n' | grep -E "0xB010000A|vehicle poses" | cut -c1-460 | sed 's/^/    /'
echo "DONE at $(date +%T): ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log"); crash captures host $(grep -a -c 'crash capture (' "$B/cdmp.host.log") guest $(grep -a -c 'crash capture (' "$B/cdmp.guest.log")"
