import * as THREE from 'three';
import { formatOf, parseModel } from '../models/load';
import { applyProject, listNodes, parseSpecs, type ProjectPart, type ProjectSpec } from '../models/project';
import { disposeObject } from '../lib/dispose';
import { createStage } from '../viewer/stage';
import '../viewer/viewer.css';
import './editor.css';

/**
 * Project editor: open a GLB, select its objects (click in 3D or in the tree, shift for more),
 * group them into named parts with a description, specs and a zoom branch (detail model), and
 * save the project into the repo through the dev server (public/projects/<name>/).
 */
const canvas = document.getElementById('gl') as HTMLCanvasElement;
const status = document.getElementById('status')!;
const { scene, camera, render, frame3q } = createStage(canvas);

const projects = new URL('projects/', new URL('./', location.href));
let spec: ProjectSpec = { name: 'Untitled', model: 'model.glb', units: 'm', parts: [] };
/** The GLB's bytes when it came from disk, so Save can write it next to the project. */
let modelBytes: ArrayBuffer | null = null;
let model: THREE.Object3D | null = null;
let slug = '';
/** Folders already in public/projects, so a new project never overwrites one. */
let taken = new Set<string>();

const say = (text: string) => (status.textContent = text);
const slugOf = (name: string) =>
  name
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 63) || 'project';

// ---------------------------------------------------------------- loading

async function showModel(bytes: ArrayBuffer, fileName: string) {
  const format = formatOf(fileName);
  if (format !== 'glb' && format !== 'gltf')
    throw new Error('The editor works with GLB / glTF files (Blender: File → Export → glTF 2.0)');
  const next = await parseModel(bytes, format, fileName.replace(/\.[^.]+$/, ''));
  if ((spec.units ?? 'm') === 'm') next.scale.multiplyScalar(1000); // glTF metres -> engine mm
  if (model) {
    scene.remove(model);
    disposeObject(model, new Set([scene.environment!]));
  }
  model = next;
  scene.add(model);
  await frame3q(model);
  selection.clear();
  drawTree();
  drawParts();
  paint();
}

async function openGlb(file: File) {
  modelBytes = await file.arrayBuffer();
  spec = {
    name: file.name.replace(/\.[^.]+$/, ''),
    model: file.name.replace(/[^A-Za-z0-9._-]/g, '_'),
    units: 'm',
    parts: [],
  };
  slug = ''; // chosen at the first save, from the name, never an existing folder
  nameInput.value = spec.name;
  current = -1;
  await showModel(modelBytes, file.name);
  say(`Opened ${file.name}: ${listNodes(model!).length} objects. Select some and press New part.`);
}

async function openProject(file: string) {
  const url = new URL(file, projects);
  const loaded = (await (await fetch(url)).json()) as ProjectSpec;
  const bytes = await (await fetch(new URL(loaded.model, url))).arrayBuffer();
  spec = loaded;
  slug = file.split('/')[0];
  modelBytes = null; // already in the repo
  nameInput.value = spec.name;
  current = spec.parts.length ? 0 : -1;
  await showModel(bytes, loaded.model);
  say(`Opened project ${spec.name}.`);
}

// ---------------------------------------------------------------- selection

const selection = new Set<THREE.Object3D>();
/** Which part each object already belongs to, for the tree and the colours. */
const owner = () => {
  const map = new Map<string, number>();
  spec.parts.forEach((p, i) => p.nodes.forEach((n) => map.set(n, i)));
  return map;
};

const saved = new Map<THREE.MeshStandardMaterial, [number, number]>();
/** Selected objects glow green, objects of the part being edited glow faintly. */
function paint() {
  for (const [m, [hex, k]] of saved) {
    m.emissive.setHex(hex);
    m.emissiveIntensity = k;
  }
  saved.clear();
  const own = owner();
  const tint = (o: THREE.Object3D, hex: number, k: number) =>
    o.traverse((c) => {
      const mats = (c as THREE.Mesh).material;
      for (const m of (Array.isArray(mats) ? mats : mats ? [mats] : []) as THREE.MeshStandardMaterial[]) {
        if (!m.emissive) continue;
        if (!saved.has(m)) saved.set(m, [m.emissive.getHex(), m.emissiveIntensity]);
        m.emissive.setHex(hex);
        m.emissiveIntensity = k;
      }
    });
  if (model) for (const o of listNodes(model)) if (current >= 0 && own.get(o.name) === current) tint(o, 0x2f6fbf, 0.35);
  for (const o of selection) tint(o, 0x76b900, 0.45);
  drawTree();
  render();
}

