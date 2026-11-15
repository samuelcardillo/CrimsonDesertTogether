#!/bin/bash
# tagcheck.sh H G [aboard] : with the host at the wheel and the guest on deck (shipsession.sh done).
#  1. The other player's name tag under sail (first gear): the guest's camera is turned to the host at the wheel and
#     a screenshot taken (shots/tagcheck_guest.png: the tag should sit over the helmsman's head), boat/tagmon.py says
#     how far from the character the tag would be at the position sent (what the mod drew until 15-nov) and where the
#     mod draws it now, and each copy's last "Overlay diag" line says what the mod itself did.
#  2. With "aboard": does the guest stay on the ship at full sail with the host's copy in front (the guest's behind,
#     5-7 frames a second)? Second gear, then the ship's speed and where the guest is every few seconds for a minute
#     and a half. On 15-nov 02:52 it left the deck at 18 m/s.
cd "$(dirname "$0")"; mkdir -p shots
SP="$(cygpath -w "$(pwd)")"
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
T="D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/boat"
H=$1; G=$2; MODE=$3
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
where() {  # as in shipsession.sh
  python -I posnow.py $1 2>/dev/null | python -I -c "
import sys
t = sys.stdin.read().split()
if len(t) < 9 or t[-1] == '0x0': print('off')
else:
    x, y = abs(float(t[0])), abs(float(t[1]))
    print('wheel' if x < 0.3 and y < 0.3 else 'ladder' if x < 0.8 else 'deck')"
}
diag() {  # log : what the mod's overlay says about the other player's tag, last line
  grep -a "Overlay diag" "$1" | tail -1 | sed -E "s/.*(other player's tag:.*)/\1/" | cut -c1-200
}
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8); host $(where $H), guest $(where $G)"
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
V=$(speed); echo "  first gear, host in front: $V m/s"
if python -I -c "import sys; sys.exit(0 if float('$V') < 2.5 else 1)"; then
  $UH -Do key -Keys R -HoldMs 150 >/dev/null 2>&1; sleep 3
  $UH -Do key -Keys E -HoldMs 150 >/dev/null 2>&1; sleep 4
  $UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
  echo "  first gear at the second try: $(speed) m/s"
fi
# With the host in front (the ship at its real speed): the numbers.
echo "  host in front:"
timeout 30 python -I "$T/tagmon.py" $H $G 8 0.5 2>&1 | python -I -c "
import re, sys
rows = []
for l in sys.stdin:
    m = re.search(r'ship\s+([\d.]+) m/s .*the guest draws its tag\s+([\d.]+) m from its character at the position sent \([^)]*\), now\s+([\d.-]+)( m)? .*the host draws its tag\s+([\d.]+) m from its character at the position sent \([^)]*\), now\s+([\d.-]+)', l)
    if m: rows.append(m.groups())
def mean(i):
    v = [float(r[i]) for r in rows if r[i] not in ('-', None)]
    return f'{sum(v) / len(v):.2f} m (max {max(v):.2f})' if v else '-'
if rows: print(f'    ship {sum(float(r[0]) for r in rows) / len(rows):.1f} m/s | the host\'s tag on the guest: at the position sent {mean(1)}, now {mean(2)} | the guest\'s tag on the host: at the position sent {mean(4)}, now {mean(5)}')
else: print('    no samples')"
sleep 5
echo "    the host's overlay:  $(diag "$B/cdmp.host.log")"
echo "    the guest's overlay: $(diag "$B/cdmp.guest.log")"
# The guest's view: its camera to the host at the wheel.
$UG -Do shot -Out "$SP\\shots\\tagcheck_guest0.png" >/dev/null 2>&1; sleep 1
for i in 1 2; do
  set -- $(python -I posnow.py $H | awk '{print $5, $7}')
  timeout 30 python -I face.py $G $1 $2 >/dev/null 2>&1
done
$UG -Do shot -Out "$SP\\shots\\tagcheck_guest.png" >/dev/null 2>&1
echo "  guest in front (the host's copy behind, the ship slow): $(speed) m/s; guest $(where $G); screenshot shots/tagcheck_guest.png"
sleep 6
echo "    the guest's overlay: $(diag "$B/cdmp.guest.log")"
# The host's view: its camera to where the guest stands; the guest's tag should sit over its head. (The guest's
# camera does not turn with the mouse while it stands on the deck; the host's does at the wheel.)
$UH -Do shot -Out "$SP\shots\tagcheck_host0.png" >/dev/null 2>&1; sleep 1
GL=$(python -I posnow.py $G | awk '{print $1, $3}')
for i in 1 2; do
  set -- $(python -I shiplocal.py $H $GL)
  timeout 30 python -I face.py $H $1 $2 >/dev/null 2>&1
done
$UH -Do shot -Out "$SP\shots\tagcheck_host.png" >/dev/null 2>&1
echo "  host in front again: $(speed) m/s; screenshot shots/tagcheck_host.png"
sleep 6
echo "    the host's overlay:  $(diag "$B/cdmp.host.log")"
if [ "$MODE" = aboard ]; then
  echo "  full sail with the host in front; the guest's copy behind:"
  $UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 4
  for n in $(seq 1 26); do
    V=$(speed); W=$(where $G)
    echo "    $(date +%T) ship $V m/s, guest $W"
    [ "$W" = off ] && break
    sleep 1.5
  done
fi
echo "DONE at $(date +%T): ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log"); crash captures host $(grep -a -c 'crash capture (' "$B/cdmp.host.log") guest $(grep -a -c 'crash capture (' "$B/cdmp.guest.log")"
