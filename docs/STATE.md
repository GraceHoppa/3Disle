# State

Where the project stands. Update at the end of every session.

## Works (C, verified 2026-11-07)

- **The app** (`build/3disle`): tabs Build, Animate, View in one window. `tests/app.sh` (`make test-app`) drives it
  headless through the real input path; also clean under the sanitizers. `tests/demo.sh` records a whole session.
- **Build:** tree, every field of `spec.h`, rules as a control plus `0:0, 100:29000`, add, duplicate, delete, reorder,
  rename that follows wires and tracks, drag a selected part in 3D with snapping, pick wire ends by clicking, try the
  controls, undo as whole documents, tours as lists of levels, several open documents, save, save as with links kept.
- **Animate:** timeline with scene strip, captions, labels and key rows; scrub, play, drag keys and blocks; camera keys
  from a free orbit; add tracks at the playhead; auto video; unsaved documents play as they are.
- **Render from the app:** size, frames, quality, grain, file; preview, progress, Cancel removes the partial file.
- **Web export:** `3disle export` or the EXPORT button: one HTML that opens from disk, wasm engine 0.3 MB plus only the
  data it reaches. `tests/web.sh` opens pages in headless Chromium; a full tour was also walked by mouse and keyboard,
  with a dive, on SwiftShader.
- **Model files:** GLB and STL, every offset checked, fuzzed under the sanitizers, including the JSON of `motor.glb`.
  Import cuts parts where the file branches; split and join. Base colour and metal-roughness maps. Parts of one file take
  apart outward. Old projects read as devices. The credit a file states shows in the viewer, app and video.
- **Data checker:** `3disle check`: 19 files in `public/`, no problems; `tests/app.sh` proves it catches broken data.
- **Engine:** `make test` 658 checks, also clean under `make asan`. All 13 models match the original's node boxes; every
  data file saves back unchanged.
- **Renderer:** side by side with three.js, effectively identical on quad, card, PCB and die.
- **Viewer** (`3disle view`): library, tours, dives and climbs, landing card, HUD, ruler, labels, flow, Disassemble,
  throttle and readouts, hover.
- **Video:** `auto`, `render`, `still`. Determinism checked by hash on one frame.

## Not verified

- **The window itself, by a person.** Everything above ran headless at fixed time steps. `window.c` was smoke-run under
  SDL's offscreen driver: starts, runs, no crash, nothing looked at. Mouse feel, real keyboard, HiDPI, resizing and the
  close-with-unsaved-work dialog in a real window are unchecked.
- A real GPU in a browser: pages ran on SwiftShader only. Other browsers not tried.
- CI runs the engine checks only: `window.c` needs SDL3, which ubuntu-24.04 does not package, so `test-app` and `test-web`
  run by hand. `gh run list` after a push.

## Known bugs

- Pale slabs and bright metal blow out when they catch the key light (FC laminate, capacitor tops, the motor's leads):
  bloom on near-white, the original's weakness too. Telling them from real glows needs an emissive mask in the HDR pass.
- `auto` close-ups aim from assembled boxes plus an estimated spread, so a small part can land off centre.
- Automatic labels can bunch in a column on tall stacks.
- The ruler draws powers of ten only, not the familiar sizes of the original.
- Normal and occlusion maps in a GLB are ignored. Draco, sparse accessors and `.gltf` with separate files are refused or
  skipped with a message. The dropdowns have no scroll, so a very long list runs off the window.
- Part drag moves on the horizontal plane only; height is the Lift field.

## Missing compared with `legacy-ts/`

Opening by drag-and-drop, Cut and X-Ray (already hidden there). `legacy-ts/` can go on the owner's word; tag `ts-final`.

## Log

### 2026-11-07

Built `PLAN.md` items 1 to 3 on the owner's "work without me": the app, web export, model files, checker. Found on the
way: a click held across frames never fired a button (the pressed widget was dropped on the frame carrying the release),
which is every ordinary click, in the viewer too; a failed open left the viewer half replaced; both fixed.

### 2026-11-04

Rewrote the engine in C on the owner's call (`DECISIONS.md` 14): engine, renderer, interface, viewer, timeline, automatic
videos, MP4. Moved TypeScript to `legacy-ts/`, 108 unit tests still pass there.
