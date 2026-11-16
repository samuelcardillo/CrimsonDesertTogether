#!/bin/bash
# boatcheck.sh H G [seconds] : the boat as the guest has it, at full sail, in one run (host at the wheel and the ship
# already on a safe course: shipsession.sh, then sailout.sh). With the HOST's copy in front (the guest's draws two to
# six frames a second: the hard case):
#   - the passenger: slidetest.sh for `seconds` (60): two more gears, the ship's speed and the guest's place on the
#     deck every three seconds, "OFF THE SHIP" if it is lost;
#   - the sail: its state on both copies every four seconds meanwhile (the guest's must be the host's);
#   - the crew: how far the guest draws each crew member from its post (boat/npcmon.py), 20 s with the host's copy in
#     front and 20 s with the guest's;
#   - what the guest's mod wrote about each time the game took its character off the deck.
cd "$(dirname "$0")"; mkdir -p shots
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
T="D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/boat"
H=$1; G=$2; SECS=${3:-60}
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"
eval "$(sed -n '/^crew() {/,/^}/p' sailcrew.sh)"
K=$(python -I sailstate.py $H key)
L0=$(grep -a -c "was in the air for" "$B/cdmp.guest.log")
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8); sail $K"
( for i in $(seq 1 $(( (SECS + 30) / 4 ))); do
    h=$(python -I sailstate.py $H $K | cut -d' ' -f1-3); g=$(python -I sailstate.py $G $K | cut -d' ' -f1-3)
    [ "$h" = "$g" ] && echo "same $h" || echo "DIFFERENT host $h guest $g"
    sleep 4
  done > shots/boatcheck_sail.txt 2>&1 & )
(python -I aboard.py $G $((SECS + 25)) 3 > shots/boatcheck_aboard.txt 2>&1 &)
bash slidetest.sh $H $G $SECS 2>&1 | grep "m/s  guest\|in $SECS s\|OFF" | awk 'NR%3==1 || /in [0-9]* s|OFF/' | cut -c1-120
for i in $(seq 1 60); do grep -q "^aboard in" shots/boatcheck_aboard.txt 2>/dev/null && break; sleep 2; done; echo "passenger: $(tail -1 shots/boatcheck_aboard.txt | cut -c1-230)"; grep -c "NOT aboard" shots/boatcheck_aboard.txt | sed "s/^/  looks in which it was NOT aboard: /"
# slidetest takes the sail in at its end: one gear again for the crew
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 9
echo "crew, the host's copy in front:"; $UH -Do shot -Out "$(cygpath -w "$(pwd)")\shots\boatcheck_h.png" >/dev/null 2>&1; crew 20
echo "crew, the guest's copy in front:"; $UG -Do shot -Out "$(cygpath -w "$(pwd)")\shots\boatcheck_g.png" >/dev/null 2>&1; crew 20
sleep 2
echo "sail, every 4 s: $(grep -c '^same' shots/boatcheck_sail.txt) the same on both copies, $(grep -c DIFFERENT shots/boatcheck_sail.txt) different; states seen: $(grep '^same' shots/boatcheck_sail.txt | sort | uniq -c | tr '\n' ';' | cut -c1-200)"
grep DIFFERENT shots/boatcheck_sail.txt | head -3
echo "times the game took the guest off the deck: $(( $(grep -a -c 'was in the air for' "$B/cdmp.guest.log") - L0 )); how they ended:"
grep -a "was in the air for" "$B/cdmp.guest.log" | tail -n +$((L0 + 1)) | sed 's/.*it ended: //; s/0x[0-9A-F]*/a part/; s/, [0-9.]* m across.*//' | cut -c1-70 | sort | uniq -c | sort -rn | head -6
echo "  landed this far from where it left the deck (m): $(grep -a "was in the air for" "$B/cdmp.guest.log" | tail -n +$((L0 + 1)) | grep -a -o "again, [0-9.]* m across" | awk '{print $2}' | sort -n | tr '\n' ' ' | cut -c1-160)"
echo "  longest without a part: $(grep -a "was in the air for" "$B/cdmp.guest.log" | tail -n +$((L0 + 1)) | grep -a -o "in the air for [0-9]* ms" | awk '{print $5}' | sort -n | tail -1) ms"
$UH -Do hold -Keys ALT -HoldMs 2500 >/dev/null 2>&1
echo "DONE at $(date +%T): ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log"); dumps of today $(ls "$B"/cdmp_crash*20261116* 2>/dev/null | wc -l)"
