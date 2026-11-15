#!/bin/bash
# mountsession.sh [stage] [H G] : a fresh two-copy session up to "both players on the hilltop of the Vellua nexus
# (Grace Estate), the host's mount called and the host standing next to it" (~12 min). Then: mounttest.sh H G.
#   stages: launch (default: everything), join, travel, call. A later stage needs the pids.
#   The host's mount of 16-nov-2026 is a Wyvern (Inventory > Special Mount, already summoned in its save): H calls it
#   ten metres from the character; E rides, R dismounts, W + LSHIFT runs at about 9 m/s.
cd "$(dirname "$0")"
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
STAGE=${1:-launch}; H=$2; G=$3
order="launch join travel call"
from() { case " ${order#*$STAGE} " in *" $1 "*) return 0;; esac; [ "$1" = "$STAGE" ]; }
pid_of() { sed -n "s/.*pids host \([0-9]*\) guest \([0-9]*\).*/\\$1/p" pids.txt 2>/dev/null | tail -1; }
inworld() {  # pid : it has a position that is not the loading placeholder (0, 1000, 0)
  python -I posnow.py $1 2>/dev/null | python -I -c "
import sys
t = sys.stdin.read().split()
sys.exit(0 if len(t) > 2 and abs(float(t[0])) > 1 else 1)"
}
tp() {  # pid role : fast travel to the Vellua nexus, three attempts
  for try in 1 2 3; do
    timeout 400 python -I map_tp.py $1 $2 -10623.2 -6109.3 2>&1 | tail -1
    for i in $(seq 1 30); do
      x=$(python -I posnow.py $1 2>/dev/null | python -I -c "
import sys
t = sys.stdin.read().split()
print('there' if len(t) > 8 and abs(float(t[4]) + 10623.2) < 3 and abs(float(t[6]) + 6109.3) < 3 else 'no')")
      [ "$x" = there ] && return 0; sleep 3
    done
    pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $1 -Do key -Keys ESC -HoldMs 120 >/dev/null 2>&1; sleep 2
  done
  return 1
}
if from launch; then
  # Into a file and in the background: the two games inherit the launcher's output pipe, so `$(... | tail)` would
  # wait until both copies close (16-nov: this script sat here for twenty minutes).
  rm -f pids.txt launchM.txt
  pwsh -NoProfile -ExecutionPolicy Bypass -File launch_house.ps1 > launchM.txt 2>&1 < /dev/null &
  for i in $(seq 1 200); do
    grep -q "host in world" launchM.txt 2>/dev/null && break
    grep -q -E "launch failed|is gone|Exception" launchM.txt 2>/dev/null && { tail -c 600 launchM.txt; exit 1; }
    sleep 2
  done
  H=$(pid_of 1); G=$(pid_of 2)
  [ -n "$H" ] && [ -n "$G" ] || { echo "launch FAILED: $(tail -c 300 launchM.txt)"; exit 1; }
  echo "launched: host $H guest $G at $(date +%T)"
  if grep -a -q "no trampoline memory" "$B/cdmp.host.log"; then echo "host hooks REFUSED (no trampoline memory): start again"; exit 1; fi
fi
if from join; then
  pwsh -NoProfile -ExecutionPolicy Bypass -File run_join.ps1 -Attach -AttachHost $H -AttachGuest $G > joinM.txt 2>&1 < /dev/null
  for i in $(seq 1 80); do inworld $G && break; sleep 3; done
  inworld $G || { echo "join FAILED: the guest has no character ($(tail -c 200 joinM.txt))"; exit 1; }
  echo "joined at $(date +%T): guest at $(python -I posnow.py $G | cut -d' ' -f1-3)"
fi
if from travel; then
  tp $H host || { echo "travel FAILED (host)"; exit 1; }
  tp $G guest || { echo "travel FAILED (guest)"; exit 1; }
  echo "both at the Vellua nexus at $(date +%T)"
fi
if from call; then
  UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
  $UH -Do key -Keys W -HoldMs 1500 >/dev/null 2>&1
  $UH -Do key -Keys H -HoldMs 150 >/dev/null 2>&1
  sleep 7
  M=$(python -I nearactors.py $H 40 5 2>/dev/null | sed -n '2p')
  [ -n "$M" ] || { echo "call FAILED: no vehicle actor within 40 m of the host"; exit 1; }
  set -- $(echo "$M" | python -I -c "
import re, sys
t = sys.stdin.read()
m = re.search(r'at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)', t)
print(t.split()[0], m.group(1), m.group(3))")
  timeout 100 python -I walk.py $H -s 2.2 $2 $3 2>&1 | grep "^at" | tail -1
  python -I face.py $H $2 $3 >/dev/null 2>&1
  echo "mount $1 called at ($2, $3), the host next to it: pids $H $G"
fi
