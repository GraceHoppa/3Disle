import * as THREE from 'three';
import { buildAssembly, footprints, type AssemblySpec } from './assembly';
import { isRule, type ControlSpec } from './logic';
import { isProject } from './project';
import { refsOf, resolveRef } from './pack';

/**
 * The data checker: every device file under `public/` opens and everything it names exists.
 * File references (details, tour levels, project models, file parts) resolve inside the data
 * folder; a tour's `into` is a part of its level; links name parts on their layer; wires name
 * parts of their model; every rule follows a control of its own file; project parts name objects
 * that are in the model file; every entry of a start-page list exists. Run by the unit tests.
 */
export interface Issue {
  file: string;
  message: string;
}

/** Bytes of a file under the data folder, or null if there is none. */
export type Reader = (rel: string) => Uint8Array | null;

const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v);
const text = new TextDecoder();

/**
 * Object names in a GLB, as the viewer sees them: GLTFLoader turns spaces into `_` and drops
 * `[ ] . : /`; the raw names are kept too. Empty when the bytes are not a GLB.
 */
export function glbNodeNames(bytes: Uint8Array): Set<string> {
  const names = new Set<string>();
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (bytes.length < 20 || view.getUint32(0, true) !== 0x46546c67 || view.getUint32(16, true) !== 0x4e4f534a)
    return names;
  const length = view.getUint32(12, true);
  if (20 + length > bytes.length) return names;
  let json: unknown;
  try {
    json = JSON.parse(text.decode(bytes.subarray(20, 20 + length)));
  } catch {
    return names;
  }
  if (!isObj(json)) return names;
  for (const list of [json.nodes, json.meshes]) {
    if (!Array.isArray(list)) continue;
    for (const n of list) {
      if (!isObj(n) || typeof n.name !== 'string' || !n.name) continue;
      names.add(n.name);
      names.add(THREE.PropertyBinding.sanitizeNodeName(n.name));
    }
  }
  return names;
}

/** Every `{ by: … }` rule anywhere in a file. */
function rulesIn(spec: unknown, out: { by: string }[] = []) {
  if (Array.isArray(spec)) spec.forEach((v) => rulesIn(v, out));
  else if (isObj(spec)) {
    if (isRule(spec)) out.push(spec);
    for (const v of Object.values(spec)) rulesIn(v, out);
  }
  return out;
}

/** Check every file in `files` (paths under the data folder); `read` returns their bytes. */
export async function checkData(files: string[], read: Reader): Promise<Issue[]> {
  const issues: Issue[] = [];
  const parsed = new Map<string, unknown>();
  const jsonAt = (rel: string): unknown => {
    if (parsed.has(rel)) return parsed.get(rel);
    const bytes = read(rel);
    let spec: unknown = undefined;
    if (bytes) {
      try {
        spec = JSON.parse(text.decode(bytes));
      } catch {
        spec = undefined;
      }
    }
    parsed.set(rel, spec);
    return spec;
  };
  const built = new Map<string, Promise<THREE.Object3D | null>>();
  const buildOf = (rel: string) => {
    if (!built.has(rel)) {
      const spec = jsonAt(rel) as AssemblySpec;
      built.set(
        rel,
        buildAssembly(spec, async () => new THREE.Group()).catch(() => null),
      );
    }
    return built.get(rel)!;
  };
  const isAssembly = (spec: unknown): spec is AssemblySpec =>
    isObj(spec) && Array.isArray(spec.layers) && Array.isArray(spec.footprint);

  for (const rel of files.filter((f) => f.endsWith('.json')).sort()) {
    const complain = (message: string) => issues.push({ file: rel, message });
    const spec = jsonAt(rel);
    if (spec === undefined) {
      complain('is not valid JSON');
      continue;
    }

    // A start-page list: every entry's file exists.
    if (rel.endsWith('index.json')) {
      if (!Array.isArray(spec)) complain('should be a list of { name, file }');
      else
        for (const e of spec) {
          const file = isObj(e) && typeof e.file === 'string' ? resolveRef(rel, e.file) : null;
          if (!file || !read(file)) complain(`lists “${isObj(e) ? String(e.file) : e}”, which is not there`);
        }
      continue;
    }

    // Every file it names is inside the data folder and exists.
    for (const ref of refsOf(spec)) {
      if (/^https?:/i.test(ref)) continue;
      const next = resolveRef(rel, ref);
      if (!next) complain(`“${ref}” points outside the data folder`);
      else if (!read(next)) complain(`“${ref}” is missing (${next})`);
    }
    if (!isObj(spec)) continue;

    // Rules follow controls of the same file (a model opened on its own starts from those).
    const controls = new Set(((spec.controls as ControlSpec[] | undefined) ?? []).map((c) => c.id));
    for (const by of new Set(rulesIn(spec).map((r) => r.by)))
      if (!controls.has(by)) complain(`a rule follows “${by}”, which is not one of its controls`);

    // A tour: each level's `into` is a part of that level.
    if (Array.isArray(spec.levels)) {
      for (const level of spec.levels) {
        if (!isObj(level) || typeof level.file !== 'string' || typeof level.into !== 'string') continue;
        const file = resolveRef(rel, level.file);
        const target = file ? jsonAt(file) : undefined;
        let found = false;
        if (isAssembly(target)) found = !!(await buildOf(file!))?.getObjectByName(level.into);
        else if (isProject(target)) found = target.parts.some((p) => p.name === level.into);
        if (!found) complain(`level ${level.file}: “into” names “${level.into}”, which is not a part of it`);
      }
    }

    // A project: its parts name objects that are in the model file.
    if (isProject(spec) && spec.model.toLowerCase().endsWith('.glb')) {
      const model = resolveRef(rel, spec.model);
      const bytes = model ? read(model) : null;
      if (bytes) {
        const have = glbNodeNames(bytes);
        for (const p of spec.parts ?? [])
          for (const n of p.nodes ?? [])
            if (!have.has(n)) complain(`part “${p.name}” names “${n}”, which is not in ${spec.model}`);
      }
    }

    // An assembly: it builds, its links name parts on their own layer, its wires name parts of it.
    if (isAssembly(spec)) {
      for (const layer of spec.layers) {
        if (!layer.links?.length) continue;
        const names = new Set(footprints(layer).map((f) => f.name));
        for (const link of layer.links)
          for (const end of [link.from, ...(Array.isArray(link.to) ? link.to : [link.to])])
            if (!names.has(end))
              complain(`link “${link.name ?? link.from}” names “${end}”, which is not on layer “${layer.name}”`);
      }
      const root = await buildOf(rel);
      if (!root) complain('does not build');
      else
        for (const name of (root.userData.missing as string[] | undefined) ?? [])
          complain(`a wire names “${name}”, which is not a part of it`);
    }
  }
  return issues;
}
