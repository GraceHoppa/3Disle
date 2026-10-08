# Architecture

How the engine actually works. Field-by-field reference: [ENGINE.md](ENGINE.md). Current state
and gaps: [STATE.md](STATE.md).

## From a file to a frame

```
viewer.html?model=<file>&path=<a>|<b>
   │
   ▼
viewer/main.ts  start() → select(entry) → open(load) → fromBytes(bytes, name, url)
   │                         │
   │   .json with "levels"   ├─► tour (only from a URL): remember it, load its first level's file
   │   .json with "model"    ├─► project: load the GLB (load.ts), ×1000 if metres,
   │                         │            applyProject() regroups its objects into parts
   │   other .json           ├─► assembly: buildAssembly() (assembly.ts)
   │   .stl / .glb / .gltf   └─► parseModel() (load.ts)
   ▼
open(): the latest call wins; a slower load that finishes after a newer one is disposed
   ▼
show(object): dispose the old model, add the new one, BVH for picking (bvh.ts),
              collect spinners / flow materials, frame the camera (stage.frame3q),
              redraw HUD, story parts, labels
   ▼
stage.ts render(): renders on demand. Each frame: camera glide, tickers (spin, flow pulses,
              make-room easing, labels, field of view), OrbitControls damping, PostFX (the
              post chain from GPU → Atom) draws. Keeps going only while something moves.
```

`stage.ts` is shared by the viewer and the editor: renderer (GPU → Atom's pixel-ratio rule:
at least 1.5×, at most 1.75× on high tier), room environment at full strength, and GPU → Atom's
card lights (hemisphere, white key, green rim, green glow from below), fixed in the world and
scaled to each model by `placeLights()`; the rig is turned so it sits relative to our default view
the way the reference's sits relative to its camera. No shadows (the reference has none).
OrbitControls with the reference's damping and speeds, zoom toward the cursor, camera glides (`flyTo`) and `frame3q` (a three-quarter view that fits the
model). Dev builds expose `window.__viewer` (camera, controls, scene) for tests.

### Building an assembly (`src/models/assembly.ts`)

- Layers are built bottom to top. Each is a `Group` named after the layer (the slab itself is
  unnamed, so clicking it selects the whole layer). The next layer starts above the **tallest
  thing on this one**, not just the slab.
- Slab: `BoxGeometry`, or `holedSlab()` when the layer has `holes`. With `surface` (and no
  `holes`), both the top and bottom faces get a painted canvas (`surfaces.ts`); the bottom omits
  the silkscreen.
- Parts (`buildPart`): box (rounded), cylinder, sphere, fan (`fanGeometry`), port
  (`portGeometry`), outline (`outlineGeometry`), file (loaded through a callback). One copy →
  `Mesh`; several → one `InstancedMesh` whose matrices come from `copyMatrix()` (row, grid,
  circle facing outward, or seeded scatter that avoids other parts' outlines). Parts with `spin`
  become separate meshes so each turns about its own centre.
- Every built object carries its data in `userData`: `info`, `category`, `specs`, `material`,
  `detail`, `names` (per-copy names), `spin`, `flow`. The viewer reads only `userData`.
- Box tops: `markChip()` adds a decal: a silicon `pattern` if set, else (chip material, at least
  2 mm across) an etched marking with a pin-1 dot. Decals follow instanced copies and are not
  pickable.
- Buses: `footprints(layer)` lists every part's outline on the layer (each named copy, plus an
  outline for the whole row; scatter and file parts are left out). Each entry in the layer's
  `links` becomes ribbons (`links.ts`) with a shader that runs pulses from → to. Names not on the
  layer are skipped; a link with no name found draws nothing. There are no generated links: every
  bus is written in the data file.

### The viewer (`src/viewer/main.ts`)

One module, in sections: disassemble + make room, X-Ray, labels, section cut, spin and flow,
loading, model list, dive / climb, URL path, tour arrows + HUD, field of view, story, hover /
click / inspector.

