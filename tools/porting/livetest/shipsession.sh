#!/bin/bash
# shipsession.sh [stage] [H G] : a fresh two-copy session up to "host at the ship's wheel, guest on the stern deck".
#   stages: launch (default: everything), hopkins, join, guestboard, hostboard. A later stage needs the pids.
#   walk H G: two copies already running with the guest joined (after mountsession.sh, say): everything but the launch,
#   starting with the host's fast travel to the Vellua nexus and its walk to Hopkins.
#   deploy + launch, host to Vellua, Hopkins (R held 2 s at the tested spot), ship retrieved, guest joins and boards by
#   the port ladder, host boards and takes the wheel. Each stage prints one line; stops at the first that fails.
#   SAIL_EXP=1 in the environment: the two copies start with profiles/host-sail-exp.ini and profiles/guest-sail-exp.ini
#   (the kit's profiles plus the sail and lantern experiments of 2026-11-14: gimmick_relay, send_trace, gimmick_take,
#   create_retry, create_retry_lost), what sailtest.sh expects.
cd "$(dirname "$0")"
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
SP="$(cygpath -w "$(pwd)")"
STAGE=${1:-launch}; H=$2; G=$3
order="launch walk hopkins join guestboard hostboard"
from() { case " ${order#*$STAGE} " in *" $1 "*) return 0;; esac; [ "$1" = "$STAGE" ]; }

tp() {  # pid role : fast travel to the Vellua nexus, three attempts
  for try in 1 2 3; do
    timeout 400 python -I map_tp.py $1 $2 -10623.2 -6109.3 2>&1 | tail -1
    for i in $(seq 1 30); do x=$(python -I posnow.py $1 2>/dev/null | cut -d' ' -f1); case "$x" in -106*) return 0;; esac; sleep 3; done
    pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $1 -Do key -Keys ESC -HoldMs 120 >/dev/null 2>&1; sleep 2
  done
  return 1
}
past_wagon() {  # pid : from the Vellua nexus to (-10485, -6140), north of the market wagon that stands at
  # (-10501.8, -6145.6) some days: on 14-nov 14:20 the straight run to (-10480, -6150) put the character on its bed,
  # between its sacks, where W does nothing (only another fast travel got it out)
  timeout 280 python -I goto3576.py $1 -10516 -6140 3 40 2>&1 | tail -1
  timeout 240 python -I walk.py $1 -s 1.0 -n 14 -10508 -6137 -10495 -6137 -10485 -6140 2>&1 | grep "^at" | tail -1
}
where() {  # pid : deck (stands on the ship), ladder (hangs on one), wheel (holds it) or off (no parent: water, quay)
  # posnow: "lx ly lz world wx wy wz parent 0xKEY". On a ladder gimmick the local x stays within 0.8 m of 0 and y is
  # 0.6 or more; at the wheel x and y are 0; on the deck by the port ladder x is near -4, on the stern deck -1.5 to -3.
  python -I posnow.py $1 2>/dev/null | python -I -c "
import sys
t = sys.stdin.read().split()
if len(t) < 9 or t[-1] == '0x0': print('off')
else:
    x, y = abs(float(t[0])), abs(float(t[1]))
    print('wheel' if x < 0.3 and y < 0.3 else 'ladder' if x < 0.8 else 'deck')"
}
board() {  # pid : swim to the port side, climb the ladder (the point and the heading are taken in the ship's frame)
  local P=$1; local UU="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $P"
  timeout 280 python -I walk.py $P -s 1.5 -n 8 -10455.5 -6186 -10430 -6196 -10402 -6203 2>&1 | grep "^at" | tail -1
  set -- $(python -I shiplocal.py $P -9.0 -7.7 -5.3 -4.3 -3.0 -4.21)
  timeout 240 python -I walk.py $P -s 1.2 -n 14 $1 $2 2>&1 | grep "^at" | tail -1
  set -- $(python -I shiplocal.py $P -9.0 -7.7 -5.3 -4.3 -3.0 -4.21)
  timeout 160 python -I walk.py $P -s 0.45 -n 10 $3 $4 2>&1 | grep "^at" | tail -1
  set -- $(python -I shiplocal.py $P -9.0 -7.7 -5.3 -4.3 -3.0 -4.21)
  timeout 40 python -I face.py $P $5 $6 >/dev/null 2>&1
  # Up the ladder in short holds, looking after each where the character is. (Until 15-nov this was W for 3.5 s, 3 s
  # and 0.9 s without looking: that night the first hold already reached the deck and the next ones walked the
  # character across it and down a ladder again: the guest ended in the water, the host hanging on the ladder, and the
  # stages still said "on deck" and "helm".)
  for n in 1 2 3 4 5 6 7 8; do
    $UU -Do hold -Keys W -HoldMs 1200 >/dev/null 2>&1; sleep 1.2
    [ "$(where $P)" = deck ] && break
  done
  # A step inboard: right at the ladder's top a short W the wrong way takes the ladder again.
  if [ "$(where $P)" = deck ] && python -I posnow.py $P | python -I -c "import sys; sys.exit(0 if float(sys.stdin.read().split()[0]) < -3.0 else 1)"; then
    $UU -Do hold -Keys W -HoldMs 450 >/dev/null 2>&1; sleep 1
  fi
}

