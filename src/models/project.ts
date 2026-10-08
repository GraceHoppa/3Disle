import * as THREE from 'three';
import type { Credit } from './load';

/**
 * A project turns a model file (GLB from Blender, say) into an explorable object without code:
 * it names parts, groups the file's objects into them, describes them and links "zoom branches".
 * Saved as projects/<name>/project.json next to its model file.
 */
export interface ProjectSpec {
  name: string;
  /** Model file, relative to the project file. */
  model: string;
  /** Units of the model file: glTF and Blender use metres (the default); the engine shows mm. */
  units?: 'm' | 'mm';
  /** Extra factor for files saved at the wrong size (e.g. 0.005 for one drawn 200× too big). */
  scale?: number;
  /** Who made the model file; defaults to the credit inside the file, if it has one. */
  credit?: Credit;
  parts: ProjectPart[];
}

export interface ProjectPart {
  name: string;
  /** Names of the model's objects that make up this part. */
  nodes: string[];
  category?: string;
  info?: string;
  specs?: Record<string, string>;
  /** Zoom branch: another project or assembly file showing this part in detail. */
  detail?: string;
}

/** Factor from the model file's units to the engine's millimetres. */
export function projectScale(spec: Pick<ProjectSpec, 'units' | 'scale'>) {
  const scale = spec.scale !== undefined && Number.isFinite(spec.scale) && spec.scale > 0 ? spec.scale : 1;
  return ((spec.units ?? 'm') === 'm' ? 1000 : 1) * scale;
}

export const isProject = (spec: unknown): spec is ProjectSpec =>
  !!spec && typeof spec === 'object' && typeof (spec as ProjectSpec).model === 'string';

/** Every object in the model that a part can be made of: meshes and named groups. */
export function listNodes(model: THREE.Object3D) {
  const out: THREE.Object3D[] = [];
  model.traverse((o) => {
    if (o !== model && o.name && !o.userData.part) out.push(o);
  });
  return out;
}

/**
 * Regroup a loaded model by the project's parts. Each part becomes one direct child of the
 * returned root (so explode, cut and inspect treat it as one piece), keeping every object
 * exactly where it was. Objects no part claims stay as they are, also as direct children.
 * Names in `nodes` that the model doesn't have are reported, not fatal.
 */
export function applyProject(model: THREE.Object3D, spec: ProjectSpec) {
  model.updateWorldMatrix(true, true);
  const root = new THREE.Group();
  root.name = spec.name;
  const credit = spec.credit ?? model.userData.credit;
  if (credit) root.userData.credit = credit;
  const missing: string[] = [];
  const claimed = new Set<THREE.Object3D>();
  // Look names up before anything moves: once moved into a part, an object is no longer under `model`.
  const byName = new Map<string, THREE.Object3D>();
  for (const o of listNodes(model)) if (!byName.has(o.name)) byName.set(o.name, o);

  for (const p of spec.parts) {
    const group = new THREE.Group();
    group.name = p.name;
    group.userData.part = true;
    group.userData.info = p.info ?? '';
    if (p.category) group.userData.category = p.category;
    if (p.specs) group.userData.specs = p.specs;
    if (p.detail) group.userData.detail = p.detail;
    for (const n of p.nodes) {
      const o = byName.get(n);
      if (!o || claimed.has(o)) {
        if (!o) missing.push(n);
        continue;
      }
      claimed.add(o);
      group.attach(o); // keeps its world transform
    }
    root.add(group);
  }
  // Whatever is left, top-level first, keeps its place in the scene.
  for (const o of [...model.children]) root.attach(o);
  return { root, missing };
}

/** "Key: value" lines (as typed in the editor) -> specs. Lines without a colon are ignored. */
export function parseSpecs(text: string) {
  const out: Record<string, string> = {};
  for (const line of text.split('\n')) {
    const at = line.indexOf(':');
    if (at > 0) out[line.slice(0, at).trim()] = line.slice(at + 1).trim();
  }
  return out;
}