- **Picking**: a raycast against the model (BVH-accelerated), skipping hidden objects and the
  side of a section cut that is removed. A hit maps to the nearest project part, else the
  nearest named object; a named instanced row also gives the copy index.
- **Dive** (`dive` / `diveTo`): fly the camera into the part's box, fade to black, the next model
  loads meanwhile, `frame3q(model, 5)` places the camera far away and glides in. **Climb**: pull
  out, fade, `frame3q(model, 0.25)` glides back. `trail` holds the levels; the breadcrumb and
  `?path=` are drawn from it. Only parts with a `detail` dive; a part without one has no Dive
  button and the arrows pass it by.
- **Arrows**: in a tour, they walk `levels`, zooming into each level's `into` part; anywhere
  else they follow the first part with a `detail` (`nextDive`). `syncTour()` derives the tour's
  level from where you actually are, so a manual Dive in keeps the tour in step.
- **Story**: a tour shows a landing card (once, unless `?nointro` or a restored path), a
  log-scale ruler of `levels[].scale` (`metres()` in `inspect.ts`) and a finale card after the
  last level. While a card is up, `body.covered` hides the rest of the interface.
- **Disassemble**: `explodeStack` for assemblies (equal gaps per layer), `explode` for anything
  else (outward from the centre); both store each piece's home position, so they can be mixed
  and undone. **Make room**: on selection, other top-level pieces get an extra offset (layers:
  above up, below down, the one it sits on least) that eases each frame; cleared on deselect.
- **X-Ray** (button hidden): swaps every mesh's material for the Fresnel shell from
  `lib/shells.ts` (dimmed ×0.3), keeping the selected part, buses and section caps as they are;
  restored before the model changes.
- **Section cut** (`models/section.ts`, buttons hidden): one clipping plane on X / Y / Z,
  hatched caps on solid parts (`lib/sectionCaps.ts`), an outline sized to the model's
  cross-section.

### Projects and the editor

- `project.json` = `{ name, model, units, parts: [{ name, nodes, category, info, specs, detail }] }`.
  `applyProject()` indexes the GLB's objects by name **before** moving any, then `attach()`es
  each into its part group (world transform kept); unclaimed objects stay as they are. Names the
  GLB does not have are reported, not fatal.
- `editor.html`: open a GLB / glTF or a listed project, select objects (3D click or tree, shift
  adds), make parts (an object belongs to one part), edit fields, Save. Save `PUT`s the GLB (when
  it came from disk) and `project.json` to `/__projects/<slug>/<file>` — a Vite middleware
  (`scripts/projects-plugin.ts`) that only exists under the dev server, only answers requests
  from this machine, checks names against strict patterns, writes inside `public/projects/` and
  updates `public/projects/index.json`. A new project never reuses a folder already listed there.
  Saved files must be committed to be kept.

### Taken from GPU → Atom

