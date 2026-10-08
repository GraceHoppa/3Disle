# Install and run

Needs [Node.js](https://nodejs.org) 22 or newer and git.

```bash
git clone git@github.com:allextraszza1488/3Disle.git
cd 3Disle
npm ci
npm run dev
```

Open the address it prints (usually http://127.0.0.1:5173/). The home page lists every tour and
model; the editor is linked from there.

## Open it on a phone

The dev server only listens on this computer. To see it on a phone, forward the port over SSH
(for example from Termux):

```bash
ssh -N -L 5173:127.0.0.1:5173 <user>@<computer-address>
```

then open http://127.0.0.1:5173/ in the phone's browser.

## Checks

```bash
npm run lint && npm run typecheck && npm test && npm run format:check
npm run build && npm run size
npx playwright test          # the viewer in a real browser
```

If Playwright's own browser download stalls, point it at an installed Chromium:
`PW_CHROMIUM=/usr/bin/chromium npx playwright test`.
