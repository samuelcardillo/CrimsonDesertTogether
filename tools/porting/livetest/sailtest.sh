#!/bin/bash
# sailtest.sh H G : with the host at the ship's wheel and the guest on deck (shipsession.sh done), the sail and lantern
# experiments of 2026-11-14. Profiles: the host's with gimmick_relay=1 (and send_trace=2609,2758), the guest's with
# gimmick_take=1 and create_retry=1. One block of lines per step; screenshots in shots/sail_*.png (each copy turns to
# the bow and looks up at the rigging).
#  0. the ship's parts on both copies (the lantern parts: create_retry), a first pair of screenshots;
#  1. first gear on the host (LSHIFT 1.5 s): what the host relayed, what the guest did with it, both views, the sail's
#     key from the host's capture, where the branch hashes live on both copies;
#  2. second gear; 3. sail down (ALT 2.5 s);
#  4. only if the guest never became the sail's controller by itself: boat/setctl.py --write, and first gear again.
cd "$(dirname "$0")"; mkdir -p shots
SP="$(cygpath -w "$(pwd)")"
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
T="D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/boat"
H=$1; G=$2
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
HL="$B/cdmp.host.log"; GL="$B/cdmp.guest.log"
shots() {  # tag [dy] : the guest first (it is on deck on foot), then the host; the camera goes back down afterwards
  local DY=${2:-220}
  for who in g h; do
    [ $who = g ] && P=$G || P=$H
    local U="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $P"
    read X Z <<< "$(python -I shiplocal.py $P 0 -40)"
    timeout 40 python -I face.py $P $X $Z >/dev/null 2>&1
    $U -Do look -Dx 0 -Dy -$DY -Steps 10 >/dev/null 2>&1; sleep 0.7
    $U -Do shot -Out "$SP\\shots\\sail_$1_$who.png" >/dev/null 2>&1
    $U -Do look -Dx 0 -Dy $DY -Steps 10 >/dev/null 2>&1; sleep 0.3
  done
  echo "  shots: shots/sail_$1_h.png shots/sail_$1_g.png"
}
hostbin() { ls -t "$B"/cdmp_packets.host_*.bin | head -1; }
guestbin() { ls -t "$B"/cdmp_packets.guest_*.bin | head -1; }
sailkey() {  # the key of the newest GimmickBranchStateReq of the host's client with a "gear set" hash (first or
  # second: the first hold can go straight to the second state). The whole capture is read (a minute for 1 GB): with
  # the first 400 requests only, as before, a sail set late in a session was never found (14-nov 14:55).
  python -I "$T/pktdump.py" "$(hostbin)" GimmickBranchStateReq 0 0 1e12 4000000 2>/dev/null | grep -i -E "4d d9 5d be|e4 80 ff 9b" | tail -1 |
    sed -E 's/.*\| ([0-9a-f]{2}) ([0-9a-f]{2}) ([0-9a-f]{2}) ([0-9a-f]{2}) .*/\4\3\2\1/'
}
hn=$(wc -l < "$HL"); gn=$(wc -l < "$GL")
since() {  # what both mods wrote since the last call
  echo "  host, since the last step:"
  tail -n +$((hn+1)) "$HL" | grep -a -E "GimmickRelay|SendTrace: opcode|\] ERR |\] WRN " | cut -c1-420 | head -14
  echo "  guest, since the last step:"
  tail -n +$((gn+1)) "$GL" | grep -a -E "GimmickTake|CreateRetry|WITHHELD|Game error|\] ERR |\] WRN " | cut -c1-420 | head -14
  hn=$(wc -l < "$HL"); gn=$(wc -l < "$GL")
}
alive() { python -I posnow.py $H >/dev/null 2>&1 && python -I posnow.py $G >/dev/null 2>&1; }

