#!/bin/bash
# sailout.sh H [heading] : from the Vellua mooring to open water without running aground (host at the wheel). The
# moored ship points south-west (-146 to -162 on 16-nov) and the shoals are south-west and south of it: around
# (-10410, -6373) and (-10442, -6432); two ships were lost there that night. One gear, then the helm hard to port (A
# lowers the heading about 3 degrees a second) until the bow points to `heading` (105: east-south-east, the track of
# the 02:42 run: (-10382, -6261) -> (-10315, -6286) -> (-10108, -6334) -> (-9782, -6383), all water). Prints the
# heading and the position after each hold; fails if the ship stops.
#   CDMP_SHIP=<key> when the ship is not 0xB010000A (a ship retrieved again gets a new key).
cd "$(dirname "$0")"
H=$1; WANT=${2:-105}
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
echo "start: $(python -I shiphead.py $H)"
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1
last=""
for i in $(seq 1 30); do
  $UH -Do hold -Keys A -HoldMs 3000 >/dev/null 2>&1
  o=$(python -I shiphead.py $H); echo "  $o"
  h=$(echo "$o" | cut -d' ' -f1 | tr -d '+')
  if [ "$h" -ge $((WANT - 12)) ] && [ "$h" -le $((WANT + 12)) ]; then echo "on course"; exit 0; fi
  [ "$o" = "$last" ] && [ $i -gt 3 ] && { echo "the ship does not move or turn: stopped"; exit 1; }
  last="$o"
done
echo "not on course after 90 s"; exit 1
