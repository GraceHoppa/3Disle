# Install and run

Linux with a C17 compiler, make, pkg-config, SDL3, EGL and OpenGL ES 3 headers. ffmpeg with libx264
renders video. clang and wasm-ld build the web page's engine; without them everything else still builds and
`export` says what is missing. On Arch: `sdl3 mesa ffmpeg clang lld` (or the NVIDIA driver's EGL).

```sh
git clone git@github.com:GraceHoppa/3Disle.git
cd 3Disle
make
build/3disle
```

## Commands

```sh
build/3disle [file]                               # the app: a device, tour, video, .glb or .stl
build/3disle view [file.json]                     # just the viewer
build/3disle shot file.json out.png [--size WxH] [--explode A]
build/3disle auto device-or-tour.json out.shot.json
build/3disle render file.shot.json -o out.mp4 [--size WxH] [--fps N] [--crf N] [--ss K] [--from S] [--to S] [--no-grain]
build/3disle still file.shot.json out.png SECONDS [--size WxH]
build/3disle export device-or-tour.json -o page.html   # one file that opens from disk
build/3disle check [folder-or-file]               # every file opens, and everything it names exists
build/3disle script [--app] file.json STEP...     # drive the viewer or the app headless, for checks
```

`script` steps go through the real input path; the list is at the top of `app/script.c`. With `--app`, `-` as the file
starts a new device, and steps press things by name: `press:SAVE`, `field:Name=Fan`, `pick:Shape=Box`,
`dragby:view@0.5,0.5|120|0`, `expect:parts=3`, `record:out.mp4`.

## Keys in the app

Ctrl+S save, Ctrl+Z / Ctrl+Y undo and redo, Ctrl+1/2/3 tabs, Ctrl+O open, Ctrl+N new. Build: Delete, Ctrl+D duplicate, F frame
the selection, Esc clear. Animate: Space play, K key, Delete, left and right step a frame, Home and End.

## Checks

```sh
make test      # engine: JSON, data round trips, layout against tests/golden, spread, picking, painting, model files
make asan      # the same under address and undefined-behaviour sanitizers
make test-app  # the whole app, driven headless through real clicks (needs EGL; software is fine)
make test-web  # exported pages in headless Chromium
tests/demo.sh  # records a whole session to build/demo/app.mp4, and checks it on the way
```

## The TypeScript version

`legacy-ts/` holds the old three.js version until the C one covers it: `cd legacy-ts && npm ci &&
npm run dev`. `legacy-ts/demo/fpv-quad.html` is its one-file demo. Git tag: `ts-final`.