echo "STEP 0 at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8); ship $(python -I "$T/actorinfo.py" $H B010000A 2>&1 | sed -E 's/.*local (\([^)]*\)).*/\1/')"
python -I "$T/shiptree.py" $H $G > shots/shiptree_now.txt 2>&1
head -1 shots/shiptree_now.txt
echo "  the guest's create_retry so far (the whole tree is in shots/shiptree_now.txt):"
grep -a "CreateRetry" "$GL" | cut -c1-420 | tail -12
grep -a -c "AsyncPostProcessForCreate" "$GL" | sed 's/^/  creation failures in the guest log: /'
shots g0
since

$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
echo "STEP 1 at $(date +%T): first gear asked"
shots g1
since
K=$(sailkey)
echo "  the sail's key from the host's capture: ${K:-NOT FOUND}"
if [ -z "$K" ]; then
  # The gear does not always go in right after taking the wheel (14-nov): let go (R), take it again (E), once more.
  echo "  no gear in the host's capture: wheel released and taken again, LSHIFT once more"
  $UH -Do key -Keys R -HoldMs 150 >/dev/null 2>&1; sleep 3
  $UH -Do key -Keys E -HoldMs 150 >/dev/null 2>&1; sleep 4
  $UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 12
  K=$(sailkey)
  echo "  the sail's key from the host's capture: ${K:-NOT FOUND}"
fi
[ -z "$K" ] && { echo "no branch request with a gear hash: stopping (the ship did not set sail?)"; exit 1; }
python -I "$T/branchfind.py" $H $G $K 2>&1 | grep -E "==|=" | cut -c1-260 | head -24
python -I "$T/setctl.py" $G $K 2>&1 | tail -1
alive || { echo "a copy is gone after step 1: stopping"; exit 1; }

$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
echo "STEP 2 at $(date +%T): second gear asked"
shots g2
since
alive || { echo "a copy is gone after step 2: stopping"; exit 1; }

$UH -Do hold -Keys ALT -HoldMs 2500 >/dev/null 2>&1; sleep 9
echo "STEP 3 at $(date +%T): sail down asked"
shots g0b
since
python -I "$T/branchfind.py" $H $G $K 2>&1 | grep -E "==|=" | cut -c1-260 | head -24

if ! grep -a -q "GimmickTake \[1\].*is now this client's user" "$GL"; then
  echo "STEP 4 at $(date +%T): the guest never took the sail by itself; written by hand now (setctl.py), first gear again"
  python -I "$T/setctl.py" $G $K own --write 2>&1 | tail -2
  $UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
  shots g1c
  since
  $UH -Do hold -Keys ALT -HoldMs 2500 >/dev/null 2>&1; sleep 8
  shots g0c
  since
fi

echo "SUMMARY at $(date +%T)"
grep -a "gimmick relay \[" "$HL" | tail -1 | sed -E 's/.*(\| gimmick relay \[[^|]*(\|[^|]*){0,3}).*/  host \1/' | cut -c1-500
grep -a "gimmick take \[" "$GL" | tail -1 | sed -E 's/.*(\| gimmick take \[[^|]*(\|[^|]*){0,4}).*/  guest \1/' | cut -c1-500
grep -a "create retry \[" "$GL" | tail -1 | sed -E 's/.*(\| create retry \[[^|]*(\|[^|]*){0,3}).*/  guest \1/' | cut -c1-700
echo "  what the guest's client sent for the sail (its capture is taken before the mod withholds: see the WITHHELD lines):"
python -I "$T/keyhist.py" "$(guestbin)" $K $K 0 1e12 x 2>/dev/null | cut -c1-120 | head -10
echo "  the host character's stat entries (statdump.py: which one is stamina on this build?):"
python -I statdump.py $H 2>&1 | cut -c1-140 | head -40
echo "DONE at $(date +%T): ship $(python -I "$T/actorinfo.py" $H B010000A 2>&1 | sed -E 's/.*local (\([^)]*\)).*/\1/'); ERR host $(grep -a -c '] ERR ' "$HL") guest $(grep -a -c '] ERR ' "$GL")"
