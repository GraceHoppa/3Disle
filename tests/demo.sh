#!/bin/sh
# Records the app being used, start to finish: builds a small device from nothing, tries it, animates
# it, renders the video, exports a page; then imports a real model and takes it apart. Every step goes
# through the real input path, so this doubles as a check.
# Writes build/demo/: app.mp4 (what the screen showed), and everything made in the app along the way.
set -eu
BIN=${BIN:-build/3disle}
OUT=${OUT:-build/demo}
rm -rf "$OUT"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cp public/projects/motor/motor.glb "$OUT/"

"$BIN" script --app - \
  "record:$OUT/app.mp4" wait:0.8 \
  "press:New device" "field:Name=Demo board" wait:0.3 \
  "field:Footprint width=80" "field:Footprint depth=50" wait:0.4 \
  press:Board "pick:Surface=pcb" wait:0.6 \
  "field:Thickness=1.6" wait:0.3 \
  "press:+ PART" "field:Name=Processor" "field:Size x=16" "field:Size y=2" "field:Size z=16" \
  "pick:Material=chip" wait:0.3 \
  "dragby:view@0.5,0.47|40|-12" wait:0.5 \
  "pick:add-shape=Cylinder" "press:+ PART" "field:Name=Capacitor" \
  "field:Radius=2.5" "field:Height=6" "field:At x=14" "field:At z=-16" \
  "press:ONE ONLY: MAKE COPIES" "field:Count=3" "field:Step x=7" wait:0.5 \
  "pick:add-shape=Fan" "press:+ PART" "field:Name=Fan" "field:Radius=11" "field:At x=-24" "field:At z=4" wait:0.5 \
  "press:+ CONTROL" "field:Label=Speed" "field:Id=speed" wait:0.3 \
  "press:Fan" "pick:Spin=Follows a control" wait:0.2 "field:Spin map=0:0, 100:6000" wait:0.3 \
  "press:+ WIRE" "pick:Carries=power" wait:0.5 \
  "dragby:try@0.1,0.5|150|0" wait:1.2 \
  "dragby:apart@0.1,0.5|120|0" wait:0.8 "dragby:apart@0.6,0.5|-120|0" wait:0.3 \
  "dragby:view@0.5,0.2|150|40" wait:0.8 \
  ctrl:z wait:0.3 ctrl:y wait:0.3 \
  press:SAVE wait:0.3 "field:file-field=$OUT/board.json" wait:0.5 \
  press:VIEW wait:1.5 \
  press:ANIMATE wait:0.5 "press:EMPTY TIMELINE" wait:0.4 \
  "field:Video length=6" wait:0.2 press:CAPTION "field:Title=Demo board" "field:Scale=8 cm" wait:0.3 \
  "dragby:picture@0.5,0.4|-120|30" "press:SET CAMERA HERE" wait:0.3 \
  "dragby:ruler@0.1,0.5|300|0" wait:0.2 \
  "dragby:picture@0.5,0.4|220|-60" wheel:3 wait:0.2 "press:SET CAMERA HERE" wait:0.3 \
  press:APART "dragby:ruler@0.5,0.5|250|0" press:APART "field:Apart=0.8" wait:0.3 \
  press:START wait:0.2 press:PLAY wait:3.2 \
  ctrl:s wait:0.3 \
  "press:RENDER MP4" wait:0.3 press:720p press:30##fps press:Draft wait:0.4 \
  "press:START##render" expect:rendering=1 wait:3.2 expect:renderdone=1 wait:1 \
  press:CLOSE##render wait:0.4 \
  press:BUILD wait:0.6 \
  press:EXPORT wait:1.5 expect:dirty=0 \
  press:OPEN wait:0.4 "field:file-field=$OUT/motor.glb" wait:1 \
  "dragby:apart@0.05,0.5|130|0" wait:1 "press:group2" wait:0.6 "press:SPLIT##objects" wait:0.8 \
  "dragby:view@0.5,0.15|-200|30" wait:1 \
  ctrl:s wait:0.3 press:EXPORT wait:1.5 press:VIEW wait:2 press:BUILD wait:0.4 \
  record:

echo "$OUT/app.mp4"
ls -la "$OUT"
