# State

Where the project stands. Update at the end of every session (newest log entry at the bottom).

## Works

- **Viewer** (`viewer.html`): assemblies, GLB projects, STL / GLB files, tours. Disassemble with
  make-room, Labels (L), Flow speeds, hover tips, inspector, per-copy picking for named rows.
  Cut and X-Ray work but their buttons are hidden.
- **Labels**: biggest parts first, one per ~70 px of the model's diagonal on screen (more appear
  as you zoom in), never overlapping; hidden under the landing and finale cards.
- **Look** from GPU → Atom: its card lights (hemisphere, white key, green rim, green under-glow),
  fixed in the world and scaled to each model; full environment light, its bloom, pixel ratio
  and orbit feel; no shadows. Material presets take its metal, PCB, plastic and mould values,
  plus `gunmetal` and `die` (polished bare silicon, used on the RTX die).
- **Credits**: a GLB's `asset.extras` (author, license, title, source, as Sketchfab writes them)
  or a project's `credit` is shown at the bottom right of the viewer, linked to its source.
- **Dives** along `detail` links: fly-in / fade / arrive, breadcrumb, arrows and Auto, `?path=`
  restores the level, Back goes up. A part without `detail` has no Dive.
- **Story mode** for tours: landing card, log-scale ruler, HUD text, finale card.
- **Editor** (`editor.html`): GLB → project with parts by object, saved into `public/projects/`
  through the dev server. A project's `scale` fixes a file saved at the wrong size (viewer and
  editor).
- **Start page** (`index.html`, titled 3Disle): tours, models, projects.
- **Data**: RTX 5070 Ti (card → PCB → package → die, tour, explicit links, each bus drawn once),
  Pixel 6 (3 levels), FPV flight controller tour (FC → MCU); FPV ESC, motor and battery models
  listed on the start page. First downloaded model: `public/projects/motor`, a brushless
  outrunner (CC BY 4.0, credit from the file) with 13 named parts.
- **Checks** (2026-10-08): lint, typecheck, 84 unit tests, format, build, size (device data not
  counted); browser tests 9 passed, 2 skipped (Cut, X-Ray) as of `f0c1403`. CI runs them on
  every push; its first run has not been looked at.

## Known bugs and weak spots

- Labels on concentric parts (the motor's bell, magnets, shaft) line up in a column.
- Disassemble scatters a GLB project: `explode()` pushes every part outward from the model's
  centre, so the motor's leads fly off screen.
- Very shiny metals in downloaded GLBs (roughness ~0) bloom to white.
- The die's thin-film sheen is a static painted gradient, not the reference's angle-dependent
  shader.
- No depth of field.
- Levels change through a fade, not one continuous zoom.
- `src/viewer/main.ts` is ~1100 lines in one module.
- Make-room and label easing are frame-rate dependent.
- Holed slabs get no painted surface.
- The editor selects whole objects only (no polygons), makes no assemblies or tours, and has no
  browser test.
- The size budget counts all pages together.

## Log

### 2026-10-08

Review with the owner, part by part (`docs/DECISIONS.md`). Moved the engine out of the
`3dgpuanimation` fork into this repo: engine, viewer, editor, start page, data and tests; the
modules it used from GPU → Atom copied into `src/lib`. Removed the generated chip interiors,
replaced role-based flow presets with explicit links in the RTX files, hid Cut and X-Ray,
removed the placeholder quad made of boxes, added CI for every push. Named the start page
3Disle; ARCHITECTURE and ENGINE describe this repo. Labels got a budget by on-screen size and
hide under story cards; lights, environment, bloom, pixel ratio and material presets now come
from GPU → Atom. Removed the duplicate RTX memory and PCIe buses; listed the FPV ESC, motor and
battery models. First downloaded model: the brushless motor as a project with 13 named parts,
credited from its file; projects got `scale` for files saved at the wrong size. Next: `PLAN.md`
0.2 continues with the owner's other models (SG90 servo, Arduino Nano, ArduCopter FC), one at a
time; then 1.3 story.
