// Size budget for the production build: fails when what a first visit downloads grows past
// the limits in size-budget.json. Run after `vite build` (npm run size).
// Sizes are gzip, as GitHub Pages serves them; brotli is reported for reference.
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';
import { brotliCompressSync, gzipSync } from 'node:zlib';

const DIST = process.argv[2] ?? 'dist';
const budget = JSON.parse(readFileSync('size-budget.json', 'utf8'));

const files = (dir) =>
  readdirSync(dir).flatMap((name) => {
    const path = join(dir, name);
    return statSync(path).isDirectory() ? files(path) : [path];
  });

const kb = (n) => `${(n / 1024).toFixed(1)} kB`;
const rows = files(DIST).map((path) => {
  const raw = readFileSync(path);
  return {
    file: relative(DIST, path),
    raw: raw.length,
    gzip: gzipSync(raw, { level: 9 }).length,
    brotli: brotliCompressSync(raw).length,
  };
});

// A rule matches files by extension under a folder (assets/*.js, assets/*.css, …).
const failures = [];
const report = [];
for (const rule of budget.rules) {
  const re = new RegExp(rule.match);
  const hit = rows.filter((r) => re.test(r.file));
  const gzip = hit.reduce((s, r) => s + r.gzip, 0);
  const ok = gzip <= rule.maxGzipKB * 1024;
  if (!ok) failures.push(rule.name);
  report.push(`${ok ? 'ok  ' : 'FAIL'}  ${rule.name.padEnd(28)} ${kb(gzip).padStart(10)} / ${rule.maxGzipKB} kB gzip`);
  for (const r of hit.sort((a, b) => b.gzip - a.gzip)) {
    report.push(
      `        ${r.file.padEnd(40)} ${kb(r.raw).padStart(10)} raw ${kb(r.gzip).padStart(10)} gz ${kb(r.brotli).padStart(10)} br`,
    );
  }
}
console.log(report.join('\n'));
if (failures.length) {
  console.error(
    `\nSize budget exceeded: ${failures.join(', ')}. Trim the bundle or raise size-budget.json on purpose.`,
  );
  process.exit(1);
}
