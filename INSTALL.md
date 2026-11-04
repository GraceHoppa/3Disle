# Install and run

Linux with a C17 compiler, make, pkg-config, SDL3, EGL and OpenGL ES 3 headers. ffmpeg with libx264
renders video. On Arch: `sdl3 mesa ffmpeg` (or the NVIDIA driver's EGL).

```sh
git clone git@github.com:GraceHoppa/3Disle.git
cd 3Disle
make
build/3disle
```

## Commands

```sh
build/3disle [file.json]                          # viewer: a device or tour
build/3disle shot file.json out.png [--size WxH] [--explode A]
build/3disle auto device-or-tour.json out.shot.json
build/3disle render file.shot.json -o out.mp4 [--size WxH] [--fps N] [--crf N] [--ss K] [--from S] [--to S] [--no-grain]
build/3disle still file.shot.json out.png SECONDS [--size WxH]
build/3disle script file.json STEP...             # drive the viewer headless, for checks
```

`script` steps go through the real input path: `wait:S mouse:X,Y click:X,Y drag:X,Y,X2,Y2 wheel:N
key:NAME type:TEXT shot:FILE explode:A labels:0|1 select:PART step:1|-1 size:WxH print:WHAT`.

## Checks

```sh
make test   # engine: JSON, data round trips, layout against tests/golden, spread, picking, painting
make asan   # the same under address and undefined-behaviour sanitizers
```

## The TypeScript version

`legacy-ts/` holds the old three.js version until the C one covers it: `cd legacy-ts && npm ci &&
npm run dev`. `legacy-ts/demo/fpv-quad.html` is its one-file demo. Git tag: `ts-final`.
