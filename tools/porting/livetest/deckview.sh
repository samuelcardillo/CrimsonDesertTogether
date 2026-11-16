#!/bin/bash
# deckview.sh H G : the sea from the guest's deck (16-nov, sea_sync): host at the wheel and guest on the stern deck
# of the moored ship (shipsession.sh). The ship leaves the harbour along the track that is all water to
# (-10150, -6345), where the swell is high, with the GUEST's copy in front the whole way: after every key hold on the
# host's window the guest's comes back to the front and a screenshot of it is kept (shots/deckview_N.png: its
# character on the stern deck, the ship under it and the sea around). At the end eight more, two seconds apart
# (shots/deckview_end_N.png), the twelve wave trains of both copies, and whether the guest is still aboard. The sail
# is NOT furled at the end (holding ALT keeps the host's copy in front for four seconds, and a passenger whose copy
# is behind falls off in a swell); furl it afterwards with gearcycle.py or by hand.
#   CDMP_SHIP=<key> when the ship is not 0xB010000A.
cd "$(dirname "$0")"; mkdir -p shots
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
H=$1; G=$2
SP="$(cygpath -w "$(pwd)")"
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"
rm -f shots/deckview_*.png
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8); ship $(python -I shiphead.py $H | cut -d' ' -f1-3); guest $(python -I aboard.py $G 2>/dev/null | tail -1 | cut -c1-80)"
timeout 40 python -I pitch.py $G -12 >/dev/null 2>&1
FRONT=$G SHOTS=deckview timeout 900 python -I sailto.py $H -r 45 -keep -10366 -6275 -10315 -6290 -10230 -6320 -10150 -6345 2>&1 | grep -E "passed|there|still|aground|not there|raises" | cut -c1-150
for i in 1 2 3 4 5 6 7 8; do $UG -Do shot -Out "$SP\\shots\\deckview_end_$i.png" >/dev/null 2>&1; sleep 2; done
echo "guest: $(python -I aboard.py $G 2>/dev/null | tail -1 | cut -c1-90)"
python -I seawaves.py $H $G | sed -n 1p
python -I seawaves.py $H $G 10 1 | tail -2 | cut -c1-200
echo "DONE at $(date +%T): ship $(python -I shiphead.py $H | cut -d' ' -f1-3); $(ls shots/deckview_*.png 2>/dev/null | wc -l) screenshots; ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log")"
