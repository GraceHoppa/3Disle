/**
 * Export one teardown as a finished page.
 *
 *   npm run export -- models/drone/tour.json                      → out/drone/ (a folder to host)
 *   npm run export -- models/drone/tour.json --single             → also out/drone.html
 *   npm run export -- models/drone/tour.json --single --name fpv-quad --out site
 *
 * The entry is a tour, assembly or project under public/. The folder holds the viewer as
 * index.html, its script and stylesheet, and only the data files that teardown reaches (levels,
 * details, models). `--single` also writes one HTML file with all of that inside, which opens
 * straight from disk: a page opened as file:// cannot fetch its neighbours, so the files ride
 * along in the page and a small fetch shim hands them to the viewer.
 */
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { basename, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { walkDeps, type Loaded } from '../src/models/pack.ts';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const pub = join(root, 'public');

const argv = process.argv.slice(2);
const flag = (name: string) => {
  const i = argv.indexOf(name);
  if (i < 0) return undefined;
  const [value] = argv.splice(i, 2).slice(1);
  return value;
};
const single = argv.includes('--single');
if (single) argv.splice(argv.indexOf('--single'), 1);
const outDir = resolve(root, flag('--out') ?? 'out');
const named = flag('--name');
const entry = argv[0]?.replace(/^public\//, '');
if (!entry || entry.startsWith('-') || !/^(models|projects)\/.+\.json$/.test(entry)) {
  console.error('Usage: npm run export -- <models/… or projects/….json> [--single] [--name NAME] [--out DIR]');
  process.exit(1);
}
if (!existsSync(join(pub, entry))) {
  console.error(`No such file: public/${entry}`);
  process.exit(1);
}

// What the teardown reaches, before spending time on a build.
const load = (rel: string): Loaded => {
  const path = join(pub, rel);
  if (!existsSync(path)) return 'missing';
  return rel.endsWith('.json') ? { json: JSON.parse(readFileSync(path, 'utf8')) as unknown } : 'bin';
};
const { files, missing } = walkDeps(entry, load);
if (missing.length) {
  console.error(`Missing files:\n  ${missing.join('\n  ')}`);
  process.exit(1);
}

const built = spawnSync('npx', ['vite', 'build', '--config', 'vite.export.config.ts', '--logLevel', 'warn'], {
  cwd: root,
  stdio: 'inherit',
});
if (built.status !== 0) process.exit(built.status ?? 1);
const dist = join(root, 'dist-export');

const spec = JSON.parse(readFileSync(join(pub, entry), 'utf8')) as { name?: string; subtitle?: string };
const title = spec.name ?? basename(entry, '.json');
const slug = (named ?? basename(dirname(entry))).replace(/[^a-z0-9-]+/gi, '-').toLowerCase() || 'page';
const escapeHtml = (s: string) =>
  s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]!);

// The viewer lists one entry and opens it: the page needs no ?model=. `embed` hides the model
// list and the links to the rest of the app, which an exported page does not have.
const folder = entry.startsWith('projects/') ? 'projects' : 'models';
const lists: Record<string, string> = {
  'models/index.json': '[]\n',
  'projects/index.json': '[]\n',
  [`${folder}/index.json`]:
    JSON.stringify([{ name: title, file: entry.slice(folder.length + 1), note: 'Exported' }], null, 2) + '\n',
};

let html = readFileSync(join(dist, 'viewer.html'), 'utf8')
  .replace(/<title>[^<]*<\/title>/, `<title>${escapeHtml(title)}</title>`)
  .replace('<body>', '<body class="embed">');
if (spec.subtitle)
  html = html.replace('</title>', `</title>\n    <meta name="description" content="${escapeHtml(spec.subtitle)}" />`);
const assets = [...html.matchAll(/(?:src|href)="\.\/(assets\/[^"]+)"/g)].map((m) => m[1]);

// ---- the folder
const dest = join(outDir, slug);
rmSync(dest, { recursive: true, force: true });
const put = (rel: string, data: string | Buffer) => {
  mkdirSync(dirname(join(dest, rel)), { recursive: true });
  writeFileSync(join(dest, rel), data);
};
put('index.html', html);
for (const rel of assets) put(rel, readFileSync(join(dist, rel)));
put('favicon.svg', readFileSync(join(pub, 'favicon.svg')));
for (const rel of files) put(rel, readFileSync(join(pub, rel)));
for (const [rel, text] of Object.entries(lists)) put(rel, text);
const kb = (n: number) => `${(n / 1024).toFixed(0)} KB`;
console.log(`Folder: ${dest} (${files.length} data files)`);

if (single) {
  // Data rides in the page: JSON as text, anything else as base64. "<" is escaped so no file
  // can close the script it sits in.
  const text: Record<string, string> = { ...lists };
  const b64: Record<string, string> = {};
  for (const rel of files) {
    const data = readFileSync(join(pub, rel));
    if (rel.endsWith('.json')) text[rel] = data.toString('utf8');
    else b64[rel] = data.toString('base64');
  }
  const payload = JSON.stringify({ text, b64 }).replace(/</g, '\\u003c');
  const shim = `<script>
(function () {
  var pack = ${payload};
  var real = window.fetch.bind(window);
  function find(path) {
    for (var k in pack.text) if (path === '/' + k || path.slice(-k.length - 1) === '/' + k) return [k, true];
    for (var b in pack.b64) if (path === '/' + b || path.slice(-b.length - 1) === '/' + b) return [b, false];
    return null;
  }
  window.fetch = function (input, init) {
    var href = typeof input === 'string' ? input : input && input.url ? input.url : String(input);
    var path = '';
    try { path = decodeURIComponent(new URL(href, location.href).pathname); } catch (e) { path = ''; }
    var hit = path && find(path);
    if (!hit) return real(input, init);
    var body;
    if (hit[1]) body = pack.text[hit[0]];
    else {
      var bin = atob(pack.b64[hit[0]]);
      body = new Uint8Array(bin.length);
      for (var i = 0; i < bin.length; i++) body[i] = bin.charCodeAt(i);
    }
    var type = hit[1] ? 'application/json' : 'application/octet-stream';
    return Promise.resolve(new Response(body, { status: 200, headers: { 'Content-Type': type } }));
  };
})();
</script>`;

  let page = html;
  for (const rel of assets) {
    const body = readFileSync(join(dist, rel), 'utf8');
    if (rel.endsWith('.css')) {
      page = page.replace(new RegExp(`<link[^>]+href="\\./${rel}"[^>]*>`), () => `<style>\n${body}</style>`);
    } else {
      // A module script inlined as is; only a literal "</script" could end it early.
      const js = body.replace(/<\/script/gi, '<\\/script');
      if (js.includes('<!--')) throw new Error(`${rel} contains "<!--", which an inline script cannot hold`);
      page = page.replace(
        new RegExp(`<script[^>]+src="\\./${rel}"[^>]*></script>`),
        () => `<script type="module">\n${js}</script>`,
      );
    }
  }
  const icon = readFileSync(join(pub, 'favicon.svg'), 'utf8');
  page = page
    .replace(/href="favicon\.svg"/, () => `href="data:image/svg+xml,${encodeURIComponent(icon)}"`)
    // After <meta charset> (it must come within the first 1024 bytes); a classic script still runs
    // before the module script, which waits for the whole page.
    .replace('</head>', () => `  ${shim}\n  </head>`);
  const file = join(outDir, `${slug}.html`);
  writeFileSync(file, page);
  console.log(`Single file: ${file} (${kb(Buffer.byteLength(page))})`);
}