if from launch; then
  rm -f pids.txt launchX.txt
  INI=()
  [ -n "$SAIL_EXP" ] && INI=(-HostIni "$SP\\profiles\\host-sail-exp.ini" -GuestIni "$SP\\profiles\\guest-sail-exp.ini")
  # FPS_CAP=<n> in the environment: both copies start with the kit's profiles plus [general] fps_cap=<n> (the mod's
  # own frame limit, a test setting): the copy in front no longer takes the whole GPU, so the one behind keeps
  # drawing (without it: 2 to 7 frames a second and slow motion; 16-nov).
  if [ -n "$FPS_CAP" ]; then
    C="D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/configs"
    for r in host guest; do sed "s/^\[general\]\r\?$/&\nfps_cap=$FPS_CAP/" "$C/cdmp.3527-2inst-$r.ini" > "profiles/$r-cap.ini"; done
    INI=(-HostIni "$SP\\profiles\\host-cap.ini" -GuestIni "$SP\\profiles\\guest-cap.ini")
  fi
  # GFX_LOW=1 in the environment (with FPS_CAP=30: the two together are what gives BOTH copies 30 frames a second on
  # one PC): the guest starts with the cheap video settings (gfxfile.sh low, by file) and the host gets them through
  # its menu once it is in the world (gfxset.sh). When the test is over and the games are closed: gfxfile.sh normal.
  [ -n "$GFX_LOW" ] && { bash gfxfile.sh low || exit 1; }
  # ASI=<a cdmp.asi> in the environment: that file instead of the built one (a released kit's, to compare).
  [ -n "$ASI" ] && INI+=(-Asi "$(cygpath -w "$ASI")")
  pwsh -NoProfile -ExecutionPolicy Bypass -File "$SP\launch_house.ps1" "${INI[@]}" > launchX.txt 2>&1 < /dev/null &
  for i in $(seq 1 200); do grep -q "host in world" launchX.txt 2>/dev/null && break; grep -q -E "launch failed|is gone|Exception" launchX.txt 2>/dev/null && { tail -c 800 launchX.txt; exit 1; }; sleep 2; done
  H=$(sed -n 's/.*pids host \([0-9]*\) guest \([0-9]*\).*/\1/p' pids.txt); G=$(sed -n 's/.*pids host \([0-9]*\) guest \([0-9]*\).*/\2/p' pids.txt)
  echo "STAGE launch: pids $H $G; host in world at $(date +%T); asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8)"
  [ -z "$H" ] && exit 1
  [ -n "$GFX_LOW" ] && { sleep 8; bash gfxset.sh $H low; }
fi
U="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
UG="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G"

if from hopkins; then
  if [ "$STAGE" = "launch" ] || [ "$STAGE" = "walk" ]; then
    sleep 6; tp $H host || { echo "STAGE host tp FAILED: $(python -I posnow.py $H)"; exit 1; }
    sleep 6
    past_wagon $H
    timeout 200 python -I walk.py $H -s 0.7 -n 8 -10476.5 -6142.8 -10475.5 -6139.6 -10472.5 -6138.5 2>&1 | grep "^at" | tail -1
  fi
  # the stance: x between -10472.0 and -10471.4 at z -6138.3 (the prompt is white there); the aim, then strafes east
  # the stance: from exactly (-10472.65, -6138.72), facing Hopkins, strafes to the left until x is east of -10472.0
  # (ends near (-10471.9, -6138.0): the prompt is white there; 0.3 m further north it is not)
  timeout 90 python -I walk.py $H -s 0.2 -n 6 -10473.3 -6138.6 -10472.65 -6138.72 2>&1 | grep "^at" | tail -1
  for n in 1 2 3 4 5; do
    timeout 40 python -I face.py $H -10468.0 -6145.0 >/dev/null 2>&1
    x=$(python -I posnow.py $H | cut -d' ' -f1)
    python -I -c "import sys; sys.exit(0 if float('$x') > -10472.0 else 1)" && break
    $U -Do hold -Keys A -HoldMs 230 >/dev/null 2>&1; sleep 0.8
  done
  # ... and south of z -6138.15: at -6137.90 the focus falls on a sack, and at -6138.02 (15-nov, 02:04) the prompt did
  # not come and R opened the map; at -6138.06 it had come an hour before and at -6138.49 it came. Short steps towards Hopkins
  for n in 1 2 3; do
    timeout 40 python -I face.py $H -10468.0 -6145.0 >/dev/null 2>&1
    z=$(python -I posnow.py $H | cut -d' ' -f3)
    python -I -c "import sys; sys.exit(0 if float('$z') < -6138.15 else 1)" && break
    $U -Do hold -Keys W -HoldMs 200 >/dev/null 2>&1; sleep 0.8
  done
  timeout 40 python -I face.py $H -10468.0 -6145.0 >/dev/null 2>&1
  $U -Do look -Dx 10 -Dy 20 -Steps 3 >/dev/null 2>&1; sleep 0.8
  echo "STAGE host at Hopkins at $(date +%T): $(python -I posnow.py $H | cut -c1-40)"
  $U -Do shot -Out "$SP\\shots\\ss_hop_a.png" >/dev/null 2>&1
  $U -Do hold -Keys R -HoldMs 2000 >/dev/null 2>&1; sleep 3
  $U -Do shot -Out "$SP\\shots\\ss_hop_b.png" >/dev/null 2>&1
  $U -Do key -Keys SPACE -HoldMs 120 >/dev/null 2>&1; sleep 4
  $U -Do key -Keys SPACE -HoldMs 120 >/dev/null 2>&1; sleep 6
  $U -Do key -Keys ESC -HoldMs 120 >/dev/null 2>&1; sleep 4
  PARTS=$(timeout 60 python -I shipparts.py $H 2>/dev/null | wc -l)
  echo "STAGE ship: $PARTS actors hang from 0xB010000A on the host at $(date +%T)"
  [ "$PARTS" -lt 5 ] && { echo "STAGE ship FAILED (see shots/ss_hop_*.png)"; exit 1; }