const raycaster = new THREE.Raycaster();
let down: { x: number; y: number } | null = null;
canvas.addEventListener('pointerdown', (e) => (down = { x: e.clientX, y: e.clientY }));
canvas.addEventListener('pointerup', (e) => {
  if (!down || Math.hypot(e.clientX - down.x, e.clientY - down.y) > 5 || !model) return;
  down = null;
  raycaster.setFromCamera(
    new THREE.Vector2((e.clientX / innerWidth) * 2 - 1, -(e.clientY / innerHeight) * 2 + 1),
    camera,
  );
  const hit = raycaster.intersectObject(model, true)[0]?.object;
  // The named object nearest the hit (a mesh, or its named parent group).
  let o: THREE.Object3D | null | undefined = hit;
  while (o && o !== model && !o.name) o = o.parent;
  if (!e.shiftKey) selection.clear();
  if (o && o !== model) {
    if (selection.has(o)) selection.delete(o);
    else selection.add(o);
  }
  paint();
});

// ---------------------------------------------------------------- object tree

const tree = document.getElementById('tree')!;
function drawTree() {
  if (!model) return tree.replaceChildren();
  const own = owner();
  tree.replaceChildren(
    ...listNodes(model).map((o) => {
      const b = document.createElement('button');
      b.type = 'button';
      let depth = 0;
      for (let p = o.parent; p && p !== model; p = p.parent) depth++;
      b.style.paddingLeft = `${10 + depth * 12}px`;
      b.textContent = o.name;
      b.setAttribute('aria-pressed', String(selection.has(o)));
      const part = own.get(o.name);
      if (part !== undefined) {
        const tag = document.createElement('small');
        tag.textContent = spec.parts[part].name;
        b.append(tag);
      }
      b.addEventListener('click', (e) => {
        if (!e.shiftKey) selection.clear();
        if (selection.has(o)) selection.delete(o);
        else selection.add(o);
        paint();
      });
      return b;
    }),
  );
}

// ---------------------------------------------------------------- parts

let current = -1;
const partsList = document.getElementById('parts')!;
const form = document.getElementById('part-form') as HTMLFormElement;
const field = (name: string) => form.elements.namedItem(name) as HTMLInputElement | HTMLTextAreaElement;

function drawParts() {
  partsList.replaceChildren(
    ...spec.parts.map((p, i) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.textContent = `${p.name} · ${p.nodes.length}`;
      b.setAttribute('aria-current', String(i === current));
      b.addEventListener('click', () => {
        current = i;
        selection.clear();
        if (model) for (const o of listNodes(model)) if (p.nodes.includes(o.name)) selection.add(o);
        drawParts();
        paint();
      });
      return b;
    }),
  );
  form.hidden = current < 0;
  if (current < 0) return;
  const p = spec.parts[current];
  field('name').value = p.name;
  field('category').value = p.category ?? '';
  field('info').value = p.info ?? '';
  field('specs').value = Object.entries(p.specs ?? {})
    .map(([k, v]) => `${k}: ${v}`)
    .join('\n');
  field('detail').value = p.detail ?? '';
  document.getElementById('part-nodes')!.textContent = p.nodes.join(', ') || 'no objects yet';
}

form.addEventListener('input', () => {
  if (current < 0) return;
  const p: ProjectPart = spec.parts[current];
  p.name = field('name').value.trim() || 'Part';
  p.category = field('category').value.trim() || undefined;
  p.info = field('info').value.trim() || undefined;
  const specs = parseSpecs(field('specs').value);
  p.specs = Object.keys(specs).length ? specs : undefined;
  p.detail = field('detail').value.trim() || undefined;
  drawParts();
  drawTree();
});

