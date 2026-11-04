# Architecture

C17, GPU through OpenGL ES 3 only, so the same code can run natively and as WebGL2. Field reference:
[ENGINE.md](ENGINE.md). Where things stand: [STATE.md](STATE.md).

```
device.json ─ spec.c ─► AssemblySpec ─ model.c ─► Model (node tree, no GL) ─ render.c ─► frame
                 ▲ edits and saves                  ▲ moved by viewer.c or player_seek in shot.c
shot.json ─ shot.c ─► Shot ─ player_seek(t) ─► ShotFrame ─ video.c ─► ffmpeg
```

| Path | What |
| --- | --- |
| `engine/` | No GL or window; runs in tests. `base.h` arrays, arena, maths. `json`, `spec` (typed device, lossless round trip), `mesh` (shapes, extrude, earcut, tubes), `model` (build, wires, spread, pick), `paint` (software canvas for finishes), `shot` (timeline, player, automatic videos) |
| `gfx/` | `render.c` forward PBR into HDR with MSAA, room environment cube, bloom chain, grade. `shaders/*.glsl` embedded at build. `ui.c` immediate-mode interface |
| `app/` | `viewer` (shared by window and future web page), `stage` (orbit, glides), `video`, `script` (headless driving), `headless` (EGL), `window` (SDL3), `main` |
| `tools/embed.c` | Files to C arrays at build time |
| `tests/` | `test_engine.c`; `golden/boxes.json`, every named node's box from the original |

## Rules that shaped it

- **Time is an argument.** `player_seek(t)` integrates spins and flows on a fixed 240 Hz grid from the
  scene's start and reads everything else at t. Scrubbing and rendering reach the same state; grain is
  seeded by t. Checked: two renders of one frame hash the same.
- **Flows integrate.** Wires hold accumulated phase, so changing the throttle changes speed without
  the pulses jumping, which the original did.
- **Layout stayed.** The same seeded generator and stroke order make the same boards. Instanced
  bounds copy the original's generous box on purpose, since layouts were tuned against it.
- **Saving is lossless.** Unknown JSON fields stay on their object and are written back; tested over
  all data.
- **Parents come before children** in `Model.nodes`, so one forward pass computes world matrices.

## Pitfalls already hit

- `Node *` and `Track *` die when their array grows: use indices, or reserve first (`auto_scene`).
- A wire end is often clamped right onto its first waypoint; a zero-length segment has no direction, so
  `mesh_tube` drops duplicate points.
- The fringe pass must run before bloom is added, or bloom loses red and blue and edges turn green.
- Painted textures are `SRGB8_ALPHA8`: mips average in linear light. Filtering sRGB bytes made carbon
  weave moiré.
- C has no raw strings: shaders live in `.glsl` files.
- `3disle` with an unknown word opens a window. Check that the build succeeded before scripting it.
