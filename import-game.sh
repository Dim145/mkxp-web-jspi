#!/bin/bash
# import-game.sh — bring your own RPG Maker XP (RGSS1) game into the web build.
#
# The engine WASM (mkxp.wasm) is game-agnostic and does NOT need rebuilding for a new
# game — this script just processes your game's files into build/gameasync/ and
# generates the asset maps the loader needs. Run it INSIDE the build container, with
# the repo mounted at /src/mkxp-web and YOUR game mounted read-only at /game:
#
#   docker run --rm -it --platform linux/amd64 \
#     -v "$PWD/mkxp-web:/src/mkxp-web" \
#     -v "$PWD/import-game.sh:/import-game.sh" \
#     -v "/path/to/YourRMXPGame:/game:ro" \
#     mkxp-web-jspi /import-game.sh <namespace> ["Window Title"]
#
# <namespace> is the IndexedDB save-key prefix (keep it unique per game).
#
# AFTER this runs, a stock/lightly-scripted RGSS1 game usually boots as-is. Heavily
# scripted games (e.g. Pokémon Essentials) need their Ruby ported to mruby first —
# see docs/PORTING-mruby.md. Scripts.rxdata is extracted to scripts_src/ for that.
set -euo pipefail

NAMESPACE="${1:-mygame}"
WTITLE="${2:-RGSS-Web}"
REPO="${REPO:-/src/mkxp-web}"
GAME="${GAME:-/game}"
GA="$REPO/build/gameasync"

[ -d "$GAME" ] || { echo "ERROR: mount your game at $GAME (read-only)"; exit 1; }
[ -d "$REPO" ] || { echo "ERROR: repo not found at $REPO"; exit 1; }
command -v ruby >/dev/null || { echo "ERROR: ruby required (run inside the build container)"; exit 1; }

echo ">>> [1/8] Resetting build/gameasync/ and copying game files"
rm -rf "$GA" "$REPO/build/preload"
mkdir -p "$GA"
# Copy Data/Graphics/Audio + Game.ini; strip Windows/editor cruft the browser can't use.
rsync -a --exclude='*.exe' --exclude='*.dll' --exclude='*.swf' --exclude='*.lnk' \
        --exclude='*.ini.bak' --exclude='Thumbs.db' --exclude='.DS_Store' \
        "$GAME"/ "$GA"/
[ -f "$GA/Game.ini" ] || echo "WARN: no Game.ini found — RMXP games need one (Scripts=Data/Scripts.rxdata)"

echo ">>> [2/8] Converting audio to OGG (browsers can't play MIDI/WMA; MP3/WAV work but OGG is best)"
if command -v ffmpeg >/dev/null; then
  ( cd "$GA" && bash "$REPO/extra/convert_audio.sh" ) || echo "WARN: audio conversion had errors (continuing)"
else
  echo "WARN: ffmpeg/timidity not installed — skipping audio conversion. MIDI (.mid) and WMA will be SILENT."
fi

echo ">>> [3/8] Extracting Scripts.rxdata -> scripts_src/ (edit these to port to mruby)"
if [ -f "$GA/Data/Scripts.rxdata" ]; then
  ruby "$REPO/../scripts_tool.rb" extract "$GA/Data/Scripts.rxdata" "$REPO/../scripts_src" 2>/dev/null \
    || ruby /src/scripts_tool.rb extract "$GA/Data/Scripts.rxdata" /src/scripts_src \
    || echo "WARN: could not extract Scripts.rxdata (adjust the path to scripts_tool.rb)"
else
  echo "WARN: no Data/Scripts.rxdata — cannot extract scripts"
fi

echo ">>> [4/8] Installing the generic mruby/RGSS compat shim (rgss.rb)"
cp -f "$REPO/extra/rgss.rb" "$GA/rgss.rb"

echo ">>> [5/8] Generating mapping.js (WITH directory markers — required by drive.js)"
bash "$REPO/../regen-mapping.sh" 2>/dev/null || bash /src/regen-mapping.sh

echo ">>> [6/8] Generating bitmap-map.js (image dimensions -> non-blocking image loads)"
ruby "$REPO/../gen-bitmap-map.rb" 2>/dev/null || ruby /src/gen-bitmap-map.rb || echo "WARN: bitmap-map generation skipped"

echo ">>> [7/8] Generating per-map preload lists (optional prefetch, reduces first-touch stalls)"
ruby "$REPO/../gen-preload.rb" "$GA" "$REPO/build/preload" 2>/dev/null \
  || ruby /src/gen-preload.rb "$GA" "$REPO/build/preload" || echo "WARN: preload generation skipped"

echo ">>> [8/8] Setting namespace + window title in index.html"
perl -i -pe "s/var namespace = '[^']*';/var namespace = '$NAMESPACE';/" "$REPO/build/index.html"
perl -i -pe "s/var wTitle = '[^']*';/var wTitle = '$WTITLE';/"          "$REPO/build/index.html"

cat <<DONE

>>> Import done. build/gameasync/ is populated; mkxp.wasm is unchanged.
Next:
  * Stock/lightly-scripted game: serve build/ (see docs/DEPLOY.md) and test in a browser.
  * Script-heavy game (Essentials-class): port scripts_src/*.rb to mruby (docs/PORTING-mruby.md),
    then repack:  ruby scripts_tool.rb pack scripts_src build/gameasync/Data/Scripts.rxdata
    and bump the Scripts.rxdata ?h= hash in build/gameasync/mapping.js (or re-run regen-mapping.sh).
  * You only rebuild the engine (rebuild-*.sh) if you change the C++/mruby, not per game.
DONE
