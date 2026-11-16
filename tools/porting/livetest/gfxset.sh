#!/bin/bash
# gfxset.sh pid low|high : one copy's video settings for a two-copies-on-one-PC test, through the game's own menu
# (Esc -> Others -> Settings -> Video), for a copy that is in the world with a 3840x2160 window (16-nov-2026, game
# 1.0.0.3663; the clicks are the ones done by hand that day).
#   low  : Upscale Resolution DLAA -> Ultra Performance (right arrow x4), Frame Generation "NVIDIA DLSS Frame
#          Generation" -> Off (left arrow x2), NVIDIA DLSS Ray Reconstruction On -> Off, Apply. The GPU goes from
#          about 36 ms a frame to 7 ms. With [general] fps_cap=30 in both copies (FPS_CAP=30 bash shipsession.sh) both
#          then hold 30 frames a second whichever is in front (docs/research/dos-instancias.md, 2.6).
#   high : the way back: Upscale Resolution Ultra Performance -> DLAA (left x4), Frame Generation Off -> NVIDIA DLSS
#          Frame Generation (right x2), Number of Frames Generated -> 4 (right x2), Ray Reconstruction -> On, Apply.
#          NOT run yet when this was written: look at the screenshot it leaves.
# It assumes the values named above as the starting point (the host's are those at every start: its settings live in
# the save sandbox, rebuilt from the real save\ at each start). It is for the HOST. The guest's persist in
# save_cdmp_guest: set them by file instead, with the games closed (gfxfile.sh low before, gfxfile.sh normal after);
# clicking `low` on a guest that already has the cheap settings would move them somewhere else.
# A screenshot of the Video page before leaving it goes to shots/gfx_<pid>_<mode>.png: LOOK AT IT.
cd "$(dirname "$0")"; mkdir -p shots
P=$1; MODE=${2:-low}
SP="$(cygpath -w "$(pwd)")"
U="pwsh -NoProfile -ExecutionPolicy Bypass -File ui.ps1 -GamePid $P"
w() { python -I -c "import time; time.sleep($1)"; }
click() { for i in $(seq 1 $3); do $U -Do click -X $1 -Y $2 >/dev/null 2>&1; w 0.8; done; }
R=2115; L=1483           # the right and the left arrow of a row
UPSCALE=796; FRAMEGEN=910; FRAMES=1023; RAYREC=1250
$U -Do key -Keys ESC -HoldMs 120 >/dev/null 2>&1; w 3
click 1609 101 1; w 1.5     # the Others tab
click 890 956 1; w 2        # Settings (opens on Video)
case "$MODE" in
  low)  click $R $UPSCALE 4; click $L $FRAMEGEN 2; click $L $RAYREC 1;;
  high) click $L $UPSCALE 4; click $R $FRAMEGEN 2; click $R $FRAMES 2; click $R $RAYREC 1;;
  *) echo "gfxset.sh pid low|high"; exit 1;;
esac
$U -Do key -Keys SPACE -HoldMs 120 >/dev/null 2>&1; w 3     # Apply
$U -Do key -Keys SPACE -HoldMs 120 >/dev/null 2>&1; w 8     # "Would you like to apply the settings?"
$U -Do shot -Out "$SP\\shots\\gfx_${P}_$MODE.png" >/dev/null 2>&1
$U -Do key -Keys ESC -HoldMs 120 >/dev/null 2>&1; w 1.5
$U -Do key -Keys ESC -HoldMs 120 >/dev/null 2>&1; w 1.5
echo "gfxset $MODE on $P: done; check shots/gfx_${P}_$MODE.png"
