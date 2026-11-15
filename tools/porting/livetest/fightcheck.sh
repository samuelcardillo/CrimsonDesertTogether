#!/bin/bash
# fightcheck.sh stage H G [args] : a fight seen from both copies (the guest hits, the host's copy has the enemy).
#   travel            both to the Abyss Nexus next to Valvegard Garrison (-4710, -5348), where enemies are at hand
#                     (docs/ESTADO-3527.md); the host first, then the guest
#   list [radius]     the type-3 actors near each copy's own character, with their health (nearactors.py)
#   hit KEY [n]       the guest turns to the actor KEY, walks up to it and presses the left button n times (12);
#                     every four presses: the actor's health on the host's copy and on the guest's, and the guest's own
#   kill KEY [n]      the same without the pauses, until the actor is gone or at 0 health or n presses (40) are
#                     spent: turns to it every three presses and looks at its health on the host's copy then
#   report            what the host's log says of the fight (User2Funnels rank / dead), errors, crash captures
# Nothing here writes game memory: keys and the mouse to the game's window, and reads.
cd "$(dirname "$0")"; mkdir -p shots
SP="$(cygpath -w "$(pwd)")"
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
STAGE=$1; H=$2; G=$3; shift 3
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"
near_nexus() {  # pid : 0 when the copy's character is within 80 m of the nexus
  python -I posnow.py $1 2>/dev/null | python -I -c "
import sys, math
t = sys.stdin.read().split()
ok = len(t) >= 3 and math.hypot(float(t[0]) + 4710, float(t[2]) + 5348) < 80
sys.exit(0 if ok else 1)"
}
tp() {  # pid role : fast travel to the Valvegard nexus, three attempts
  for try in 1 2 3; do
    timeout 400 python -I map_tp.py $1 $2 -4710 -5348 2>&1 | tail -1
    for i in $(seq 1 30); do near_nexus $1 && return 0; sleep 3; done
    pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $1 -Do key -Keys ESC -HoldMs 120 >/dev/null 2>&1; sleep 2
  done
  return 1
}
case "$STAGE" in
travel)
  tp $H host || { echo "host travel FAILED: $(python -I posnow.py $H)"; exit 1; }
  echo "host at the nexus at $(date +%T): $(python -I posnow.py $H | cut -c1-40)"
  sleep 4
  tp $G guest || { echo "guest travel FAILED: $(python -I posnow.py $G)"; exit 1; }
  echo "guest at the nexus at $(date +%T): $(python -I posnow.py $G | cut -c1-40)"
  ;;
list)
  R=${1:-60}
  echo "host:";  timeout 60 python -I nearactors.py $H $R 3 2>&1 | head -14
  echo "guest:"; timeout 60 python -I nearactors.py $G $R 3 2>&1 | head -14
  ;;
hit)
  K=$1; N=${2:-12}
  echo "before: host $(python -I actorhp.py $H $K) || guest $(python -I actorhp.py $G $K)"
  # its place on the guest's copy: "KEY cur/max at D m (x, z)"
  set -- $(python -I actorhp.py $G $K | sed -E 's/.* m \(([-0-9.]+), ([-0-9.]+)\).*/\1 \2/')
  case "$1" in ''|*[!-0-9.]*) echo "the guest's copy has no $K"; exit 1;; esac
  timeout 120 python -I walk.py $G -s 2.2 -n 8 $1 $2 2>&1 | grep "^at" | tail -1
  for i in $(seq 1 $N); do
    if [ $((i % 4)) = 1 ]; then
      set -- $(python -I actorhp.py $G $K | sed -E 's/.* m \(([-0-9.]+), ([-0-9.]+)\).*/\1 \2/')
      case "$1" in ''|*[!-0-9.]*) ;; *) timeout 30 python -I face.py $G $1 $2 >/dev/null 2>&1;; esac
    fi
    $UG -Do lmb -HoldMs 90 >/dev/null 2>&1; sleep 0.6
    [ $((i % 4)) = 0 ] && echo "after $i: host $(python -I actorhp.py $H $K) || guest $(python -I actorhp.py $G $K)"
  done
  $UG -Do shot -Out "$SP\\shots\\fight_guest.png" >/dev/null 2>&1
  ;;
kill)
  K=$1; N=${2:-40}
  echo "before: host $(python -I actorhp.py $H $K) || guest $(python -I actorhp.py $G $K)"
  set -- $(python -I actorhp.py $G $K | sed -E 's/.* m \(([-0-9.]+), ([-0-9.]+)\).*/\1 \2/')
  case "$1" in ''|*[!-0-9.]*) echo "the guest's copy has no $K"; exit 1;; esac
  timeout 60 python -I walk.py $G -s 2.0 -n 6 $1 $2 2>&1 | grep "^at" | tail -1
  for i in $(seq 1 $N); do
    if [ $((i % 3)) = 1 ]; then
      L=$(python -I actorhp.py $G $K)
      case "$L" in *absent*) echo "after $((i - 1)): gone from the guest's copy"; break;; esac
      set -- $(echo "$L" | sed -E 's/.* m \(([-0-9.]+), ([-0-9.]+)\).*/\1 \2/')
      case "$1" in ''|*[!-0-9.]*) ;; *) timeout 20 python -I face.py $G $1 $2 >/dev/null 2>&1;; esac
      HL=$(python -I actorhp.py $H $K); echo "after $((i - 1)): host $HL"
      case "${HL%%|*}" in *" 0/"*|*absent*) break;; esac  # the actor's part of the line, not the host's own health
    fi
    $UG -Do lmb -HoldMs 90 >/dev/null 2>&1; sleep 0.45
  done
  echo "end: host $(python -I actorhp.py $H $K) || guest $(python -I actorhp.py $G $K)"
  $UG -Do shot -Out "$SP\\shots\\fight_kill.png" >/dev/null 2>&1
  ;;
report)
  echo "host log: User2Funnels rank lines $(grep -a -c 'User2Funnels rank' "$B/cdmp.host.log"), dead lines $(grep -a -c 'User2Funnels dead' "$B/cdmp.host.log")"
  grep -a 'User2Funnels dead' "$B/cdmp.host.log" | tail -3 | cut -c1-260
  echo "ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log"); crash captures host $(grep -a -c 'crash capture (' "$B/cdmp.host.log") guest $(grep -a -c 'crash capture (' "$B/cdmp.guest.log")"
  ;;
*) echo "stages: travel, list, hit, kill, report"; exit 2;;
esac
