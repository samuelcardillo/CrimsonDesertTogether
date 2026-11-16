#!/bin/bash
# seapair.sh H G : the same sea on both screens, seen (16-nov, sea_sync). Two copies in the world with the guest
# joined and no ship out (mountsession.sh gets there in twelve minutes; the session starts at 11:35 game time and it
# is dark by 19:30, forty real minutes later). Both characters walk to the end of the Vellua quay, a metre and a half
# apart, turn to the harbour mouth (where the swell is high) with the same tilt, and six pairs of screenshots are
# taken, each copy's within about a second of the other's: shots/seapair_h_N.png and shots/seapair_g_N.png. Then
# shots/seapair_N.jpg puts each pair one above the other (host above). With sea_sync the long waves are in the same
# places in both halves of a pair; without it they are not. The twelve wave trains of both copies are printed too.
cd "$(dirname "$0")"; mkdir -p shots
B="D:/SteamLibrary/steamapps/common/Crimson Desert/bin64"
H=$1; G=$2
SP="$(cygpath -w "$(pwd)")"
route() {  # pid x z : from the Vellua nexus to the quay's end by the way shipsession.sh takes, then to (x, z)
  timeout 280 python -I goto3576.py $1 -10516 -6140 3 40 2>&1 | tail -1
  timeout 240 python -I walk.py $1 -s 1.0 -n 14 -10508 -6137 -10495 -6137 -10485 -6140 2>&1 | grep "^at" | tail -1
  timeout 200 python -I walk.py $1 -s 1.2 -n 10 -10480 -6150 2>&1 | grep "^at" | tail -1
  timeout 280 python -I walk.py $1 -s 0.8 -n 10 -10463 -6148 -10457 -6160 $2 $3 2>&1 | grep "^at" | tail -1
}
echo "START at $(date +%T): asi $(certutil -hashfile "$B/cdmp.asi" SHA256 | sed -n 2p | cut -c1-8)"
route $H -10457.5 -6176.0
route $G -10457.0 -6173.0
for P in $H $G; do
  timeout 60 python -I face.py $P -10150 -6345 >/dev/null 2>&1
  timeout 60 python -I pitch.py $P -4 2>&1 | tail -1
done
echo "host $(python -I posnow.py $H | cut -d' ' -f1-3) | guest $(python -I posnow.py $G | cut -d' ' -f1-3)"
python -I seawaves.py $H $G | cut -c1-210
rm -f shots/seapair_*.png shots/seapair_*.jpg
for i in 1 2 3 4 5 6; do
  pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $H -Do shot -Out "$SP\\shots\\seapair_h_$i.png" >/dev/null 2>&1
  pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $G -Do shot -Out "$SP\\shots\\seapair_g_$i.png" >/dev/null 2>&1
  sleep 2
done
python -I -c "
from PIL import Image, ImageDraw
import os
for i in range(1, 7):
    a, b = f'shots/seapair_h_{i}.png', f'shots/seapair_g_{i}.png'
    if not (os.path.exists(a) and os.path.exists(b)):
        continue
    A, Bm = Image.open(a).convert('RGB'), Image.open(b).convert('RGB')
    w = min(A.width, Bm.width)
    out = Image.new('RGB', (w, A.height + Bm.height + 4), (255, 255, 255))
    out.paste(A.crop((0, 0, w, A.height)), (0, 0))
    out.paste(Bm.crop((0, 0, w, Bm.height)), (0, A.height + 4))
    d = ImageDraw.Draw(out)
    d.text((w - 150, 8), 'HOST', fill=(255, 255, 0))
    d.text((w - 150, A.height + 12), 'GUEST', fill=(255, 255, 0))
    out.save(f'shots/seapair_{i}.jpg', quality=88)
print('pairs written')
"
echo "DONE at $(date +%T): $(ls shots/seapair_*.jpg 2>/dev/null | wc -l) pairs; ERR host $(grep -a -c '] ERR ' "$B/cdmp.host.log") guest $(grep -a -c '] ERR ' "$B/cdmp.guest.log")"
