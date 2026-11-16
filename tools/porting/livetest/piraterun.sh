#!/bin/bash
# piraterun.sh H G [minutes] : the other ships' sails on both copies (16-nov). Host at the wheel and guest on the deck
# of the moored ship (shipsession.sh). The ship leaves the harbour and goes on south-east at one gear, where the AI's
# ships sail (two pirates sank the player's ship there at 14:00 that day, so this run can end the same way), with the
# GUEST's copy in front (FRONT, sailto.py). All the while shipsails.py writes every ship's sail on both copies every
# five seconds to shots/pirate_sails.txt. At the end: how many looks had a sail that differed, the last looks, and
# what each mod traced (GimmickRelay trace on the host: every update of a sail and whether it went out; GimmickTake
# trace on the guest: every one that arrived, the state its copy was in, and the state when the sail was given back).
#   CDMP_SHIP=<key> when the ship is not 0xB010000A.
cd "$(dirname "$0")"; mkdir -p shots
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
H=$1; G=$2; MIN=${3:-10}
SP="$(cygpath -w "$(pwd)")"
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8); ship $(python -I shiphead.py $H | cut -d' ' -f1-3)"
(timeout $((MIN * 60 + 60)) python -I shipsails.py $H $G $((MIN * 60)) 5 > shots/pirate_sails.txt 2>&1 &)
FRONT=$G timeout $((MIN * 60)) python -I sailto.py $H -r 45 -keep -10366 -6275 -10315 -6290 -10230 -6320 -10150 -6345 -10000 -6480 -9850 -6620 -9700 -6700 -9550 -6800 2>&1 | grep -E "passed|there|still|aground|not there|raises" | cut -c1-150
$UG -Do shot -Out "$SP\\shots\\pirate_g.png" >/dev/null 2>&1
echo "ship: $(python -I shiphead.py $H | cut -d' ' -f1-3) health $(python -I actorhp.py $H ${CDMP_SHIP:-0xB010000A} 2>/dev/null | cut -d' ' -f2); guest: $(python -I aboard.py $G 2>/dev/null | tail -1 | cut -c1-80)"
echo "--- the sails, every 5 s (shots/pirate_sails.txt)"
tail -1 shots/pirate_sails.txt
echo "looks with each ship: $(grep -c ' ship ' shots/pirate_sails.txt) lines, $(grep -c DIFFERENT shots/pirate_sails.txt) with a difference"
grep " ship " shots/pirate_sails.txt | grep -v "${CDMP_SHIP#0x}" | tail -8 | cut -c1-260
echo "--- the host's trace (the last 14 of $(grep -a -c 'GimmickRelay trace' "$B/cdmp.host.log"))"
grep -a "GimmickRelay trace" "$B/cdmp.host.log" | tail -14 | cut -c1-230
echo "--- the guest's trace (the last 14 of $(grep -a -c 'GimmickTake trace' "$B/cdmp.guest.log"))"
grep -a "GimmickTake trace" "$B/cdmp.guest.log" | tail -14 | cut -c1-230
echo "DONE at $(date +%T): ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log")"
