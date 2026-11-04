# State

Where the project stands. Update at the end of every session.

## Works (C, verified 2026-11-04)

- **Engine:** `make test` 631 checks, also clean under `make asan`. All 13 models match the original's
  node boxes; every data file saves back unchanged.
- **Renderer:** side by side with three.js, effectively identical on quad, card, PCB and die.
- **Viewer** (`build/3disle`): library, tours, dives and climbs, landing card, HUD, ruler, labels, flow,
  Disassemble, throttle and readouts, hover. Headless: dive, dive, climb land on the right levels.
- **Video:** `auto`, `render`, `still`. 82 s at 720p30, 1.5× supersampling: 5.7 s. Determinism checked
  by hash on one frame.
- **Window:** starts from any folder and finds its data beside the binary.

## Not verified

- Clicking a part to open the inspector, finale card, and mouse orbit have not been checked by eye.
- CI for the new jobs: check `gh run list` after this push.

## Known bugs

- Pale slabs blow out when they catch the key light (FC laminate during an automatic video): bloom on
  near-white, the original's weakness too.
- `auto` close-ups aim from assembled boxes plus an estimated spread, so a small part can land off centre.
- Automatic labels can bunch in a column on tall stacks.
- The ruler draws powers of ten only, not the familiar sizes of the original.
- A failed `viewer_open` leaves the previous tour state partly replaced.

## Missing compared with `legacy-ts/`

Editor, one-file web export, GLB and STL, projects and outward explode, data checker, Cut and X-Ray
(already hidden), opening by drag-and-drop. `legacy-ts/` stays until these land, then goes; tag `ts-final`.

## Log

### 2026-11-04

Rewrote the engine in C on the owner's call (`DECISIONS.md` 14): engine, renderer, interface, viewer,
timeline, automatic videos, MP4. Moved TypeScript to `legacy-ts/`, 108 unit tests still pass there. The
editor was next when the owner called a stop; its design is in `PLAN.md`.
