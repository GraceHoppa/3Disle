#!/bin/sh
# The app, driven headless through its real input path: every step presses what a person would press,
# then checks what the app says it holds. Needs a GPU or software EGL. BIN picks the build to run.
set -u
BIN=${BIN:-build/3disle}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
run() { # name, steps...
  name=$1
  shift
  if out=$("$BIN" script --app "$@" 2>&1); then
    echo "ok   $name"
  else
    echo "FAIL $name"
    echo "$out" | grep -v '^ok ' | tail -8
    echo "(exit code $(
      "$BIN" script --app "$@" >/dev/null 2>&1
      echo $?
    ))"
    fails=$((fails + 1))
  fi
}

# ---- Build
run "a new device: add, name, undo, save, reopen" - \
  expect:docs=1 expect:layers=1 expect:parts=0 expect:dirty=1 \
  press:Board expect:sel=layer \
  "press:+ PART" expect:parts=1 expect:sel=part \
  "field:Name=Main chip" "expect:selname=Main chip" \
  "field:Size x=20" \
  "press:+ PART" expect:parts=2 \
  "field:Name=Second chip" \
  "press:+ WIRE" expect:wires=1 expect:sel=wire expect:missing=0 \
  key:escape \
  "press:Main chip" expect:sel=part \
  "press:DUP" expect:parts=3 \
  "press:DEL" expect:parts=2 \
  ctrl:z expect:parts=3 \
  ctrl:z expect:parts=2 \
  ctrl:z expect:wires=0 \
  ctrl:y expect:wires=1 \
  "press:SAVE" expect:modal=2 \
  "field:file-field=$T/made.json" expect:modal=0 expect:dirty=0
run "and it comes back the same" "$T/made.json" expect:parts=2 expect:wires=1 expect:dirty=0 "press:Main chip" expect:sel=part

run "rename follows wires; a taken name is refused" public/models/drone/quad.json \
  "press:ESC board" "field:Name=Motors" "expect:selname=ESC board" \
  "field:Name=Power board" "expect:selname=Power board" expect:missing=0 \
  ctrl:z "expect:selname=ESC board" expect:missing=0

cat >"$T/plate.json" <<JSON
{ "name": "Plate", "footprint": [100, 60], "layers": [ { "name": "Base", "thickness": 2, "parts": [
  { "name": "Block", "shape": "box", "size": [40, 20, 40], "at": [0, 0] } ] } ] }
JSON
cp "$T/plate.json" "$T/plate0.json"
run "drag a part in the view, one undo step, saved to disk" "$T/plate.json" \
  "press:Block" expect:at=0,0 expect:undo=0 \
  "dragby:view@0.5,0.5|120|0" expect:undo=1 expect:dirty=1 \
  ctrl:z expect:at=0,0 expect:dirty=0 \
  ctrl:y expect:undo=1 expect:dirty=1 \
  ctrl:s expect:dirty=0
if cmp -s "$T/plate0.json" "$T/plate.json"; then
  echo "FAIL the saved file did not change"
  fails=$((fails + 1))
fi

run "every shape can be added and built" - \
  "pick:add-shape=Cylinder" "press:+ PART" "pick:add-shape=Sphere" "press:+ PART" \
  "pick:add-shape=Fan" "press:+ PART" "pick:add-shape=Port" "press:+ PART" \
  "pick:add-shape=Outline" "press:+ PART" "pick:add-shape=Box" "press:+ PART" \
  expect:parts=6 wait:0.1 "pick:Shape=Fan" wait:0.1 "pick:Shape=Box" wait:0.1

run "controls, rules, readouts" - \
  "press:+ PART" "press:+ CONTROL" expect:controls=1 expect:sel=control \
  "press:+ READOUT" expect:sel=readout \
  "field:Map=0:0, 100:42" \
  "press:Part 1" "pick:Spin=Follows a control" wait:0.1 "field:Spin map=0:0, 100:29000" wait:0.1

run "layers: add, move, delete; opening a linked file" public/models/drone/quad.json \
  expect:docs=1 "press:+ LAYER" expect:layers=11 \
  "press:UP##move" "press:DOWN##move" "press:DEL" expect:layers=10 expect:sel=device \
  "press:ESC board" "press:OPEN THAT FILE" expect:docs=2

run "a tour: levels open as documents" public/models/drone/tour.json \
  expect:sel=tour expect:docs=3 "press:2  SpeedyBee F405 V5 flight controller" expect:sel=level

