# Plan

Claude leads this project (`DECISIONS.md` 15). In order. Finished items move to `STATE.md`.

## 1. Editor (`app/editor.c`), the main goal

One app with tabs **Build · Animate · View**. `3disle` opens it; `3disle view` stays the viewer.

- **Viewport:** render 3D into an offscreen target sized to the free area, blit, draw panels around it.
  Animate letterboxes to the video's aspect.
- **Build:** left tree (layers, parts, wires; add, duplicate, delete, reorder), right properties for the
  selection, every `spec.h` field. Rules as a control dropdown plus `0:0, 100:29000`.
- **Move parts:** dragging the selected part previews through `off_anim`; commit `at` and rebuild on release.
  Other edits rebuild at most every 30 ms. Sweep unused textures after a rebuild.
- **Undo:** a stack of whole documents as JSON. Open a group on the first change; close it when the mouse is
  up and no field has focus.
- **Animate:** bottom timeline with scene strip and rows per track, draggable keys, scrub, play. Right panel:
  key values, ease, Set camera from view, captions, labels, Auto video. Orbiting detaches the camera from the
  track until the next scrub. `player_new` needs an override so an unsaved document plays.
- **Render:** size, fps, quality, file; one `video_frame` per interface frame with progress and Cancel.
  `video.c` already does the work.
- **Tests:** generalise `script.c` to drive the editor, look at the screenshots.

## 2. Web export

`clang --target=wasm32` with a small libc in `wasm/`, JS forwarding GL to WebGL2, `3disle export` embedding
wasm and reachable data in one HTML. Browser check in headless Chromium.

## 3. GLB, STL, projects, checker

`engine/glb.c` with every offset bounds-checked and a fuzz test on `motor.glb`; project grouping; outward
explode; `shape: file`; `3disle check` ported from `legacy-ts/src/models/check.ts`.

## 4. Then delete `legacy-ts/`, and make it better

Polygon selection, premade parts library, continuous zoom between levels, sound, airflow, Thermal view, script
box, manual, Windows and macOS builds. Later: the Wattson bridge (`BRIDGE.md`).
