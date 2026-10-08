# State

Where the project stands. Update at the end of every session (newest log entry at the bottom).

## Works

- **Viewer** (`viewer.html`): assemblies, GLB projects, STL / GLB files, tours. Disassemble with
  make-room, Labels (L), Flow speeds, hover tips, inspector, per-copy picking for named rows.
  Cut and X-Ray work but their buttons are hidden.
- **Dives** along `detail` links: fly-in / fade / arrive, breadcrumb, arrows and Auto, `?path=`
  restores the level, Back goes up. A part without `detail` has no Dive.
- **Story mode** for tours: landing card, log-scale ruler, HUD text, finale card.
- **Editor** (`editor.html`): GLB → project with parts by object, saved into `public/projects/`
  through the dev server.
- **Start page** (`index.html`): tours, models, projects.
- **Data**: RTX 5070 Ti (card → PCB → package → die, tour, explicit links), Pixel 6 (3 levels),
  FPV flight controller tour (FC → MCU) plus ESC, motor and battery models.
- **Checks** (2026-10-08): lint, typecheck, 79 unit tests, format, build, size; 7 browser tests
  pass, 2 skipped (Cut, X-Ray). CI runs them on every push; its first run has not been looked at.

## Known bugs and weak spots

- Labels are drawn over the tour's landing card and stack into columns.
- Disassemble looks broken on complex geometry.
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
removed the placeholder quad made of boxes, added CI for every push. Next: `PLAN.md` 0.2, the
owner's downloaded models, one at a time.
