#!/bin/bash
# reboard.sh pid : one character, from the water or the quay next to the moored ship, up the port ladder and on to
# the stern deck (shipsession.sh's guestboard stage alone: for a character that fell off or never got up, without
# touching the other one). Prints where it ends: a parent 0xB... and a local position mean it is on the ship.
cd "$(dirname "$0")"
P=$1
UU="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $P"
set -- $(python -I shiplocal.py $P -9.0 -7.7 -5.3 -4.3 -3.0 -4.21)
timeout 240 python -I walk.py $P -s 1.2 -n 14 $1 $2 2>&1 | grep "^at" | tail -1
set -- $(python -I shiplocal.py $P -9.0 -7.7 -5.3 -4.3 -3.0 -4.21)
timeout 160 python -I walk.py $P -s 0.45 -n 10 $3 $4 2>&1 | grep "^at" | tail -1
set -- $(python -I shiplocal.py $P -9.0 -7.7 -5.3 -4.3 -3.0 -4.21)
timeout 40 python -I face.py $P $5 $6 >/dev/null 2>&1
$UU -Do hold -Keys W -HoldMs 3500 >/dev/null 2>&1; sleep 1; $UU -Do hold -Keys W -HoldMs 3000 >/dev/null 2>&1; sleep 1.5
$UU -Do hold -Keys W -HoldMs 900 >/dev/null 2>&1; sleep 1
echo "ladder: $(python -I posnow.py $P)"
timeout 220 python -I shipwalk.py $P -s 0.4 -n 9 -2.9 -1.2 -2.85 0.6 -2.8 3.6 -1.2 4.6 2>&1 | grep "^at" | tail -1
echo "deck: $(python -I posnow.py $P)"
