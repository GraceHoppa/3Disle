# Plan

Stages in order; tasks top to bottom. Every task: build it, test it hard, report with
screenshots, the owner checks on his phone and says ok. One task per commit (or a few small
ones). Move finished tasks into `STATE.md`.

## 0. Move in

1. ~~**Repository.**~~ Done 2026-10-08, see STATE.
2. **Test models, one at a time.** Start from the owner's downloaded FPV / Arduino models
   (labelled report first; he confirms the labels). Each one: check its license, add it under
   `public/projects/<name>/` with named parts, specs from its datasheet.
   _Done:_ the model opens, its parts select by name, specs show. _Check:_ open it from the start
   page on the phone, tap two parts.
3. **Laptop test (easy).** Framework Laptop 16 mainboard (STEP, CC BY 4.0) converted to GLB by a
   script; one level (board), then board → chip. _Done/check:_ as above.

## 1. Look: match GPU → Atom (copy its values first, then the owner's notes)

1. **Materials and light.** Its lighting, environment, post settings and material presets, so a
   real GLB looks like its Sketchfab / Blender preview. _Files:_ `src/viewer/stage.ts`,
   `src/models/load.ts`, `src/models/assembly.ts`. _Check:_ our RTX card next to its level 1 on
   the live site; a downloaded model next to its source page.
2. **Labels.** No labels over the landing card, no stacked columns; then his notes.
   _Files:_ `src/viewer/main.ts`, `viewer.css`. _Check:_ open the RTX tour, landing is clean;
   press Labels at every level.
3. **Story.** Landing drift, how text appears, acts, key numbers, typography and timings from
   the reference. _Check:_ RTX tour landing and first two levels next to the reference.
4. **Sound.** Ambient and UI sounds like the reference (ElevenLabs may supply them). _Check:_
   sound toggles and plays on the phone.
5. **Continuous zoom.** The next level appears inside the part (scale and position mapped),
   preloaded ahead, no black frame. _Check:_ dive card → PCB → package, it reads as one move.

## 2. Make the engine solid

1. Split `src/viewer/main.ts` (~1100 lines) into modules; behaviour unchanged, tests pass.
2. **Disassemble that works on complex models** (author-set directions, by hierarchy), then
   select → info → isolate. _Check:_ a downloaded drone part comes apart cleanly.
3. Frame-rate-independent easing; speed pass on PC, a laptop and a phone. (Data checker: done,
   see STATE.)
4. ~~**Export**~~ (folder and single `.html`): done 2026-10-25, see STATE. Left: check the
   exported file on the phone.

## 3. Logic and mechanics (one kind at a time, each tested before the next)

1. The core: parts have state (on, rpm, current); rules; trigger → action; controls the author
   puts on the page (slider, button).
2. Data / signal pulses on that core (today's links move onto it).
3. Power and current.
4. Real wires in 3D.
5. Air, heat and motion (props pushing air; a Thermal view).
6. A script box with a small API for anything else (servos, lighting).

## 4. The app

1. Start screen: recent projects with previews, New, Open.
2. Polygon selection: click a connected island, and brush / lasso; the owner tries both.
3. Levels, dives and the main path; text per stop; logic in forms and blocks.
4. Premade parts library: IC packages, passives, connectors, labelled black box.
5. Desktop window around it, with export.
6. Author manual; themes (GPU → Atom's look stays the default).
