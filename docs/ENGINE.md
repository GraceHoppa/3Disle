# The engine

A data-driven way to build explorable 3D teardowns of real hardware: describe a device in a
JSON file (or point at a GLB and name its parts in the editor), and the viewer gives you the
descent-style experience of GPU → Atom by Oleh Havrilko — exploded layers, hover and inspect,
dives into deeper models with smooth transitions, animated data and power flows, and arrow
navigation. Section cuts and X-Ray are in the engine too, with their buttons hidden for now.

No device-specific code. Everything below is generic; the RTX 5070 Ti, the Pixel 6 and the FPV
flight controller are only data.

## Pages

| Page          | What it is                                                                                                                                                                                                                                                                                                                                                 |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `index.html`  | Start page: every tour, model and saved project, each linked into the viewer; links the editor and [GPU → Atom](https://olehhavrilko.github.io/3dgpuanimation/). Listed files ending in `tour.json` show under Tours.                                                                                                                                      |
| `viewer.html` | The viewer. `?model=<file>` opens a model, assembly, project or tour (relative to `models/`, or a path starting with `models/` or `projects/`); `&path=a.json\|b.json` reopens the levels you dived through; `?embed` hides the model list and the file links; `?nointro` skips a tour's landing card; `?quality=low\|medium\|high` forces a quality tier. |
| `editor.html` | Turns a GLB into a project: select objects, group them into parts, describe, save. Saving works only under the dev server, from this machine.                                                                                                                                                                                                              |

Without `?model=`, the viewer opens the first listed entry. The lists are
`public/models/index.json` and `public/projects/index.json` (written by the editor's Save):
arrays of `{ "name", "file", "note", "hidden" }`, `file` relative to its folder. `hidden` entries
stay out of the lists but still open by `?model=`. The viewer also opens `.stl`, `.glb`, `.gltf`
and assembly `.json` files from disk (Open file, or drop one on the page).

## Three ways to describe a device

Paths inside a file (`detail`, a tour's `file`, a project's `model`, a `file` part) are relative
to that file, with one exception: on a tour's first level, `detail` resolves relative to the tour
file, so keep a tour next to its first level. Projects and tours open from a URL (the list or
`?model=`), not from disk.

**1. Layered assembly** (`public/models/**/*.json`, `src/models/assembly.ts`). A stack of
layers, bottom to top, each a slab (footprint × thickness) with parts on it. Sizes in mm.
The builder stacks layers automatically (each starts above the tallest part below it).
A layer has `name`, `thickness` (0 for a layer that only carries parts), and optionally `color`,
`material`, `surface`, `holes`, `info`, `category`, `specs`, `detail`, `parts` and `links`.

```json
{
  "name": "Main PCB",
  "footprint": [303, 121],
  "layers": [
    {
      "name": "Top copper + mask",
      "thickness": 0.1,
      "surface": "pcb",
      "color": "#14181c",
      "parts": [
        { "name": "GB203 package", "shape": "box", "size": [45, 1, 45], "at": [-45, 0], "detail": "package.json" },
        {
          "name": "GDDR7",
          "shape": "box",
          "size": [14, 1.1, 12],
          "material": "chip",
          "repeat": { "count": 3, "step": [28, 0], "names": ["Memory chip 1", "Memory chip 2", "Memory chip 3"] },
          "specs": { "Capacity": "2 GB", "Speed": "28 Gbps" }
        }
      ],
      "links": [{ "name": "Memory bus", "from": "GB203 package", "to": ["Memory chip 1", "Memory chip 2"], "lanes": 8 }]
    }
  ]
}
```

**2. Project** (`public/projects/<name>/project.json`, `src/models/project.ts`). A GLB (from
Blender, a CAD export, a download) plus a list of parts, each grouping some of the GLB's named
objects (`nodes`), with the same info / specs / detail fields. `units` is `"m"` (the default,
scaled ×1000) or `"mm"`. Made in `editor.html`, no code.

**Credits.** A GLB that carries `asset.extras` with `author` and `license` (Sketchfab downloads
do, with `title` and `source`) is credited automatically; a project can set its own
`credit: { title, author, license, source }` instead. The viewer shows it in the bottom-right
corner with a link to the source, as CC BY licenses require.

**3. Tour** (`tour.json`). A guided path through levels: each level names a model file, its
scale, title, text, caption, and which part to zoom into next (`into`). The arrows and Auto walk
it. Optional `lead` and `subtitle` fill the landing card, `finale: { title, text }` the end card.

```json
{
  "name": "Inside the GeForce RTX 5070 Ti",
  "lead": "Four levels · 30 cm to 20 mm",
  "subtitle": "From the whole MSI card down to the GB203 die.",
  "finale": { "title": "70 of 84 SMs, switched on.", "text": "..." },
  "levels": [
    {
      "file": "card.json",
      "scale": "30 cm",
      "title": "GeForce RTX 5070 Ti",
      "text": "...",
      "caption": "...",
      "into": "Top copper + mask"
    },
    { "file": "pcb.json", "scale": "25 cm", "title": "Main PCB", "text": "...", "caption": "..." }
  ]
}
```

## Building blocks (the "tools")

| Field / shape                                      | Does                                                                                                                                                                                                        | Code                     |
| -------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------ |
| `at`                                               | Centre of the part's base on the slab, [x, z] from the slab centre                                                                                                                                          | `assembly.ts`            |
| `shape: box` (+ `round`)                           | Boxes, rounded corners                                                                                                                                                                                      | `assembly.ts`            |
| `shape: cylinder`, `sphere`                        | Capacitors, standoffs; solder balls, bumps                                                                                                                                                                  | `assembly.ts`            |
| `shape: fan` (+ `blades`, `ring`)                  | Fans and props: tapered pitched blades, optional tip ring                                                                                                                                                   | `assembly.ts`            |
| `spin`                                             | Any part turns about its vertical axis at `spin` rpm while shown                                                                                                                                            | `assembly.ts`            |
| `shape: port` (`dp` / `hdmi` / `usbc`)             | Connector receptacles with the real opening profile and tongue; `facing` turns it (degrees, 0 = opening toward -x)                                                                                          | `assembly.ts`            |
| `shape: outline` (+ `points`, `cutouts`, `finish`) | Any flat outline extruded `height` thick, with round holes: frame plates, arms, brackets; `finish: carbon \| brushed` tiles at real scale                                                                   | `assembly.ts`            |
| `shape: file`                                      | Any STL / GLB as a part, seated on the slab and centred on `at`; used at its own scale                                                                                                                      | `assembly.ts`, `load.ts` |
| `repeat` row / `grid` / `around`                   | Rows, grids, circles (facing outward, from `startAngle`) of copies, drawn as one instanced mesh; `names` makes each copy pickable                                                                           | `assembly.ts`            |
| `scatter`                                          | `repeat.count` small components over an area, same layout every time, keeping clear of other parts                                                                                                          | `assembly.ts`            |
| `lift`                                             | Sit a part on another (die on substrate)                                                                                                                                                                    | `assembly.ts`            |
| `material`                                         | Presets: pcb, copper, gold, aluminium, steel, gunmetal, chip, die (bare silicon), plastic, rubber, ceramic, fibreglass, resin (see-through mould), glass; `color` tints it                                  | `assembly.ts`            |
| `surface`                                          | Painted slab tops: `pcb` (traces, vias, silkscreen outline + name under every part), `traces` (copper routing bundles), `laminate`, `carbon`, `brushed`; `color` sets the base; not on a layer with `holes` | `surfaces.ts`            |
| `holes`                                            | Round openings through a slab (fan openings in a shroud)                                                                                                                                                    | `assembly.ts`            |
| `links`                                            | Buses between named parts, copies or whole rows on the same layer: `lanes` traces (default 6), `width` (0.4 mm), `color`; routed at 45°, pulses run from → to; the Flow button sets the speed               | `links.ts`               |
| `glow`                                             | Emissive parts (LEDs, light bars) that bloom: `true` for the accent green, or a colour                                                                                                                      | `assembly.ts`            |
| `marking`                                          | Text etched on `chip` box tops at least 2 mm across (defaults to the part name)                                                                                                                             | `assembly.ts`            |
| `pattern` (+ `cells`)                              | Silicon structure painted on a die block: `sm-grid` (columns × rows of SM tiles), `sram`, `phy`, `logic`, with a thin-film sheen                                                                            | `surfaces.ts`            |
| `detail`                                           | Link to a deeper model, on a part or a layer: "Dive in", arrows, breadcrumb. Without it there is no Dive                                                                                                    | viewer                   |
| `category`, `info`, `specs`                        | What the inspector shows; facts only from datasheets, approximations marked                                                                                                                                 | viewer                   |

Every bus is written out in `links`; nothing is generated from what a part is.

## What the viewer does with it

- **Disassemble** — layers spread by equal gaps (`explodeStack`), anything else pushes parts outward (`explode`). Selecting a piece makes room: the others ease away from it.
- **Hover / click** — brackets, tooltip, inspector with specs, size, material, copies, "consists of", Re-centre.
- **Dive / climb** — fly into the part, fade, the deeper model arrives from far away; breadcrumb back.
- **Arrows / Auto** — follow a tour, or any model's dive links, with a scale HUD; Auto steps every 7 s and ends a tour on its finale card.
- **Flow** — animated data / power buses: Off, Slow, Realtime, Burst (shown only when the model has buses).
- **Labels** (L) — tags over the largest parts, biggest first, never overlapping; fewer when the model is small on screen, hidden under the story cards.
- **Story** — a tour opens on a landing card, shows a log-scale ruler of its levels and ends on a finale card.
- **Field of view** — live width across the screen, magnification, and a familiar size for comparison.
- **Where you are** — the address bar records the dive path; refresh returns there, Back goes one level up.
- **Keys** — ← / → step, Esc closes the inspector, L toggles labels.
- **Cut** and **X-Ray** — section plane on X / Y / Z with hatched caps (`section.ts`), and a Fresnel ghost of everything except the selected part. Both work; their buttons are hidden in `viewer.html` for now.
- Shadows, the post-processing from GPU → Atom (bloom, grade, vignette), render-on-demand, BVH picking.

## Checks

`npm run lint && npm run typecheck && npm test && npm run format:check`. `npm test` runs the
unit tests in Node (builders, routing, surfaces, explode, section, inspect, loaders, BVH,
projects, save rules). `npm run test:e2e` builds and runs the browser tests (`tests/e2e/`)
against `vite preview` in Chromium with SwiftShader; set `PW_CHROMIUM=/usr/bin/chromium` to use
an installed Chromium. `npm run size` checks the build against `size-budget.json`. CI
(`.github/workflows/ci.yml`) runs all of these on every push and pull request.

The Cut and X-Ray browser tests are skipped while their buttons are hidden. The editor has no
browser test yet. Data files are checked by the tests that open them: the unit tests build
`rtx5070ti/card.json`; the running browser tests open the RTX tour, `card.json`, `pcb.json`,
`die.json`, the drone tour's first level and the Pixel 6 device. The other files (the RTX
`package.json`, `mcu.json`, the Pixel 6 board and SoC, the ESC, motor and battery) are opened by
no running test.

## Adding a device

1. Collect real specs with sources (official pages, datasheets, teardowns).
2. Write an assembly per level (device → board → chip), or export a GLB and name its parts in the editor.
3. Link levels with `detail`; add `links` for the signals and power that matter.
4. Open `viewer.html?model=<your file>`; compare against references; iterate.
5. List it in `public/models/index.json`; name a tour `tour.json` so the start page shows it under Tours.
