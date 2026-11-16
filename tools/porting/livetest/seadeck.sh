#!/bin/bash
# seadeck.sh H G : the three things of 16-nov seen from the guest's deck, in one run (host at the wheel and guest on
# the stern deck of the moored ship: shipsession.sh). The GUEST's copy stays in front all the time (FRONT, sailto.py):
# a passenger whose copy is behind falls off the ship once it sails, and a copy behind draws 2 to 7 frames a second.
#   1. out of the harbour along the track that is all water, to (-10150, -6345), and the sail furled there;
#   2. the sea: the twelve wave trains of both copies (seawaves.py) and eight screenshots of the guest's view, three
#      seconds apart (shots/deck_g_N.png): the ship rides the swell and the water must stay off the deck;
#   3. the other ships' sails: every sail the host's mod has said again (GimmickRelay) on both copies, and what the
#      guest's mod did with each (GimmickTake);
#   4. the crew: one gear, then how far the guest draws each crew member from its post (boat/npcmon.py), at the
#      first gear and at the second.
#   CDMP_SHIP=<key> when the ship is not 0xB010000A.
cd "$(dirname "$0")"; mkdir -p shots
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
T="D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/boat"
H=$1; G=$2
SP="$(cygpath -w "$(pwd)")"
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"
eval "$(sed -n '/^crew() {/,/^}/p' sailcrew.sh)"
front() { $UG -Do shot -Out "$SP\\shots\\front_tmp.png" >/dev/null 2>&1; }
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8); ship $(python -I shiphead.py $H | cut -d' ' -f1-3); guest $(python -I aboard.py $G 2>/dev/null | tail -1 | cut -c1-80)"
echo "--- 1. out of the harbour, the guest's copy in front"
FRONT=$G timeout 900 python -I sailto.py $H -r 45 -10366 -6275 -10315 -6290 -10230 -6320 -10150 -6345 2>&1 | grep -E "passed|there|still|aground|not there|raises" | cut -c1-150
front; sleep 3
echo "guest: $(python -I posnow.py $G | cut -c1-90)"
echo "--- 2. the sea"
python -I seawaves.py $H $G | cut -c1-210
for i in 1 2 3 4 5 6 7 8; do $UG -Do shot -Out "$SP\\shots\\deck_g_$i.png" >/dev/null 2>&1; sleep 3; done
python -I seawaves.py $H $G 20 1 | tail -2 | cut -c1-230
echo "--- 3. the other ships' sails"
for k in $(grep -a "is said to user 2 again\|goes to user 2 again" "$B/cdmp.host.log" | grep -a -o "0x[0-9A-F]\{8\}" | grep -v "0x29A56175\|0xC3738DA8\|0x0FE0996E\|0x8DC1B584" | sort -u | tail -12); do
  key=${k#0x}
  echo "  sail $k: host $(python -I sailstate.py $H $key 2>&1 | cut -c1-40) | guest $(python -I sailstate.py $G $key 2>&1 | cut -c1-40) | guest's mod: $(grep -a "GimmickTake.*$k" "$B/cdmp.guest.log" | tail -1 | sed 's/.*): //' | cut -c1-90)"
done
echo "  said again by the host so far: $(grep -a -c 'is said to user 2 again' "$B/cdmp.host.log") lines (the log stops at 12)"
echo "--- 4. the crew"
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; front; sleep 8
echo "first gear, ship $(python -I shiphead.py $H | cut -d' ' -f1-3):"; crew 20
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; front; sleep 8
echo "second gear, ship $(python -I shiphead.py $H | cut -d' ' -f1-3):"; crew 20
$UG -Do shot -Out "$SP\\shots\\deck_g_sail.png" >/dev/null 2>&1
$UH -Do hold -Keys ALT -HoldMs 4000 >/dev/null 2>&1; front; sleep 3
$UH -Do hold -Keys ALT -HoldMs 4000 >/dev/null 2>&1; front
echo "guest: $(python -I posnow.py $G | cut -c1-90)"
echo "DONE at $(date +%T): ship $(python -I shiphead.py $H | cut -d' ' -f1-3); ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log")"
