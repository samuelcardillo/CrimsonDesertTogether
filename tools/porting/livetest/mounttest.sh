#!/bin/bash
# mounttest.sh RIDER OBSERVER [observer's log] : after mountsession.sh (the rider next to its mount on the Vellua
# hilltop, the other player at the nexus): the rider mounts (E), runs four legs of 5.5 s back and forth within 50 m of
# the observer (W + LSHIFT, about 9 m/s) and dismounts (R) where it started. All the while the observer's copy of the
# rider's character and mount is measured against the rider's own (mountmon.py) and the mount's interpolation buffer
# on the observer is read (interpmon.py). Then what the observer's mod said: the mount transaction, the seat, and a
# rider that entered RideOn without a seat (the mount blink).
#   RIDER / OBSERVER: the two pids (host guest: the host rides; guest host: the guest rides).
#   observer's log: cdmp.guest.log (default) or cdmp.host.log. A fourth argument names the mount (its key) when the
#   nearest vehicle actor to the rider is not it.
#   FAR=1 in the environment: before dismounting, 12 s more away from the observer (past 100 m, where the game updates
#   a creature's copy less often) and back. Without it the rider never leaves the hilltop: on 16-nov the far leg ended
#   on a mountainside, the host dismounted there, fell thirty metres and died.
cd "$(dirname "$0")"
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
H=$1; G=$2; LOG=${3:-cdmp.guest.log}; OUT=$(pwd)/shots
T=75; [ -n "$FAR" ] && T=110
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
M=$4
[ -n "$M" ] || M=$(python -I nearactors.py $H 40 5 2>/dev/null | sed -n '2p' | awk '{print $1}')
[ -n "$M" ] || { echo "no mount within 40 m of the rider"; exit 1; }
echo "mount $M"
(python -I mountmon.py $H $G $T "$OUT/mounttest.csv" > "$OUT/mounttest.txt" 2>&1 &)
(python -I interpmon.py $G $M $T "$OUT/mounttest_interp.txt" > "$OUT/mounttest_interp_sum.txt" 2>&1 &)
sleep 2
$UH -Do key -Keys E -HoldMs 150 >/dev/null 2>&1
sleep 6
P=$(python -I posnow.py $H | sed 's/.*parent //')
echo "mounted: parent $P"
[ "$P" = "0x0" ] && { echo "the rider did NOT mount (E did nothing): stopping"; exit 1; }
for leg in "-10659 -6018" "-10619 -6100" "-10659 -6018" "-10619 -6100"; do
  python -I face.py $H $leg >/dev/null 2>&1
  $UH -Do hold -Keys "W LSHIFT" -HoldMs 5500 >/dev/null 2>&1
done
echo "near legs done: rider at $(python -I posnow.py $H | sed 's/.*world //;s/ parent.*//')"
if [ -n "$FAR" ]; then
  python -I face.py $H -10659 -6018 >/dev/null 2>&1
  $UH -Do hold -Keys "W LSHIFT" -HoldMs 12000 >/dev/null 2>&1
  echo "far leg done: rider at $(python -I posnow.py $H | sed 's/.*world //;s/ parent.*//')"
  python -I face.py $H -10619 -6100 >/dev/null 2>&1
  $UH -Do hold -Keys "W LSHIFT" -HoldMs 12000 >/dev/null 2>&1
  echo "back: rider at $(python -I posnow.py $H | sed 's/.*world //;s/ parent.*//')"
fi
sleep 2
$UH -Do key -Keys R -HoldMs 150 >/dev/null 2>&1
sleep 6
echo "dismounted: parent $(python -I posnow.py $H | sed 's/.*parent //')"
for i in $(seq 1 30); do grep -q "seat:\|on foot" "$OUT/mounttest.txt" 2>/dev/null && break; sleep 2; done
cat "$OUT/mounttest.txt" "$OUT/mounttest_interp_sum.txt"
echo "--- the observer's mod ($LOG)"
grep -a "MountSync" "$B/$LOG" | grep -a "MountApply\|NOT SEATED\|stuck\|STUCK" | grep -a -v summary | tail -3 | cut -c1-330
grep -a "MountSync summary" "$B/$LOG" | tail -1 | grep -a -o "mount_seat [0-9a-z]*: seat \[[^]]*\]"
echo "ERR lines: guest $(grep -a -c ' ERR ' "$B/cdmp.guest.log"), host $(grep -a -c ' ERR ' "$B/cdmp.host.log"); dumps: $(ls "$B"/cdmp_crash*.dmp 2>/dev/null | wc -l)"