document.getElementById('new-part')!.addEventListener('click', () => {
  if (!selection.size) return say('Select one or more objects first (click them; shift-click for more).');
  const nodes = [...selection].map((o) => o.name);
  // An object belongs to one part: take it out of any other.
  for (const p of spec.parts) p.nodes = p.nodes.filter((n) => !nodes.includes(n));
  spec.parts.push({ name: nodes.length === 1 ? nodes[0] : `Part ${spec.parts.length + 1}`, nodes });
  current = spec.parts.length - 1;
  drawParts();
  paint();
});

document.getElementById('add-to-part')!.addEventListener('click', () => {
  if (current < 0 || !selection.size) return;
  const nodes = [...selection].map((o) => o.name);
  for (const p of spec.parts) p.nodes = p.nodes.filter((n) => !nodes.includes(n));
  spec.parts[current].nodes.push(...nodes);
  drawParts();
  paint();
});

document.getElementById('delete-part')!.addEventListener('click', () => {
  if (current < 0) return;
  spec.parts.splice(current, 1);
  current = Math.min(current, spec.parts.length - 1);
  drawParts();
  paint();
});

// ---------------------------------------------------------------- save

const nameInput = document.getElementById('project-name') as HTMLInputElement;
nameInput.addEventListener('input', () => (spec.name = nameInput.value.trim() || 'Untitled'));

async function put(path: string, body: BodyInit) {
  const res = await fetch(`/__projects/${path}`, { method: 'PUT', body });
  if (!res.ok)
    throw new Error(((await res.json().catch(() => ({}))) as { error?: string }).error ?? `HTTP ${res.status}`);
}

document.getElementById('save')!.addEventListener('click', async () => {
  if (!model) return say('Open a GLB first.');
  if (!slug) {
    const base = slugOf(spec.name);
    slug = base;
    for (let i = 2; taken.has(slug); i++) slug = `${base}-${i}`;
  }
  try {
    if (modelBytes) await put(`${slug}/${spec.model}`, modelBytes);
    await put(`${slug}/project.json`, JSON.stringify(spec, null, 2) + '\n');
    modelBytes = null;
    const check = applyProject(model.clone(), spec);
    say(
      `Saved to public/projects/${slug}/ (commit it to keep it).` +
        (check.missing.length ? ` Missing objects: ${check.missing.join(', ')}` : ''),
    );
    (document.getElementById('view') as HTMLAnchorElement).href = `viewer.html?model=projects/${slug}/project.json`;
    document.getElementById('view')!.hidden = false;
    await refreshProjects();
  } catch (err) {
    say(
      `Save failed: ${err instanceof Error ? err.message : String(err)} (saving only works from npm run dev on this machine)`,
    );
  }
});

// ---------------------------------------------------------------- open

const fileInput = document.getElementById('file') as HTMLInputElement;
fileInput.addEventListener('change', () => {
  const f = fileInput.files?.[0];
  if (f) openGlb(f).catch((err) => say(String(err instanceof Error ? err.message : err)));
  fileInput.value = '';
});

const picker = document.getElementById('projects') as HTMLSelectElement;
async function refreshProjects() {
  const list = (await (await fetch(new URL('index.json', projects))).json().catch(() => [])) as {
    name: string;
    file: string;
  }[];
  taken = new Set(list.map((p) => p.file.split('/')[0]));
  picker.replaceChildren(
    Object.assign(document.createElement('option'), { value: '', textContent: 'Open a project…' }),
    ...list.map((p) => Object.assign(document.createElement('option'), { value: p.file, textContent: p.name })),
  );
}
picker.addEventListener('change', () => {
  if (picker.value) openProject(picker.value).catch((err) => say(String(err instanceof Error ? err.message : err)));
});
void refreshProjects();
say('Open a GLB (from Blender: File → Export → glTF 2.0 Binary) or an existing project.');
