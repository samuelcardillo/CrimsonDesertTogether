#!/bin/bash
# slidetest.sh H G [seconds] : how fast a passenger slides along the deck in the copy whose window is behind (host at
# the wheel, guest on deck: shipsession.sh done). Second gear with the host's copy in front, then boat/deckdrift.py
# for `seconds` (90): the ship's speed and the guest's place on the deck every three seconds, and at the end the
# distance it slid, the time, and the slide as a share of the distance the ship covered. Stops when the guest is off
# the ship. Made on 15-nov to compare builds (ASI=<a kit's cdmp.asi> bash shipsession.sh starts a session with a
# released mod file).
cd "$(dirname "$0")"; mkdir -p shots
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
T="D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting/boat"
H=$1; G=$2; SECS=${3:-90}
UH="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H"
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8) ($(head -1 "$B/cdmp.host.log" | sed -E 's/.*\((build [^,]*),.*/\1/')); guest at $(python -I posnow.py $G | cut -c1-24)"
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
if python -I sailstate.py $H "$(python -I sailstate.py $H key)" | grep -q furled; then
  $UH -Do key -Keys R -HoldMs 150 >/dev/null 2>&1; sleep 3
  $UH -Do key -Keys E -HoldMs 150 >/dev/null 2>&1; sleep 4
  $UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 8
fi
$UH -Do hold -Keys LSHIFT -HoldMs 1500 >/dev/null 2>&1; sleep 6
(timeout $((SECS + 20)) python -I "$T/slidetrace.py" $G $SECS > shots/slidetrace_last.txt 2>&1 &)
timeout $((SECS + 40)) python -I "$T/deckdrift.py" $H $G $SECS 3 2>&1 | tee shots/slidetest_last.txt | python -I -c "
import re, sys
rows = []
for l in sys.stdin:
    m = re.match(r'\s*([\d.]+) ship\s+([\d.]+) m/s .*guest on (0x[0-9a-f]+) local \(([-\d.]+), ([-\d.]+), ([-\d.]+)\) moved ([\d.]+) m', l)
    if not m: continue
    rows.append((float(m.group(1)), float(m.group(2)), m.group(3), float(m.group(6)), float(m.group(7))))
    print(f'    {m.group(1):>5} s  ship {float(m.group(2)):4.1f} m/s  guest z {float(m.group(6)):6.2f}  moved {float(m.group(7)):5.2f} m' + ('  OFF THE SHIP' if m.group(3) == '0x0' else ''))
    if m.group(3) == '0x0': break
if rows:
    on = [r for r in rows if r[2] != '0x0'] or rows
    t = on[-1][0]
    dist = sum(r[1] for r in on) * 3.0
    slid = on[-1][4]
    print(f'  in {t:.0f} s the ship covered about {dist:.0f} m (mean {dist / max(t, 1):.1f} m/s, fastest {max(r[1] for r in on):.1f}); the guest slid {slid:.2f} m: {100 * slid / max(dist, 1):.2f}% of that' + ('; it left the ship' if rows[-1][2] == '0x0' else ''))
else:
    print('  no samples')"
sleep 3
echo "  frame by frame on the guest (boat/slidetrace.py; the whole trace is in shots/slidetrace_last.txt):"
head -8 shots/slidetrace_last.txt | cut -c1-170 | sed 's/^/    /'
echo "    ..."
tail -1 shots/slidetrace_last.txt | cut -c1-230 | sed 's/^/    /'
$UH -Do hold -Keys ALT -HoldMs 2500 >/dev/null 2>&1; sleep 4
echo "DONE at $(date +%T): ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log")"
