/**
 * Which files a teardown reaches, starting from one device file: a tour's levels, every part's or
 * layer's `detail`, a project's model, a `shape: "file"` part's file, and so on down. Used by the
 * data checker and by the export. Paths are relative to `public/`, with forward slashes and no
 * leading slash. No file system here: the caller loads each file.
 */

/** What the caller found at a path: nothing, a binary (GLB, STL), or parsed JSON. */
export type Loaded = 'missing' | 'bin' | { json: unknown };

const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v);

/** File references in a device file, as written (relative to that file). */
export function refsOf(spec: unknown): string[] {
  const out: string[] = [];
  const visit = (o: unknown) => {
    if (Array.isArray(o)) return o.forEach(visit);
    if (!isObj(o)) return;
    if (typeof o.model === 'string') out.push(o.model);
    if (Array.isArray(o.levels)) for (const l of o.levels) if (isObj(l) && typeof l.file === 'string') out.push(l.file);
    if (typeof o.detail === 'string') out.push(o.detail);
    if (o.shape === 'file' && typeof o.file === 'string') out.push(o.file);
    for (const v of Object.values(o)) visit(v);
  };
  visit(spec);
  return out;
}

/**
 * `ref` resolved against the folder of `fromFile`. Null for web, data and blob URLs, and for a
 * path that climbs out of the data folder.
 */
export function resolveRef(fromFile: string, ref: string): string | null {
  if (/^[a-z][a-z0-9+.-]*:/i.test(ref) || ref.startsWith('/')) return null;
  const stack = fromFile.split('/').slice(0, -1);
  for (const p of ref.split(/[?#]/)[0].split('/')) {
    if (p === '' || p === '.') continue;
    if (p === '..') {
      if (!stack.length) return null;
      stack.pop();
    } else stack.push(p);
  }
  return stack.join('/');
}

/** Every file reached from `entry` (entry first), and the references that lead nowhere. */
export function walkDeps(entry: string, load: (rel: string) => Loaded) {
  const files: string[] = [];
  const missing: string[] = [];
  const seen = new Set<string>();
  const queue = [entry];
  while (queue.length) {
    const rel = queue.shift()!;
    if (seen.has(rel)) continue;
    seen.add(rel);
    const got = load(rel);
    if (got === 'missing') {
      missing.push(rel);
      continue;
    }
    files.push(rel);
    if (got === 'bin') continue;
    for (const ref of refsOf(got.json)) {
      const next = resolveRef(rel, ref);
      if (next && !seen.has(next)) queue.push(next);
    }
  }
  return { files, missing };
}
