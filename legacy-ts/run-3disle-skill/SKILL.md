---
name: run-3disle
description: Run, start, open, drive or screenshot 3Disle (the 3D teardown viewer, editor and start page) in a headless Chromium; check a model or project loads, press keys, read the HUD, test on a phone-sized screen, run the browser tests.
---

# Run 3Disle

3Disle is a Vite web app with three pages: `index.html` (start page), `viewer.html` (viewer and
tours) and `editor.html` (GLB → project). Agents drive it with
`run-3disle-skill/driver.mjs`: it starts its own Vite dev server from this checkout,
opens pages in headless Chromium (WebGL through SwiftShader), runs steps and saves screenshots.
Paths below are relative to the repo root.

## Prerequisites

- Node 22 or newer (tested on 26), `npm ci` done.
- Chromium at `/usr/bin/chromium` (or set `PW_CHROMIUM` to another binary). Playwright's own
  browser download is not used.

## Run (agent path)

```sh
node run-3disle-skill/driver.mjs \
  open:index.html shot:home \
  "open:viewer.html?model=rtx5070ti/tour.json" shot:landing \
  key:Escape wait:800 key:L wait:500 shot:card-labels "text:#hud .ttl" \
  key:ArrowRight wait:4000 "text:#hud .ttl" \
  "eval:document.querySelector('.ruler-marker span').textContent" shot:pcb \
  "open:viewer.html?model=projects/motor/project.json" fill:#explode=1 wait:1500 \
  shot:motor-apart "text:#credit" \
  open:editor.html shot:editor
```

Prints each `text:`/`eval:` result, each saved file, and at the end the page's console errors and
warnings (or `no console errors`). Screenshots land in `/tmp/3disle-shots/NAME.png`; look at them.

Options (before the steps): `--phone` (412×860, DPR 2, touch), `--port N` (default 5190),
`--out DIR`.

Steps, in order:

| Step             | Does                                                             |
| ---------------- | ---------------------------------------------------------------- |
| `open:PATH`      | load the page, wait until `#title` is not "Loading…", then 2.5 s |
| `wait:MS`        | sleep                                                            |
| `key:KEY`        | press a key: `L` labels, `ArrowRight` next level, `Escape` …     |
| `click:SELECTOR` | click the first match                                            |
| `fill:SEL=VALUE` | set an input, e.g. `fill:#explode=1` (Disassemble slider)        |
| `text:SELECTOR`  | print the element's inner text                                   |
| `eval:JS`        | print a JS expression's value from the page                      |
| `shot:NAME`      | screenshot to `OUT/NAME.png`                                     |

What to open:

- A tour: `viewer.html?model=rtx5070ti/tour.json` (starts on the landing card; `Escape`
  explores, `Enter` starts the story). Add `&nointro` to skip the card.
- An assembly: `viewer.html?model=rtx5070ti/card.json`.
- A project (GLB + parts): `viewer.html?model=projects/motor/project.json`.
- What the start page lists: `public/models/index.json`, `public/projects/index.json`.

## Run (human path)

```sh
npx vite --port 5173
```

Then open the `Local:` address it prints (if 5173 is busy, Vite takes the next free port). The
server listens on 127.0.0.1 only; from a phone use an SSH tunnel:
`ssh -N -L 5173:127.0.0.1:5173 user@pc`. Ctrl-C stops it.

## Test

```sh
npm run lint && npm run typecheck && npm test && npm run format:check
npm run build && PW_CHROMIUM=/usr/bin/chromium npx playwright test
```

Browser tests run against `vite preview` on port 4173 (`PW_PORT` changes it); about 3 minutes.

## Gotchas

- **Another checkout may own the usual ports.** Another copy of this repo may be served on 5173,
  and screenshots from 5173 then show that copy. The driver never reuses a server and refuses a
  busy port. Playwright reuses whatever answers on 4173 outside CI, so check `ss -ltnp` first
  or set `PW_PORT`.
- **WebGL needs SwiftShader flags** (`--use-gl=angle --use-angle=swiftshader
--enable-unsafe-swiftshader`); without them Three.js gets no context and the canvas is black.
  The driver and `playwright.config.ts` already pass them. Rendering is slow: a level change
  needs `wait:4000` before its text and camera settle.
- **Landing card covers the viewer on tours.** Labels and HUD are hidden under it; press
  `Escape` or use `&nointro`.
- **Enter on a focused button** clicks the button instead of starting the tour.
- `window.__viewer` (scene, camera, current level) exists on the dev server only, not in a build.
- Cut and X-Ray buttons are hidden (`hidden` attribute); their browser tests are skipped.
- Run Prettier on the files you changed (`npx prettier --write PATH…`), not `.`: it would also
  rewrite untracked files other agents leave in the tree.

## Troubleshooting

- `port 5190 is already in use` → a previous driver run is still alive or someone else uses the
  port; pass `--port 5191`.
- Screenshot shows the wrong page, an old look or a missing model → it came from another
  checkout's server; see the first gotcha.
- `#title` stays "Loading…" → the model path is wrong (check `public/…`) or the GLB is broken; the
  console list at the end shows the 404 or parse error.
