#!/bin/bash
# seatest.sh H G [seconds] : the sea as the guest draws it (16-nov, sea_sync). Host at the wheel and the ship under way
# at sea (shipsession.sh, then sailaway.py). What it says:
#   - the twelve wave trains of both copies side by side, once (seawaves.py), and for `seconds` (30) how far apart they
#     stay: with sea_sync the direction and the origin of every train are the host's and the phase is a frame or two
#     behind; without it nothing matches;
#   - the same with the guest's copy in front (the copy behind draws 2 to 7 frames a second, and a train of 21 m turns
#     2.3 rad in a second: its phase lags by one of ITS frames);
#   - what each mod said (SeaSync lines), and a screenshot of each copy every five seconds while its copy is in front
#     (shots/sea_h_N.png, shots/sea_g_N.png): the guest stands on the stern deck, the water over the deck would show.
cd "$(dirname "$0")"; mkdir -p shots
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
H=$1; G=$2; SECS=${3:-30}
SP="$(cygpath -w "$(pwd)")"
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8); ship $(python -I shiphead.py $H)"
shots() {  # ui prefix n : n screenshots of that copy, five seconds apart
  for i in $(seq 1 $3); do $1 -Do shot -Out "$SP\\shots\\sea_$2_$i.png" >/dev/null 2>&1; sleep 5; done
}
echo "--- the host's copy in front"
$UH -Do shot -Out "$SP\\shots\\sea_h_0.png" >/dev/null 2>&1; sleep 2
python -I seawaves.py $H $G | cut -c1-230
( shots "$UH" h $((SECS / 5)) & )
python -I seawaves.py $H $G $SECS 1 | tail -2 | cut -c1-230
sleep 3
echo "--- the guest's copy in front"
$UG -Do shot -Out "$SP\\shots\\sea_g_0.png" >/dev/null 2>&1; sleep 4
( shots "$UG" g $((SECS / 5)) & )
python -I seawaves.py $H $G $SECS 1 | tail -2 | cut -c1-230
sleep 3
echo "--- what the mods said"
grep -a "SeaSync" "$B/cdmp.host.log" | tail -2 | cut -c1-330
grep -a "SeaSync" "$B/cdmp.guest.log" | grep -a -v "the sea object" | tail -4 | cut -c1-520
echo "DONE at $(date +%T): ship $(python -I shiphead.py $H); ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log"); dumps of today $(ls "$B"/cdmp_crash*20261116* 2>/dev/null | wc -l)"