fi

if from join; then
  pwsh -NoProfile -ExecutionPolicy Bypass -File "$SP\run_join.ps1" -Attach -AttachHost $H -AttachGuest $G > joinV.txt 2>&1
  J=$(grep -E 'guest idle|GONE' joinV.txt | sed 's/\x1b\[[0-9;]*m//g' | tail -1)
  echo "STAGE guest join: $J at $(date +%T)"
  # "guest idle: False" can be a slow load that did succeed: the test is whether the guest has a position.
  case "$J" in *True*) ;; *) ok=0; for i in $(seq 1 36); do python -I posnow.py $G >/dev/null 2>&1 && { ok=1; break; }; sleep 5; done
    [ $ok = 1 ] || { echo "STAGE guest join FAILED"; exit 1; }; echo "STAGE guest join: in the world after all at $(date +%T)";; esac
  sleep 4; tp $G guest || { echo "STAGE guest tp FAILED: $(python -I posnow.py $G)"; exit 1; }
  sleep 6
  past_wagon $G
  timeout 200 python -I walk.py $G -s 1.2 -n 10 -10480 -6150 2>&1 | grep "^at" | tail -1
  timeout 280 python -I walk.py $G -s 1.5 -n 8 -10463 -6148 -10457 -6160 -10457.5 -6176 2>&1 | grep "^at" | tail -1
  echo "STAGE guest on the quay at $(date +%T): $(python -I posnow.py $G | cut -c1-40)"
fi

if from guestboard; then
  for try in 1 2; do
    board $G
    echo "STAGE guest ladder at $(date +%T): $(where $G): $(python -I posnow.py $G)"
    [ "$(where $G)" = deck ] && break
  done
  [ "$(where $G)" = deck ] || { echo "STAGE guest boarding FAILED"; exit 1; }
  timeout 220 python -I shipwalk.py $G -s 0.4 -n 9 -2.9 -1.2 -2.85 0.6 -2.8 3.6 -1.2 4.6 2>&1 | grep "^at" | tail -1
  echo "STAGE guest on deck at $(date +%T): $(where $G): $(python -I posnow.py $G)"
  [ "$(where $G)" = deck ] || { echo "STAGE guest on deck FAILED"; exit 1; }
fi

if from hostboard; then
  timeout 280 python -I walk.py $H -s 1.5 -n 8 -10463 -6141.5 -10457 -6160 -10457.5 -6176 2>&1 | grep "^at" | tail -1
  for try in 1 2; do
    board $H
    echo "STAGE host ladder at $(date +%T): $(where $H): $(python -I posnow.py $H)"
    [ "$(where $H)" = deck ] && break
  done
  [ "$(where $H)" = deck ] || { echo "STAGE host boarding FAILED"; exit 1; }
  timeout 280 python -I shipwalk.py $H -s 0.35 -n 9 -2.9 -1.2 -2.85 0.6 -2.78 2.27 2>&1 | grep "^at" | tail -1
  # The wheel: E works from two metres away (the prompt "Ship's Wheel / Operate"), also with the other player's
  # character standing in the way; a second and a third walk if the first E did not take it.
  for try in 1 2 3; do
    timeout 60 python -I shipwalk.py $H -s 0.6 -n 4 0.0 1.85 2>&1 | tail -1
    sleep 0.5; $U -Do shot -Out "$SP\\shots\\ss_helm_a.png" >/dev/null 2>&1
    $U -Do key -Keys E -HoldMs 150 >/dev/null 2>&1; sleep 4
    [ "$(where $H)" = wheel ] && break
  done
  echo "STAGE host helm at $(date +%T): $(where $H): $(python -I posnow.py $H)"
  [ "$(where $H)" = wheel ] || { echo "STAGE host helm FAILED"; exit 1; }
fi
echo "DONE pids $H $G"
