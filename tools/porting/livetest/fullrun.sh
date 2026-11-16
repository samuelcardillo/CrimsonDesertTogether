#!/bin/bash
# fullrun.sh H G : the owner's three observations of 16-nov in one sail, with BOTH copies drawing at full rate (start
# the session with FPS_CAP=30 GFX_LOW=1 bash shipsession.sh: without the cap AND the cheap video settings the copy
# behind draws 2 to 7 frames a second and none of this can be seen on one PC; when the games are closed afterwards,
# gfxfile.sh normal puts the guest's settings back). Host at the wheel, guest on the stern deck of the moored ship:
# start this as soon as the host has the wheel.
#   1. out of the harbour along the track that is all water (first gear), a screenshot of the guest's deck after
#      every steering input (shots/full_out_N.png);
#   2. on to open sea, east-south-east, gear by gear up to full sail. At each gear: the ship's speed, how far the
#      guest draws each crew member from its post (boat/npcmon.py ten times a second: the median, the 90th and 99th
#      percentile and the worst), whether the guest is still aboard, and four screenshots of the guest's view
#      (shots/full_gearN_M.png): the deck, the crew and the sea at that speed;
#   3. all the while, every ship's sail on both copies every five seconds (shots/full_sails.txt) and the twelve wave
#      trains of both copies at each gear;
#   4. the sail taken in, and what each mod said.
#   CDMP_SHIP=<key> when the ship is not 0xB010000A.
cd "$(dirname "$0")"; mkdir -p shots
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
T="D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/boat"
H=$1; G=$2
SP="$(cygpath -w "$(pwd)")"
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"
rm -f shots/full_*.png shots/full_sails.txt
fps() {  # role : presents a second over the last five seconds of that copy's log
  grep -a "Overlay: installed" "$B/cdmp.$1.log" | tail -2 | sed 's/.*presents \([0-9]*\) .*/\1/' | awk 'NR>1{printf "%.0f", ($1-p)/5} {p=$1}'
}
crewfast() {  # seconds : npcmon ten times a second, summed up
  timeout $(( $1 + 40 )) python -I "$T/npcmon.py" $H $G $1 0.1 2>&1 | python -I -c "
import re, sys
errs, speeds, riders, absent = [], [], set(), 0
for l in sys.stdin:
    m = re.match(r'\s*[\d.]+ ship\s+([\d.]+) m/s', l)
    if not m: continue
    speeds.append(float(m.group(1)))
    for k, e in re.findall(r'(\w{8}) on \w{4} \(guest: on \w{4}, req \d+\)\s+([\d.]+) m off', l):
        errs.append(float(e)); riders.add(k)
    absent += len(re.findall(r'NOT on the guest|the guest has no', l))
if errs:
    errs.sort(); n = len(errs)
    print(f'    {len(riders)} crew, {n} samples, ship {min(speeds):.1f} to {max(speeds):.1f} m/s: off their posts by {errs[n // 2]:.2f} m (median), {errs[n * 9 // 10]:.2f} m (90 %), {errs[n * 99 // 100]:.2f} m (99 %), {errs[-1]:.2f} m at most; missing on the guest in {absent} samples')
else:
    print('    no samples')"
}
gear() {  # a steering tap each way (the wheel offers its sail controls only after one), then one gear up
  $UH -Do hold -Keys A -HoldMs 200 >/dev/null 2>&1; $UH -Do hold -Keys D -HoldMs 200 >/dev/null 2>&1
  $UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1
  $UG -Do shot -Out "$SP\\shots\\front_tmp.png" >/dev/null 2>&1
}
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8); ship $(python -I shiphead.py $H | cut -d' ' -f1-3); guest $(python -I aboard.py $G 2>/dev/null | tail -1 | cut -c1-70); frames a second host $(fps host) guest $(fps guest)"
(timeout 900 python -I shipsails.py $H $G 780 5 > shots/full_sails.txt 2>&1 &)
timeout 40 python -I pitch.py $G -10 >/dev/null 2>&1
echo "--- 1. out of the harbour"
FRONT=$G SHOTS=full_out timeout 420 python -I sailto.py $H -r 45 -keep -10366 -6275 -10315 -6290 -10230 -6320 -10150 -6345 2>&1 | grep -E "passed|there|still|aground|not there|raises" | cut -c1-140
echo "guest: $(python -I aboard.py $G 2>/dev/null | tail -1 | cut -c1-80); frames a second host $(fps host) guest $(fps guest)"
echo "--- 2. open sea, gear by gear"
K=$(python -I sailstate.py $H key)
for n in 2 3; do
  gear; sleep 9
  # keep the bow east-south-east while measuring: the next waypoints of the track
  (FRONT=$G timeout 60 python -I sailto.py $H -r 60 -keep -g 0 -9782 -6383 -9300 -6450 >/dev/null 2>&1 &)
  echo "gear: sail $(python -I sailstate.py $H $K | cut -c1-30) (guest $(python -I sailstate.py $G $K | cut -c1-30)); ship $(python -I shiphead.py $H | cut -d' ' -f1-3)"
  for m in 1 2 3 4; do $UG -Do shot -Out "$SP\\shots\\full_gear${n}_$m.png" >/dev/null 2>&1; sleep 2; done
  crewfast 12
  echo "    guest: $(python -I aboard.py $G 2>/dev/null | tail -1 | cut -c1-80); frames a second host $(fps host) guest $(fps guest)"
  python -I seawaves.py $H $G 5 1 | tail -2 | cut -c1-200 | sed 's/^/    /'
done
echo "--- 3. the other ships' sails so far"
echo "looks: $(grep -c ' ship ' shots/full_sails.txt) lines, $(grep -c DIFFERENT shots/full_sails.txt) with a difference; ships seen: $(grep -o ' ship [0-9A-F]*' shots/full_sails.txt | sort -u | wc -l)"
grep " ship " shots/full_sails.txt | grep -v "${CDMP_SHIP:-0xB010000A}" | grep -v "B010000A" | tail -4 | cut -c1-230
echo "--- 4. the sail in"
for n in 1 2 3; do
  $UH -Do hold -Keys A -HoldMs 200 >/dev/null 2>&1; $UH -Do hold -Keys D -HoldMs 200 >/dev/null 2>&1
  $UH -Do hold -Keys ALT -HoldMs 4500 >/dev/null 2>&1; sleep 2
  case "$(python -I sailstate.py $H $K)" in *furled*|853c63b7*) break;; esac
done
$UG -Do shot -Out "$SP\\shots\\full_end.png" >/dev/null 2>&1
echo "sail $(python -I sailstate.py $H $K | cut -c1-30); ship $(python -I shiphead.py $H | cut -d' ' -f1-3) health $(python -I actorhp.py $H ${CDMP_SHIP:-B010000A} 2>/dev/null | cut -d' ' -f2); guest $(python -I aboard.py $G 2>/dev/null | tail -1 | cut -c1-80)"
echo "times the game took the guest off the deck: $(grep -a -c 'the game took this client.s own character off' "$B/cdmp.guest.log"); lone far ship samples not drawn: $(grep -a 'VehicleFollow summary' "$B/cdmp.guest.log" | tail -1 | grep -a -o 'lone far samples not drawn [0-9]*' | head -1)"
echo "DONE at $(date +%T): ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log")"
