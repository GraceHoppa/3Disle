# 3Disle

An engine for explorable 3D teardowns and explainer videos of real hardware, in C. The goal is a desktop app: an author
loads 3D models of a device and its parts, marks and names the parts (by object or by polygon),
links them into levels, adds logic (flows, motion, controls), and exports a finished web page
(a single `.html`) or an MP4. The look and mechanics follow Oleh Havrilko's
[GPU → Atom](https://olehhavrilko.github.io/3dgpuanimation/), which is the design reference;
its code is not in this repo.

**Start by reading `docs/STATE.md` (where things stand) and `docs/PLAN.md` (what is next).**
The owner's answers and wants: `docs/DECISIONS.md` (wins) and `docs/VISION.md` (older).
At the end of every session, update `docs/STATE.md`, commit and push.

## Run and check

See `INSTALL.md`. Before every commit: `make test`, and `make asan` for engine changes. Build warning-free. Never
use sudo. Don't run bare `3disle` from scripts: it opens a window on the owner's screen; use `script`, `shot`,
`still` or `render`. Look at screenshots before saying something works.

## Code map

`docs/ARCHITECTURE.md`. `legacy-ts/` is the old TypeScript version, kept until C covers it; don't extend it.

## Rules

- **Devices are data only.** No code for one device. If a device needs something new, add a
  generic field or shape and document it in `docs/ENGINE.md`.
- **Nothing generated pretends to be real.** No auto-made interiors or links; what a page shows
  is what its author made.
- **Facts from sources** in our own example data (datasheets, official pages, teardowns);
  unconfirmed things say "approx.".
- The dev server is often open: create a module before importing it.
- Credit line, exactly: **Created by f3nt**. 3Disle is the owner's alone (decided 2026-10-30).
  GPU → Atom stays credited only where the law needs it: its MIT notice in `LICENSE` and
  `third_party/NOTICES.md`.
- Repository files explain the code. The exceptions are `docs/DECISIONS.md` and `docs/VISION.md`,
  which record the owner's wants.
- Commit messages explain themselves briefly.

## How the owner works with agents

- Claude leads the project (`docs/DECISIONS.md` 15): decide and build, test hard, show results with
  screenshots. The owner says ok or what is wrong, and interrupts when they want to.
- When told to go on, keep going without stopping; interrupt only with questions.
- The main agent talks with the owner and does the editing. Hand small research or repetitive tasks to
  subagents and check what they return.
