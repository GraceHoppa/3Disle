# State

Where the project stands. Update at the end of every session (newest log entry at the bottom).

**Revived 2026-10-26.** Next goal besides the viewer: offline video rendering of the scene
(Playwright frames → ffmpeg → MP4), plan in `BRIDGE.md`, tasks in `PLAN.md` stage 5.

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
- **FPV quad** (`public/models/drone/`), built only from engine assemblies: frame, motors, props,
  ESC / FC / VTX stack, battery, camera, receiver, antennas and 36 real wires (battery leads,
  3 phases per motor, the 8-pin stack cable with DShot, CRSF, video through the OSD, 5 V / 9 V,
  RF). A Throttle slider drives prop and bell spin, every current and live readouts (rpm,
  electrical Hz, amps, volts, watts, DShot value; author's numbers, approx.). Tour quad → FC →
  MCU; dives into the ESC (battery bus, shunt, gate signals, phases), the motor (bell spins,
  windings light by phase) and the battery. The throttle carries across dives.
- **Engine additions**: `wires` (3D, by kind, stretch with Disassemble), `controls` / `readouts`
  / rules (`logic.ts`), link `kind` / `level`, layer `base`, prop blades, `pulse`, rings that spin
  about their centre. Downloaded GLB projects are no longer listed (owner, 2026-10-08: build
  models with the engine instead); `public/projects/motor` stays in the repo.
- **Data**: RTX 5070 Ti (card → PCB → package → die, tour, explicit links, each bus drawn once),
  Pixel 6 (3 levels).
- **Export** (`npm run export`): any tour, assembly or project as a folder to host or, with
  `--single`, one HTML that opens from disk. `demo/fpv-quad.html` (`npm run demo`) is the quad
  tour, linked from the README; regenerate it after viewer or quad changes.
- **Data checker** (`src/models/check.ts`, in `npm test`): files, details, tour stops, links,
  wires, rules, project nodes and list entries all resolve.
- **Demo page workflow** (`.github/workflows/pages.yml`): exports the quad on every push to main,
  keeps the one-file page as an artifact, deploys to Pages only while the repo is public (or on a
  manual run). Pages is not switched on yet; the repo is private.
- **Checks** (2026-10-25): lint, typecheck, 108 unit tests, format, build, size (device data not
  counted); browser tests 14 passed, 2 skipped (Cut, X-Ray). CI runs them on every push; its
  runs have not been looked at.

## Known bugs and weak spots

- Labels on concentric parts (the motor's bell, magnets, shaft) line up in a column.
- Disassemble scatters a GLB project: `explode()` pushes every part outward from the model's
  centre, so the motor's leads fly off screen.
- Very shiny metals and near-white parts (white plastic, connectors) bloom to white: the bloom
  threshold from GPU → Atom suits its dark card. Engine data now avoids near-white colours.
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

Later the same day: the owner dropped downloaded models (laptop, SG90 and the rest; their
untracked folders `public/projects/servo-sg90`, `framework16-board` await his OK to delete) and
asked for a full teardown page built with the engine. Added 3D wires with flow kinds, controls
and rules, and the FPV quad tour with real wiring, throttle and dives into ESC, FC and motor.
Run skill `.claude/skills/run-3disle` drives the app headless for screenshots. Next: owner's
notes on the quad; ESC heatsinks hide its buses (lift them or open on Disassemble); camera tilt;
battery wrap; select → isolate; continuous zoom into dives.

### 2026-10-25

Made the repo presentable on GitHub (agent working alone). Ported from the frozen side copy
(re-implemented against current code, not copied): the one-page export (`scripts/export-page.ts`,
`vite.export.config.ts`, `src/models/pack.ts`) and the data checker (now `src/models/check.ts`).
Added `demo/fpv-quad.html`, README screenshots in `docs/media/`, the Demo page workflow, a
browser test for the exported page. Not ported: its airflow (`air.ts`), its logic panel
(`logic-ui.ts`, superseded by the current controls), `calmMetals`, its research notes, its
downloaded models and its diverged data. Next: Pages (owner: make the repo public or use a plan
with private Pages, then Settings → Pages → Source: GitHub Actions); a README GIF; airflow under
the props (PLAN 3.5); the research notes worth keeping; the owner's notes on the quad (10-08 entry).

### 2026-10-26

Docs only, no code changes. The owner revived 3Disle after a pause (`DECISIONS.md` part 13).
New goal: offline video rendering of the three.js scene for Embedr-style explainer videos
(headless Playwright frame capture → ffmpeg → MP4), next to the interactive viewer; `BRIDGE.md`
has the plan and `PLAN.md` stage 5 the tasks. Recorded the framing: 3Disle is a program for
users; an agent works on top of it. Also recorded the future Wattson bridge (verified
electronics params from the owner's Wattson repo feeding the agent that drives 3Disle). No
engine optimisation planned. Next: PLAN 5.1 (time-based easing), then 5.2 `renderFrame`.
