# 3Disle

An engine for explorable 3D teardowns of real hardware. The goal is a desktop app: an author
loads 3D models of a device and its parts, marks and names the parts (by object or by polygon),
links them into levels, adds logic (flows, motion, controls), and exports a finished web page
(a folder or a single `.html`). The look and mechanics follow Oleh Havrilko's
[GPU → Atom](https://olehhavrilko.github.io/3dgpuanimation/), which is the design reference;
its code is not in this repo.

**Start by reading `docs/STATE.md` (where things stand) and `docs/PLAN.md` (what is next).**
The owner's answers and wants: `docs/DECISIONS.md` (wins) and `docs/VISION.md` (older).
At the end of every session, update `docs/STATE.md`, commit and push.

## Run and check

See `INSTALL.md`. Before every commit: `npm run lint && npm run typecheck && npm test &&
npm run format:check`; for viewer changes also `npm run build && PW_CHROMIUM=/usr/bin/chromium
npx playwright test`. Never use sudo; nothing here needs it.

## Code map

| Path                                 | What                                                                                                                             |
| ------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------- |
| `src/models/`                        | Engine without DOM (runs in Node tests): assemblies, surfaces, links, explode, section, inspect, projects, loading, data checker |
| `src/viewer/`, `viewer.html`         | The viewer. Today it is the engine's test bench; exported pages will grow out of it                                              |
| `src/editor/`, `editor.html`         | GLB → project editor; saves through `scripts/projects-plugin.ts` (dev server only)                                               |
| `src/home/`, `index.html`            | Start page: tours, models, projects                                                                                              |
| `src/lib/`                           | Modules taken from GPU → Atom (post chain, X-Ray shells, section caps, brackets, routing)                                        |
| `public/models/`, `public/projects/` | Device data; each `index.json` lists what the start page shows                                                                   |

## Rules

- **Devices are data only.** No code for one device. If a device needs something new, add a
  generic field or shape and document it in `docs/ENGINE.md`.
- **Nothing generated pretends to be real.** No auto-made interiors or links; what a page shows
  is what its author made.
- **Facts from sources** in our own example data (datasheets, official pages, teardowns);
  unconfirmed things say "approx.".
- The dev server is often open: create a module before importing it.
- Credit line, exactly: **Created by f3nt**. 3Disle is the owner's alone (decided 2026-10-30).
  GPU → Atom stays credited only where the law needs it: its MIT notice in `LICENSE` and the
  "Taken from GPU → Atom" table in `docs/ARCHITECTURE.md`, for the modules copied into `src/lib/`.
- Repository files explain the code. The exceptions are `docs/DECISIONS.md` and `docs/VISION.md`,
  which record the owner's wants.
- Commit messages explain themselves briefly.

## How the owner works with agents

- One step at a time: build it, test it hard (try to break it), then report "done, check it"
  with screenshots and a one-minute check he can do on his phone. He says ok or what is wrong.
- When he says go on, keep going without stopping; interrupt him only with questions.
- The main agent talks with him and does the editing. Hand small research or repetitive tasks to
  subagents and check what they return.