# ---- Model files
cp public/projects/motor/motor.glb "$T/motor.glb"
run "import a model: parts cut from it, join, split, save" "$T/motor.glb" \
  expect:sel=device expect:dirty=1 wait:0.2 \
  "dragby:apart@0.1,0.5|100|0" wait:0.1 \
  ctrl:s expect:dirty=0 \
  press:VIEW wait:0.2 press:BUILD
if [ ! -s "$T/motor.json" ]; then
  echo "FAIL the import was not saved beside the model"
  fails=$((fails + 1))
fi
run "and it opens again" "$T/motor.json" expect:dirty=0 expect:missing=0 wait:0.1 press:EXPORT
if [ ! -s "$T/motor.html" ]; then
  echo "FAIL no page from the imported model"
  fails=$((fails + 1))
fi
run "a project opens as a device" public/projects/motor/project.json "press:Bell" expect:sel=part "expect:selname=Bell" wait:0.1

# ---- The checker
if ! "$BIN" check public >/dev/null; then
  echo "FAIL the data checker found problems in public/"
  "$BIN" check public | tail -5
  fails=$((fails + 1))
else
  echo "ok   the data checker passes the real data"
fi
cat >"$T/broken.json" <<JSON
{ "name": "Broken", "footprint": [10, 10], "layers": [ { "name": "L", "thickness": 1, "detail": "gone.json", "parts": [
  { "name": "A", "shape": "box", "size": [1, 1, 1], "spin": { "by": "nope", "map": [[0, 0]] } } ],
  "links": [ { "from": "A", "to": "ghost" } ] } ], "wires": [ { "name": "w", "from": "A", "to": "ghost2" } ] }
JSON
n=$("$BIN" check "$T/broken.json" | tail -1)
if [ "$n" = "1 files, 4 problems" ]; then
  echo "ok   the data checker names what is broken"
else
  echo "FAIL the checker said: $n"
  "$BIN" check "$T/broken.json"
  fails=$((fails + 1))
fi

# ---- View
run "the View tab shows unsaved work" - \
  "press:+ PART" ctrl:3 expect:tab=view wait:0.2 press:ANIMATE expect:tab=animate press:BUILD expect:tab=build

run "export a page from the app" "$T/plate.json" press:EXPORT expect:dirty=0
if [ ! -s "$T/plate.html" ]; then
  echo "FAIL no page"
  fails=$((fails + 1))
fi

# ---- Animate
cp -r public/models/drone "$T/drone"
run "auto video: scrub, keys, undo, save, reopen" "$T/drone/motor.json" \
  ctrl:2 expect:shot=0 "press:AUTO VIDEO" expect:shot=1 expect:shotdirty=1 \
  wait:0.1 "dragby:ruler@0.5,0.5|40|0" "press:picture" \
  "dragby:picture@0.5,0.5|80|30" expect:detached=1 \
  "press:SET CAMERA HERE" expect:detached=0 \
  key:space expect:playing=1 wait:0.2 key:space expect:playing=0 \
  ctrl:z ctrl:y \
  ctrl:s expect:shotdirty=0

run "an empty timeline by hand" "$T/drone/esc.json" \
  ctrl:2 "press:EMPTY TIMELINE" expect:shot=1 expect:tracks=0 \
  key:k expect:tracks=1 "expect:keys:0=1" \
  "dragby:ruler@0.5,0.5|60|0" key:k "expect:keys:0=2" \
  "dragby:key:0:1|50|0" \
  press:FADE expect:tracks=2 press:APART expect:tracks=3 \
  press:CAPTION expect:captions=1 \
  "dragby:caption:0|30|0" \
  key:delete expect:captions=0 ctrl:z expect:captions=1

run "render from the app, then cancel one" "$T/drone/esc.json" \
  ctrl:2 "press:EMPTY TIMELINE" key:k "press:Video length" \
  "field:Video length=1" \
  "press:RENDER MP4" expect:modal=5 press:720p "press:Draft" "press:START##render" expect:rendering=1 \
  wait:0.2 press:CANCEL##render expect:rendering=0 \
  "press:START##render" expect:rendering=1 wait:1.5 expect:rendering=0 expect:renderdone=1 \
  press:CLOSE##render expect:modal=0
if [ ! -s "$T/drone/esc.mp4" ]; then
  echo "FAIL no video file"
  fails=$((fails + 1))
fi

[ "$fails" = 0 ] && echo "all app checks passed"
exit "$fails"
