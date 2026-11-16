#!/bin/bash
# gfxfile.sh low|normal|keep : the GUEST test profile's video settings, by file, with every game copy closed (no
# window is touched). The game keeps them in %LOCALAPPDATA%\Pearl Abyss\CD\save_cdmp_guest\user_engine_option_save.xml
# (plain XML; 16-nov-2026, game 1.0.0.3663). What a two-copies-on-one-PC test needs changed is three things:
#   _upscaleResolution _select "AA" -> "33%"          (Upscale Resolution DLAA -> Ultra Performance)
#   _frameGenerationModeSelect, absent                (Frame Generation -> Off)
#   _enableRayReconstruction, absent                  (NVIDIA DLSS Ray Reconstruction -> Off)
# The files are the game's OWN, written by it on 16-nov through its menu, kept in backups/video-settings/ (never
# pushed: they name the owner's monitor): guest-low.xml, guest-normal.xml (and host-low.xml, for reference).
#   low    : the guest's file := guest-low.xml (what it has now is kept as guest-normal.xml first, unless it is
#            already the cheap one). Do it BEFORE the launch.
#   normal : the guest's file := guest-normal.xml. Do it AFTER the test, with the games closed.
#   keep   : nothing written; says which of the two the guest has now.
# The HOST's settings cannot be set this way: with save_guard=redirect its copy reads them from its sandbox, which the
# mod rebuilds from the real save\ at every start (and the real save\ is never written). For the host: gfxset.sh
# <pid> low, through the menu, once it is in the world; nothing to put back afterwards.
# NOT run in a session yet when written (the guest starting with a file the game wrote in another run is expected to
# work; check the frames a second in cdmp.guest.log).
R="$(cd "$(dirname "$0")/../../.." && pwd)"
F="$LOCALAPPDATA/Pearl Abyss/CD/save_cdmp_guest/user_engine_option_save.xml"
V="$R/backups/video-settings"
cheap() { grep -q '_upscaleResolution" _select="33%"' "$1"; }
[ -f "$F" ] || { echo "gfxfile: no $F"; exit 1; }
case "$1" in
  keep) cheap "$F" && echo "gfxfile: the guest has the cheap settings" || echo "gfxfile: the guest has its normal settings"; exit 0;;
  low|normal) ;;
  *) echo "gfxfile.sh low|normal|keep"; exit 1;;
esac
tasklist //FI "IMAGENAME eq CrimsonDesert.exe" //NH | grep -qi crimson && { echo "gfxfile: a game copy is running: nothing done"; exit 1; }
[ -f "$V/guest-$1.xml" ] || { echo "gfxfile: no $V/guest-$1.xml"; exit 1; }
if [ "$1" = low ] && ! cheap "$F"; then cp -p "$F" "$V/guest-normal.xml"; fi
cp -p "$V/guest-$1.xml" "$F" && echo "gfxfile: the guest's video settings are now guest-$1.xml"
