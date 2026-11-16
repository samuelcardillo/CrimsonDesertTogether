#!/bin/bash
# jumptest.sh H G [jumps] : passenger_air_follow seen in the game (host at the wheel, guest on deck: shipsession.sh
# done). The host sets one gear, brings the bow to open water (STEER=<heading>, 95 by default: east of the Vellua
# mooring) and sets a second gear with its copy in front; then the GUEST's copy comes in front and the guest jumps on
# the deck (SPACE) `jumps` times (8), four seconds apart, while the ship runs. After each jump: the ship's speed, where
# the guest stands (its parent and its place in the ship's frame) and how far that is from where it stood before the
# jump. At the end, what the guest's mod wrote about each time in the air ("was in the air for ... it ended: ...")
# and the records of the frames in which the game took the character off the ship.
#   NOSAIL=1: the ship is already under way on its course (sailout.sh): no gear, no steering; only the jumps.
#   With the guest in front the host's copy runs slowly (5-7 frames a second and its world slower): the ship then
#   does 1-4 m/s, not the 10 m/s of second gear. passenger_air_follow does nothing below 1 m/s.
cd "$(dirname "$0")"; mkdir -p shots
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
H=$1; G=$2; N=${3:-8}
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"
L0=$(grep -a -c "was in the air for" "$B/cdmp.guest.log")
R0=$(grep -a -c "the game took this client's own character off" "$B/cdmp.guest.log")
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8); guest at $(python -I posnow.py $G | cut -c1-60)"
grep -a "passenger_air_follow" "$B/cdmp.guest.log" | head -1 | cut -c30-200
if [ -z "$NOSAIL" ]; then
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
if python -I sailstate.py $H "$(python -I sailstate.py $H key)" | grep -q furled; then
  $UH -Do key -Keys R -HoldMs 150 >/dev/null 2>&1; sleep 3
  $UH -Do key -Keys E -HoldMs 150 >/dev/null 2>&1; sleep 4
  $UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
fi
timeout 120 python -I steer.py $H ${STEER:-95} 20 2>&1 | tail -1 | sed 's/^/  steering: /'
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 6
fi
echo "  under way: $(python -I shiphead.py $H | cut -c1-90)"
place() {  # the guest: parent and local position
  python -I posnow.py $G 2>/dev/null | python -I -c "
import sys
t = sys.stdin.read().split()
print(t[-1], t[0], t[1], t[2])"
}
speed() {  # the ship's speed on the host over one second
  python -I - $H <<'EOF'
import subprocess, sys, time, math, re
def at():
    o = subprocess.run([sys.executable, '-I', 'shiphead.py', sys.argv[1]], capture_output=True, text=True).stdout
    m = re.findall(r'-?\d+\.\d+', o)
    return float(m[-2]), float(m[-1]), time.time()  # shiphead prints "heading x z"
try:
    a = at(); time.sleep(1.0); b = at()
    print(f'{math.hypot(b[0] - a[0], b[1] - a[1]) / (b[2] - a[2]):.1f}')
except Exception as e:
    print('?')
EOF
}
for i in $(seq 1 $N); do
  set -- $(place); P0=$1; X0=$2; Y0=$3; Z0=$4
  (python -I jumpwatch.py $G 2.6 > shots/jumpwatch.txt 2>&1 &)
  sleep 0.3
  $UG -Do key -Keys ${JUMPKEY:-SPACE} -HoldMs 120 >/dev/null 2>&1
  sleep 2.5
  set -- $(place)
  D=$(python -I -c "import math; print(f'{math.hypot($2 - ($X0), $4 - ($Z0)):.2f}')" 2>/dev/null)
  echo "  jump $i: ship $(speed) m/s | before on $P0 at ($X0, $Y0, $Z0) | after on $1 at ($2, $3, $4) | $D m across the deck | $(cat shots/jumpwatch.txt 2>/dev/null)"
  [ "$1" = "0x0" ] && { echo "  the guest is OFF the ship"; break; }
  sleep 1.5
done
sleep 3
echo "--- the guest's mod, this test"
grep -a "was in the air for" "$B/cdmp.guest.log" | tail -n +$((L0 + 1)) | cut -c16-28,60-520
echo "records of frames this test: $(( $(grep -a -c "the game took this client's own character off" "$B/cdmp.guest.log") - R0 ))"
grep -a "VehicleFollow summary" "$B/cdmp.guest.log" | tail -1 | grep -a -o "this client's own character in the air[^|]*"
$UH -Do hold -Keys ALT -HoldMs 2500 >/dev/null 2>&1; sleep 3
echo "DONE at $(date +%T): ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log"); dumps $(ls "$B"/cdmp_crash*.dmp 2>/dev/null | wc -l)"
