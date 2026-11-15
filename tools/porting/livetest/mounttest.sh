#!/bin/bash
# mounttest.sh H G [out_dir] : after mountsession.sh (the host next to its mount on the Vellua hilltop, the guest at the
# nexus): the host mounts (E), runs four legs of 5.5 s back and forth within 50 m of the guest (W + LSHIFT, about 9 m/s),
# runs away for 12 s (past 100 m, where the game updates a creature's copy less often), and dismounts (R). All the
# while the guest's copy of the host's character and mount is measured against the host's own (mountmon.py) and the
# mount's interpolation buffer on the guest is read (interpmon.py). Then what the guest's mod said: the mount
# transaction, the seat, and a rider that entered RideOn without a seat (the mount blink).
cd "$(dirname "$0")"
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
H=$1; G=$2; OUT=${3:-$(pwd)/shots}
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
M=$(python -I nearactors.py $H 40 5 2>/dev/null | sed -n '2p' | cut -d' ' -f3)
[ -n "$M" ] || { echo "no mount within 40 m of the host"; exit 1; }
echo "mount $M"
(python -I mountmon.py $H $G 75 "$OUT/mounttest.csv" > "$OUT/mounttest.txt" 2>&1 &)
(python -I interpmon.py $G $M 75 "$OUT/mounttest_interp.txt" > "$OUT/mounttest_interp_sum.txt" 2>&1 &)
sleep 2
$UH -Do key -Keys E -HoldMs 150 >/dev/null 2>&1
sleep 6
echo "mounted: $(python -I posnow.py $H | sed 's/.*parent/parent/')"
for leg in "-10659 -6018" "-10619 -6100" "-10659 -6018" "-10619 -6100"; do
  python -I face.py $H $leg >/dev/null 2>&1
  $UH -Do hold -Keys "W LSHIFT" -HoldMs 5500 >/dev/null 2>&1
done
echo "near legs done: host at $(python -I posnow.py $H | sed 's/.*world //;s/ parent.*//')"
python -I face.py $H -10659 -6018 >/dev/null 2>&1
$UH -Do hold -Keys "W LSHIFT" -HoldMs 12000 >/dev/null 2>&1
echo "far leg done: host at $(python -I posnow.py $H | sed 's/.*world //;s/ parent.*//')"
sleep 2
$UH -Do key -Keys R -HoldMs 150 >/dev/null 2>&1
sleep 6
echo "dismounted: $(python -I posnow.py $H | sed 's/.*parent/parent/')"
for i in $(seq 1 20); do grep -q "seat:\|on foot" "$OUT/mounttest.txt" 2>/dev/null && break; sleep 2; done
cat "$OUT/mounttest.txt" "$OUT/mounttest_interp_sum.txt"
echo "--- the guest's mod"
grep -a "MountSync" "$B/cdmp.guest.log" | grep -a "MountApply\|MountSeat\|NOT SEATED\|SEAT\|stuck\|retire" | grep -a -v summary | tail -8 | cut -c1-420
grep -a "MountSync summary" "$B/cdmp.guest.log" | tail -1 | grep -a -o "mount_seat [0-9a-z]*: seat \[[^]]*\]"
grep -a "MountSync summary" "$B/cdmp.guest.log" | tail -1 | grep -a -o "seated here \[[^]]*\]" | cut -c1-400
grep -a -c "ERR" "$B/cdmp.guest.log" "$B/cdmp.host.log"
ls "$B"/cdmp_crash*.dmp 2>/dev/null | tail -2
