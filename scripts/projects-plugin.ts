import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import type { IncomingMessage, ServerResponse } from 'node:http';
import type { Plugin } from 'vite';

/**
 * Dev-only endpoint that lets the editor save projects into the repo:
 *   PUT /__projects/<slug>/<file>   (file: project.json or a model file)
 * Files land in public/projects/<slug>/, and public/projects/index.json lists every project.
 * Only answers requests from this machine, and only writes inside public/projects.
 */
export const SLUG = /^[a-z0-9][a-z0-9-]{0,63}$/;
export const FILE = /^[A-Za-z0-9][A-Za-z0-9._-]{0,127}\.(json|glb|gltf|stl)$/;
const MAX_BYTES = 100 * 1024 * 1024;

export interface IndexEntry {
  name: string;
  file: string;
  note: string;
}

/** Write one project file and keep the index in step. Throws on anything outside the rules. */
export function saveProjectFile(dir: string, slug: string, file: string, data: Buffer) {
  if (!SLUG.test(slug)) throw new Error(`Bad project name: ${slug}`);
  if (!FILE.test(file) || file.includes('..')) throw new Error(`Bad file name: ${file}`);
  if (data.length > MAX_BYTES) throw new Error('File too large');
  const base = resolve(dir);
  const target = resolve(base, slug, file);
  if (!target.startsWith(base + '/')) throw new Error('Outside the projects folder');

  let name = slug;
  if (file === 'project.json') {
    const spec = JSON.parse(data.toString('utf8'));
    if (typeof spec?.name !== 'string' || typeof spec?.model !== 'string') throw new Error('Not a project file');
    name = spec.name;
  }
  mkdirSync(join(base, slug), { recursive: true });
  writeFileSync(target, data);

  if (file === 'project.json') {
    const indexPath = join(base, 'index.json');
    let index: IndexEntry[] = [];
    try {
      index = JSON.parse(readFileSync(indexPath, 'utf8'));
    } catch {
      /* first project */
    }
    const entry = { name, file: `${slug}/project.json`, note: 'Project' };
    const at = index.findIndex((e) => e.file === entry.file);
    if (at >= 0) index[at] = entry;
    else index.push(entry);
    writeFileSync(indexPath, JSON.stringify(index, null, 2) + '\n');
  }
  return target;
}

const isLocal = (req: IncomingMessage) => {
  const a = req.socket.remoteAddress ?? '';
  return a === '127.0.0.1' || a === '::1' || a === '::ffff:127.0.0.1';
};

export function projectsPlugin(dir = 'public/projects'): Plugin {
  return {
    name: 'projects-save',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use('/__projects', (req: IncomingMessage, res: ServerResponse) => {
        const reply = (code: number, body: object) => {
          res.statusCode = code;
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify(body));
        };
        if (!isLocal(req)) return reply(403, { error: 'Only from this machine' });
        if (req.method !== 'PUT') return reply(405, { error: 'Use PUT' });
        const [slug, file, ...rest] = (req.url ?? '').replace(/^\//, '').split('/');
        if (rest.length) return reply(400, { error: 'Bad path' });
        const chunks: Buffer[] = [];
        let size = 0;
        req.on('data', (c: Buffer) => {
          size += c.length;
          if (size > MAX_BYTES) req.destroy();
          else chunks.push(c);
        });
        req.on('end', () => {
          try {
            saveProjectFile(dir, decodeURIComponent(slug ?? ''), decodeURIComponent(file ?? ''), Buffer.concat(chunks));
            reply(200, { saved: `${slug}/${file}` });
          } catch (err) {
            reply(400, { error: err instanceof Error ? err.message : String(err) });
          }
        });
      });
    },
  };
}