These modules come from GPU → Atom by Oleh Havrilko
([live](https://olehhavrilko.github.io/3dgpuanimation/)), copied unchanged into `src/lib/`:

| Module                                                                     | Use in the engine                                                                                                                                 |
| -------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `PostFX.ts` (+ `Grade.ts`, `FxaaEffect.ts`, `SeamEffect.ts`, `quality.ts`) | The whole post chain. The stage turns DOF off, uses the descent's first-level grade (`setGrade(0)`) and raises the bloom threshold; seam pass off |
| `sectionCaps.ts` (+ `sectionStack.ts`, its type)                           | Hatched caps for the section cut                                                                                                                  |
| `shells.ts`                                                                | `makeShellMaterial('xray')` for X-Ray                                                                                                             |
| `brackets.ts`                                                              | Corner brackets on hover and selection, in `tight` mode                                                                                           |
| `canvas.ts` (`route45`), `random.ts` (`mulberry32`), `dispose.ts`          | 45° routing (buses, PCB traces), seeded randomness (scatter, surfaces), disposal                                                                  |

Nothing else from it is here: no `LevelManager`, `BaseLevel`, `seam.ts` or `Story.ts`, no
levels, no descent page. Its levels are synchronous code and its story is hardwired to 8 GPU
levels, so the engine has its own loader, tour and story.

## Why it is built this way

**Decisions**

- **Only the engine lives in this repo.** GPU → Atom is the reference, not a dependency: the few
  modules the engine uses are copied into `src/lib/`.
- **Data, not code, per device**, in three forms (assembly JSON, GLB project, tour), because the
  goal is an engine anyone can feed, not more hand-built levels.
- **Millimetres everywhere.** A project's GLB in metres (`units: "m"`, the default) is scaled
  ×1000 on load, in the viewer and the editor; STL is turned Z-up → Y-up. A GLB opened on its own
  or used as a `file` part is taken as it is.
- **Live rendering, not pre-rendered video** for transitions: video would be far larger than the
  code and data, stutters when scrubbed backwards, fixes one aspect ratio, and cannot cover
  models users load.
- **Projects saved into the repo** through a dev-only endpoint, so git is the history; no
  backend.
- **Facts only from sources**, approximations labelled. Only dives an author linked exist: a
  part without `detail` has no Dive (the generated chip interior is gone).
- **Explicit links only**: every bus drawn is listed in its file's `links`; parts no longer get
  buses from a role.
- **The descent's look** (near-black, one green, mono labels, square panels with a green edge)
  is the brief, so the viewer follows it rather than a new visual identity.
- **Cut and X-Ray are hidden, not removed**: their code and unit tests stay; their buttons (and
  "Cut through it") carry `hidden` in `viewer.html`, and their browser tests are skipped.
- **The dev server listens on 127.0.0.1 only** (`vite.config.ts`); another device reaches it
  through an SSH tunnel ([INSTALL.md](../INSTALL.md)). The save endpoint also refuses requests
  from other machines.

**Rejected**

- Pre-rendered video transitions (above). A tree of hand-written `Level` classes per device
  (code per device). Picking every copy of every row one by one (fins and solder balls select as
  a row; only rows with `names` pick per copy). A backend for projects. Generated chip interiors
  for parts without a `detail` model (content not from a source). Role-based flow presets (buses
  the author did not write).

**Pitfalls already hit**

- Vite shows a full-page error if a module imports a file that does not exist yet: write the
  new module first.
- Unit regex `(km|m|cm|mm…)` matched `m` before `mm`: longest units first.
- `three-mesh-bvh` reorders triangles: compare hit points, not face indices.
- `GLTFLoader` with an embedded data URI needs browser APIs (`ProgressEvent`) in Node: test with
  a packed GLB instead.
- Moving GLB objects into groups changes what `getObjectByName` finds: index names first.
- Raycasts ignore `visible`: filter hidden objects yourself.
- A light rig copied from a scene with a different camera glares: GPU → Atom's green rim, used
  as is, reflected off the card's top straight into our camera. Turn the rig with the view.
- Colours in model files were picked for the old, dim lights (key 0.55): after moving to the
  reference's lights (key 2.2) pale colours wash out; prefer the reference's material colours.
- Mould uses `resin`, not `glass`.
- Additive X-Ray on big stacked panels saturates to white: dim the shell colour.
- Corner brackets sized as 22 % of a 300 mm layer run across the screen: `tight` mode.
- Labels pushed up past each other build a tower when zoomed out: give up after two nudges, and
  place at most one tag per ~70 px of the model's diagonal on screen.
- A manual Dive in during a tour left the HUD on the old level: derive the level from the trail.
- A rule with `display: flex` beats the `hidden` attribute: such elements need their own
  `[hidden] { display: none }` (`.cut`, `.tour`, the landing and finale cards).
- Playwright's browser download can stall: `PW_CHROMIUM=/usr/bin/chromium` uses an installed
  Chromium.
