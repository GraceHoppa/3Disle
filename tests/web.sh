#!/bin/sh
# Exports pages and opens each in headless Chromium: the engine has to start and draw frames, with no
# error from the page or the graphics context. Set CHROMIUM to pick the browser.
set -u
BIN=${BIN:-build/3disle}
CHROMIUM=${CHROMIUM:-$(command -v chromium || command -v chromium-browser || command -v google-chrome)}
[ -n "$CHROMIUM" ] || { echo "skipped: no Chromium"; exit 0; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
for f in public/models/drone/tour.json public/models/rtx5070ti/tour.json public/models/drone/motor.json public/projects/motor/project.json; do
  n=$(echo "$f" | tr '/.' '__')
  if ! "$BIN" export "$f" -o "$T/$n.html" >/dev/null; then
    echo "FAIL export $f"
    fails=$((fails + 1))
    continue
  fi
  dom=$(timeout 180 "$CHROMIUM" --headless=new --no-sandbox --disable-gpu-sandbox --use-angle=swiftshader --enable-unsafe-swiftshader --ignore-gpu-blocklist \
    --user-data-dir="$T/profile" --window-size=1200,800 --virtual-time-budget=30000 --dump-dom "file://$T/$n.html" 2>/dev/null)
  if echo "$dom" | grep -q 'data-error'; then
    echo "FAIL $f: $(echo "$dom" | grep -o 'data-error="[^"]*"')"
    fails=$((fails + 1))
  elif echo "$dom" | grep -q 'data-frames'; then
    echo "ok   $f ($(du -k "$T/$n.html" | cut -f1) KB, $(echo "$dom" | grep -o 'data-frames="[0-9]*"'))"
  else
    echo "FAIL $f: the page drew no frames"
    fails=$((fails + 1))
  fi
done
[ "$fails" = 0 ] && echo "all pages run"
exit "$fails"
