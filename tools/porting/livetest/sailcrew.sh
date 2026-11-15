#!/bin/bash
# sailcrew.sh H G : the two things the ship work started from, on whatever build is installed (host at the wheel,
# guest on deck: shipsession.sh done; the kit's profiles).
#  SAIL: the state of the ship's sail (the vehicle's direct child at local 0,0,0) on the host's copy and on the
#        guest's, before anything, after a first gear (LSHIFT 1.5 s), after a second, and after the sail is taken in
#        (ALT 2.5 s), with what each mod logged in between (GimmickRelay on the host, GimmickTake on the guest). The
#        guest's copy should show the host's state each time.
#  CREW: under way, how far the guest draws each crew member from its post (the guest's own copy of what the crew
#        member hangs from on the host, with its local pose there: boat/npcmon.py), with the host's copy in front
#        (the ship at its real speed) and then with the guest's in front. Before EventKind::Riders the guest left them
#        10 to 30 m behind, over the water.
cd "$(dirname "$0")"; mkdir -p shots
SP="$(cygpath -w "$(pwd)")"
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
T="D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/boat"
H=$1; G=$2
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"
HL="$B/cdmp.host.log"; GL="$B/cdmp.guest.log"
K=$(python -I sailstate.py $H key)
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8); the sail is ${K:-NOT FOUND}"
[ -z "$K" ] && exit 1
hn=$(wc -l < "$HL"); gn=$(wc -l < "$GL")
state() {  # label : the sail on both copies, and what the mods wrote since the last call
  local h g
  h=$(python -I sailstate.py $H $K); g=$(python -I sailstate.py $G $K)
  echo "  $1: host $h | guest $g | $([ "${h%% *}" = "${g%% *}" ] && echo SAME || echo DIFFERENT)"
  echo "      since the last step: the host relayed $(tail -n +$((hn+1)) "$HL" | grep -a -c 'GimmickRelay.*relayed\|GimmickRelay \[') update(s); the guest's GimmickTake lines: $(tail -n +$((gn+1)) "$GL" | grep -a -c 'GimmickTake')"
  hn=$(wc -l < "$HL"); gn=$(wc -l < "$GL")
}
crew() {  # seconds : npcmon, summed up
  timeout $(( $1 + 30 )) python -I "$T/npcmon.py" $H $G $1 2 2>&1 | python -I -c "
import re, sys
errs, speeds, absent, riders = [], [], 0, set()
for l in sys.stdin:
    m = re.match(r'\s*[\d.]+ ship\s+([\d.]+) m/s', l)
    if not m: continue
    speeds.append(float(m.group(1)))
    for k, e in re.findall(r'(\w{8}) on \w{4} \(guest: on \w{4}, req \d+\)\s+([\d.]+) m off', l):
        errs.append(float(e)); riders.add(k)
    absent += len(re.findall(r'NOT on the guest|the guest has no', l))
if errs:
    errs.sort()
    print(f'    {len(riders)} crew members, {len(errs)} samples, ship {min(speeds):.1f} to {max(speeds):.1f} m/s: off their posts by {sum(errs) / len(errs):.2f} m on average, {errs[len(errs) * 95 // 100]:.2f} m at the 95th percentile, {errs[-1]:.2f} m at most; samples with a crew member missing on the guest: {absent}')
else:
    print('    no samples')"
}
state "before"
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 9
state "first gear"
if python -I sailstate.py $H $K | grep -q furled; then
  echo "  no gear: wheel released and taken again, LSHIFT once more"
  $UH -Do key -Keys R -HoldMs 150 >/dev/null 2>&1; sleep 3
  $UH -Do key -Keys E -HoldMs 150 >/dev/null 2>&1; sleep 4
  $UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 9
  state "first gear, second try"
fi
echo "  crew, the host's copy in front:"
crew 16
$UG -Do shot -Out "$SP\\shots\\sailcrew_g.png" >/dev/null 2>&1; sleep 3
echo "  crew, the guest's copy in front:"
crew 16
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 9
state "second gear"
$UH -Do hold -Keys ALT -HoldMs 2500 >/dev/null 2>&1; sleep 10
state "sail taken in"
echo "  totals: $(grep -a 'gimmick relay \[' "$HL" | tail -1 | sed -E 's/.*(gimmick relay \[[^|]*).*/host \1/' | cut -c1-220)"
echo "          $(grep -a 'gimmick take \[' "$GL" | tail -1 | sed -E 's/.*(gimmick take \[[^|]*).*/guest \1/' | cut -c1-220)"
echo "          $(grep -a 'VehicleFollow summary' "$GL" | tail -1 | sed -E 's/.*(\| riders:[^|]*).*/guest \1/' | cut -c1-330)"
echo "DONE at $(date +%T): ERR host $(grep -a -c '] ERR ' "$HL") guest $(grep -a -c '] ERR ' "$GL"); crash captures host $(grep -a -c 'crash capture (' "$HL") guest $(grep -a -c 'crash capture (' "$GL")"
