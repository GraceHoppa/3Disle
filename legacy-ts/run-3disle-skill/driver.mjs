#!/usr/bin/env node
// Drive 3Disle in a real (headless) Chromium: open pages, press keys, click, read text,
// take screenshots. Starts its own Vite dev server from this checkout (port 5190 by default)
// and stops it at the end; it never reuses a server that is already running, because another
// checkout of this repo may be serving on the usual port. Prints console errors and warnings.
//
//   node run-3disle-skill/driver.mjs [--port 5190] [--phone] [--out DIR] STEP...
//
// Steps run in order:
//   open:PATH        go to http://127.0.0.1:PORT/PATH and wait until the model has loaded
//   wait:MS          sleep
//   key:KEY          press a key (L, ArrowRight, Escape, Enter …)
//   click:SELECTOR   click an element
//   fill:SEL=VALUE   set an input's value (e.g. fill:#explode=1 for Disassemble)
//   text:SELECTOR    print the element's text
//   eval:JS          print the result of a JS expression in the page
//   shot:NAME        save OUT/NAME.png (OUT defaults to /tmp/3disle-shots)
import { spawn } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const { chromium } = await import(join(ROOT, 'node_modules/@playwright/test/index.mjs'));

const args = process.argv.slice(2);
const opt = (name, fallback) => {
  const i = args.indexOf(name);
  if (i < 0) return fallback;
  const [v] = args.splice(i, name.startsWith('--phone') ? 1 : 2).slice(1);
  return v ?? true;
};
const port = Number(opt('--port', 5190));
const phone = opt('--phone', false);
const out = opt('--out', '/tmp/3disle-shots');
const steps = args;
if (!steps.length) {
  console.error('usage: driver.mjs [--port 5190] [--phone] [--out DIR] open:viewer.html?model=… shot:name …');
  process.exit(2);
}
const base = `http://127.0.0.1:${port}/`;

const up = async () => {
  try {
    return (await fetch(base)).ok;
  } catch {
    return false;
  }
};

if (await up()) {
  console.error(`port ${port} is already in use; pass --port with a free one`);
  process.exit(1);
}
const server = spawn(join(ROOT, 'node_modules/.bin/vite'), ['--port', String(port), '--strictPort'], {
  cwd: ROOT,
  stdio: 'ignore',
});
for (let i = 0; i < 100 && !(await up()); i++) await new Promise((r) => setTimeout(r, 200));
if (!(await up())) {
  server.kill();
  throw new Error(`dev server did not start on ${base}`);
}

const browser = await chromium.launch({
  executablePath: process.env.PW_CHROMIUM || '/usr/bin/chromium',
  args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
});
const page = await browser.newPage(
  phone
    ? { viewport: { width: 412, height: 860 }, deviceScaleFactor: 2, hasTouch: true }
    : { viewport: { width: 1400, height: 850 } },
);
const errors = [];
page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`));
page.on('console', (m) => (m.type() === 'error' || m.type() === 'warning') && errors.push(`${m.type()}: ${m.text()}`));
mkdirSync(out, { recursive: true });

try {
  for (const step of steps) {
    const at = step.indexOf(':');
    const [cmd, arg] = [step.slice(0, at), step.slice(at + 1)];
    if (cmd === 'open') {
      await page.goto(base + arg);
      // The viewer says "Loading…" until the model is on screen; other pages just need a moment.
      await page
        .waitForFunction(
          () => !document.querySelector('#title') || document.querySelector('#title').textContent !== 'Loading…',
          null,
          {
            timeout: 90_000,
          },
        )
        .catch(() => {});
      await page.waitForTimeout(2500); // camera glide and first frames
    } else if (cmd === 'wait') await page.waitForTimeout(Number(arg));
    else if (cmd === 'key') await page.keyboard.press(arg);
    else if (cmd === 'click') await page.locator(arg).first().click();
    else if (cmd === 'fill') {
      // The value follows the last '=', so selectors like input[data-control=throttle] work.
      const at = arg.lastIndexOf('=');
      await page
        .locator(arg.slice(0, at))
        .first()
        .fill(arg.slice(at + 1));
    } else if (cmd === 'text') console.log(`${arg}: ${(await page.locator(arg).first().innerText()).trim()}`);
    else if (cmd === 'eval') console.log(`${arg} => ${JSON.stringify(await page.evaluate(arg))}`);
    else if (cmd === 'shot') {
      const file = join(out, `${arg}.png`);
      await page.screenshot({ path: file });
      console.log(`saved ${file}`);
    } else throw new Error(`unknown step "${step}"`);
  }
} finally {
  console.log(errors.length ? `console:\n  ${errors.join('\n  ')}` : 'no console errors');
  await browser.close();
  server.kill();
}
