# Video output and the Wattson bridge

Recorded 2026-10-26 from the owner (`DECISIONS.md` part 13). A plan, not built yet.

## Who is what

- **3Disle is a program for people.** The engine, viewer and (later) editor a user opens to
  make and watch teardowns. It is not an agent and is not built for agents. Its animation
  already looks close to the Embedr reference video with almost no manual animation work.
- **An agent works on top of it.** Today a Claude Code session drives 3Disle to make
  animations and models (device data, tours, screenshots). That agent is a user of the program,
  not part of it.
- Superseded 2026-11-04: the engine was rewritten in C (`DECISIONS.md` 14). The render below is built in `app/video.c`, without Playwright.

## Offline video rendering

New output next to the interactive page: an MP4 of the same scene, for Embedr-style explainer
videos (boards coming apart, parts lifting off, labels, camera moves). Real time is the enemy:
a slow frame must not drop or stretch the video, so frames are driven by a counter, not by the
clock.

1. **Frame-driven render.** The viewer exposes `renderFrame(i, fps)` (on `window`, behind a URL
   flag such as `?render=1`): time = `i / fps`, update every animation and the camera from that
   time, call the renderer once. Today every per-frame step already takes a `now` from
   `requestAnimationFrame` (`src/viewer/stage.ts`, the `render` loop: `fly(now)`, `tickers`,
   `post.render(dt)`; plus `performance.now()` in `flyTo` and hover), so the work is feeding them
   a synthetic `now = i * 1000 / fps` and stopping the rAF loop while rendering. The known
   frame-rate-dependent easing (make-room, labels; `STATE.md`) has to become time-based first or
   it will look different in the video.
2. **Headless driver.** A script (Playwright is already a dev dependency; the `run-3disle`
   skill drives the app headless) opens the page, loops `i = 0..N`, calls `renderFrame(i)`,
   saves the canvas (`canvas.toDataURL('image/png')` or a screenshot of the canvas) to
   `frames/%05d.png`.
3. **Encode** (width and height must be even for yuv420p):

   ```sh
   ffmpeg -framerate 60 -i frames/%05d.png -c:v libx264 -pix_fmt yuv420p -crf 18 out.mp4
   ```

CCapture.js (hooks the browser clock, records WebM/PNG) is quicker to try but unmaintained since
about 2019; the Playwright + ffmpeg path is the one to keep.

Open: the shot list format. A JSON timeline of camera keys and part moves (tour stops,
Disassemble, dives, throttle) fits next to the existing tours and export.

## Future: the Wattson bridge (reminder to try it)

[Wattson](https://github.com/allextraszza1488/wattson) is the owner's context and tool layer
for doing electronics with Claude Code without hallucinations (every pin, voltage or value
cited from a datasheet, compiled, simulated or measured). The owner's goal: work only on the
real electronics, and let the 3D models, animations and visual design be made automatically
from verified parameters instead of hand-designed.

```
human: designs, solders, measures
  -> Agent A (Wattson): datasheets, compile, sim, ERC, bench readings
  -> device params file (each value sourced, checked, or marked approx.)
  -> Agent B (on top of 3Disle): turns the params into 3Disle device data + a shot list
  -> 3Disle (the program): interactive page + offline render -> MP4
  -> GitHub project + YouTube video
```

It fits this repo's rules: devices are data only, and nothing generated pretends to be real
(anything without a source shows as approx.). The params map onto what the engine already has:
assemblies, 3D `wires` by kind, `controls`, `readouts` and rules (`ENGINE.md`); the FPV quad's
currents and DShot readouts are the kind of values that should come from Agent A.

Details, a first guess at the params file and the steps to try: Wattson
`docs/future-3disle-bridge.md`.
