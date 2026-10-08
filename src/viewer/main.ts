import * as THREE from 'three';
import { formatOf, loadModel, parseModel } from '../models/load';
import { buildAssembly, type AssemblySpec } from '../models/assembly';
import { applyProject, isProject } from '../models/project';
import { disposeBvh, enableBvh } from '../models/bvh';
import { explode, explodeStack } from '../models/explode';
import { measure } from '../models/frame';
import { disposeObject } from '../lib/dispose';
import { SectionCut, type Axis } from '../models/section';
import { focusPose, inspectInstance, inspectPart, instanceBox, instanceName, metres } from '../models/inspect';
import { fitBracket, makeBracket } from '../lib/brackets';
import { createStage } from './stage';
import { makeShellMaterial } from '../lib/shells';
import './viewer.css';

/**
 * Model viewer: open an assembly (.json), a project (.json made in the editor), STL or GLB,
 * orbit, explode, cut, click parts to inspect them and dive into their detail models.
 * ?model=<file> picks the file; files can also be dropped or opened.
 */
const canvas = document.getElementById('gl') as HTMLCanvasElement;
const title = document.getElementById('title')!;
const status = document.getElementById('status')!;
const tip = document.getElementById('tip')!;

const { renderer, scene, camera, controls, render, flyTo, frame3q, tickers } = createStage(canvas);
// Dev builds expose the camera so tests can watch transitions.
if (import.meta.env.DEV) Object.assign(window, { __viewer: { camera, controls, scene } });

// ---------------------------------------------------------------- model

let model: THREE.Object3D | null = null;
/** Layered assemblies spread layer by layer; anything else pushes its parts outward. */
let spread: (root: THREE.Object3D, amount: number) => void = explode;
const slider = document.getElementById('explode') as HTMLInputElement;
slider.addEventListener('input', () => {
  if (!model) return;
  applySpread();
  section.fit();
  buildTags();
  render();
});

/**
 * Making room around a selection: the other top-level pieces glide away from the selected one
 * (the piece it sits on only a little), on top of the Disassemble slider; deselecting glides
 * them back. Offsets ease toward their targets each frame.
 */
const room = new Map<THREE.Object3D, { now: THREE.Vector3; to: THREE.Vector3 }>();
let roomMoving = false;

function applySpread() {
  if (!model) return;
  spread(model, +slider.value);
  for (const [o, r] of room) o.position.add(r.now);
  if (selected) fitBracket(selectBracket, boxOfTarget(selected), true);
}

function makeRoom(part: THREE.Object3D | null) {
  if (!model) return;
  // The top-level piece (layer, or project part) the selection belongs to.
  let top: THREE.Object3D | null = part;
  while (top && top.parent !== model) top = top.parent;
  const pieces = model.children.filter((c) => c.visible);
  spread(model, +slider.value);
  const radius = measure(model).radius;
  const layered = spread === explodeStack;
  const at = top ? boxOf(top).getCenter(new THREE.Vector3()) : null;
  const order = layered ? pieces.slice().sort((a, b) => boxOf(a).min.y - boxOf(b).min.y) : pieces;
  const k = top ? order.indexOf(top) : -1;
  for (const [i, o] of order.entries()) {
    let to = new THREE.Vector3();
    if (top && o !== top && at) {
      if (layered) {
        // Above moves up, below moves down; the layer it is mounted on moves least.
        const share = i === k - 1 ? 0.12 : 0.35;
        to = new THREE.Vector3(0, Math.sign(i - k) * radius * share, 0);
      } else {
        to = boxOf(o).getCenter(new THREE.Vector3()).sub(at);
        to.setLength(to.lengthSq() > 1e-9 ? radius * 0.3 : 0);
      }
    }
    const r = room.get(o) ?? { now: new THREE.Vector3(), to: new THREE.Vector3() };
    r.to.copy(to);
    room.set(o, r);
  }
  applySpread();
  roomMoving = true;
  render();
}

tickers.add(() => {
  if (!roomMoving || !model) return false;
  let still = true;
  for (const r of room.values()) {
    r.now.lerp(r.to, 0.12);
    if (r.now.distanceToSquared(r.to) > 1e-6) still = false;
    else r.now.copy(r.to);
  }
  applySpread();
  if (still) {
    roomMoving = false;
    section.fit();
    buildTags();
  }
  return !still;
});

// ---------------------------------------------------------------- x-ray

/** The descent's X-Ray: everything becomes a green Fresnel ghost except the selected part. */
const xrayMat = makeShellMaterial('xray');
// Dimmer than the descent's: whole devices stack many large panels, which add up to white.
(xrayMat.uniforms.uColor.value as THREE.Color).multiplyScalar(0.3);
const xrayButton = document.getElementById('xray')!;
const ghosted = new Map<THREE.Mesh, THREE.Material | THREE.Material[]>();
let xray = false;
function applyXray() {
  ghosted.forEach((mat, mesh) => (mesh.material = mat));
  ghosted.clear();
  xrayButton.setAttribute('aria-pressed', String(xray));
  if (!xray || !model) return render();
  const keep = new Set<THREE.Object3D>();
  selected?.part.traverse((o) => keep.add(o));
  model.traverse((o) => {
    const mesh = o as THREE.Mesh;
    if (!mesh.isMesh || keep.has(o) || o.userData.sectionCap || o.userData.flow) return;
    ghosted.set(mesh, mesh.material);
    mesh.material = xrayMat;
  });
  render();
}
xrayButton.addEventListener('click', () => {
  xray = !xray;
  applyXray();
});

// ---------------------------------------------------------------- labels

/** Labels mode: name tags over the largest named parts, kept apart on screen. */
const tagLayer = document.getElementById('tags')!;
const labelsButton = document.getElementById('labels')!;
let labelsOn = false;
let tagged: { part: THREE.Object3D; el: HTMLElement; top: THREE.Vector3 }[] = [];

function buildTags() {
  tagLayer.replaceChildren();
  tagged = [];
  if (!labelsOn || !model) return;
  const candidates: { part: THREE.Object3D; size: number; box: THREE.Box3 }[] = [];
  model.traverse((o) => {
    if (o === model || !o.name || o.userData.slab || o.userData.sectionCap || !o.visible) return;
    const isPart = o.userData.part || (o.parent && o.parent !== model); // parts on layers, project parts
    if (!isPart || o.userData.scatter) return;
    const box = boxOf(o);
    candidates.push({ part: o, size: box.getSize(new THREE.Vector3()).length(), box });
  });
  // Biggest first, one tag per part; children of an already tagged part are skipped.
  candidates.sort((a, b) => b.size - a.size);
  const picked: typeof candidates = [];
  for (const c of candidates) {
    if (picked.length >= 14) break;
    let inside = false;
    for (let p = c.part.parent; p && p !== model; p = p.parent) if (picked.some((k) => k.part === p)) inside = true;
    if (!inside) picked.push(c);
  }
  tagged = picked.map(({ part, box }) => {
    const el = document.createElement('div');
    el.className = 'tag';
    el.textContent = part.name;
    tagLayer.append(el);
    const c = box.getCenter(new THREE.Vector3());
    return { part, el, top: new THREE.Vector3(c.x, box.max.y, c.z) };
  });
}

const projected = new THREE.Vector3();
/** Place tags on screen; nudge each one up until it clears the tags already placed. */
function placeTags() {
  if (!tagged.length) return false;
  const placed: { x: number; y: number; w: number }[] = [];
  const order = tagged
    .map((t) => {
      projected.copy(t.top).project(camera);
      return { t, x: (projected.x + 1) * 0.5 * innerWidth, y: (1 - projected.y) * 0.5 * innerHeight, z: projected.z };
    })
    .sort((a, b) => b.y - a.y);
  for (const { t, x, y, z } of order) {
    const show = z < 1 && x > 0 && x < innerWidth && y > 0 && y < innerHeight;
    t.el.hidden = !show;
    if (!show) continue;
    const w = t.el.offsetWidth || 80;
    let ty = y - 14;
    // Up to two nudges; a tag that still collides is left out (more appear as you zoom in).
    let clear = false;
    for (let k = 0; k < 3; k++) {
      const hit = placed.find((p) => Math.abs(p.x - x) < (p.w + w) / 2 + 4 && Math.abs(p.y - ty) < 20);
      if (!hit) {
        clear = true;
        break;
      }
      ty = hit.y - 21;
    }
    t.el.hidden = !clear;
    if (!clear) continue;
    placed.push({ x, y: ty, w });
    t.el.style.transform = `translate(${x}px, ${ty}px) translate(-50%, -100%)`;
    t.el.style.setProperty('--lead', `${y - ty}px`);
  }
  return false;
}
tickers.add(() => placeTags());

function setLabels(on: boolean) {
  labelsOn = on;
  labelsButton.setAttribute('aria-pressed', String(on));
  buildTags();
  render();
}
labelsButton.addEventListener('click', () => setLabels(!labelsOn));
window.addEventListener('keydown', (e) => {
  if (e.key.toLowerCase() === 'l' && !(e.target instanceof HTMLInputElement)) setLabels(!labelsOn);
});

// ---------------------------------------------------------------- section cut

const section = new SectionCut(renderer);
const cutBox = document.querySelector<HTMLElement>('.cut')!;
const cutToggle = document.getElementById('cut')!;
const cutPos = document.getElementById('cut-pos') as HTMLInputElement;
const axisButtons = [...document.querySelectorAll<HTMLButtonElement>('[data-axis]')];

function setCut(on: boolean) {
  if (on && model) section.enable(model);
  else section.disable();
  cutBox.classList.toggle('on', section.active);
  cutToggle.setAttribute('aria-pressed', String(section.active));
  render();
}
cutToggle.addEventListener('click', () => setCut(!section.active));
cutPos.addEventListener('input', () => {
  section.set(+cutPos.value);
  render();
});
for (const b of axisButtons) {
  b.addEventListener('click', () => {
    section.setAxis(b.dataset.axis as Axis);
    for (const o of axisButtons) o.setAttribute('aria-pressed', String(o === b));
    render();
  });
}

/** Parts with `spin` (fans, props) turn while shown; the stage keeps drawing while any do. */
let spinners: THREE.Object3D[] = [];
/** Data buses (links): their pulses advance while the flow speed is above zero. */
let flows: THREE.ShaderMaterial[] = [];
const FLOW_SPEEDS = [
  ['Off', 0],
  ['Slow', 0.3],
  ['Realtime', 1],
  ['Burst', 2.5],
] as const;
let flowAt = 2;
let spunAt = 0;
let flowTime = 0;
tickers.add((now) => {
  const speed = FLOW_SPEEDS[flowAt][1];
  if (!spinners.length && !(flows.length && speed)) return false;
  const dt = spunAt ? Math.min((now - spunAt) / 1000, 0.1) : 0;
  spunAt = now;
  for (const o of spinners) o.rotation.y += ((o.userData.spin as number) / 60) * Math.PI * 2 * dt;
  flowTime += dt;
  for (const m of flows) {
    m.uniforms.uTime.value = flowTime;
    m.uniforms.uSpeed.value = speed;
  }
  return true;
});
const flowButton = document.getElementById('flow')!;
flowButton.addEventListener('click', () => {
  flowAt = (flowAt + 1) % FLOW_SPEEDS.length;
  flowButton.textContent = `Flow · ${FLOW_SPEEDS[flowAt][0]}`;
  for (const m of flows) m.uniforms.uSpeed.value = FLOW_SPEEDS[flowAt][1];
  spunAt = 0;
  render();
});

function show(next: THREE.Object3D, name: string, layered: boolean, arrive = 1) {
  setCut(false);
  focus(null);
  xray = false;
  applyXray();
  if (model) {
    highlight(null);
    scene.remove(model);
    disposeBvh(model);
    disposeObject(model, new Set([scene.environment!]));
  }
  model = next;
  spinners = [];
  spunAt = 0;
  next.traverse((o) => o.userData.spin && spinners.push(o));
  flows = [];
  next.traverse((o) => o.userData.flow && flows.push((o as THREE.Mesh).material as THREE.ShaderMaterial));
  flowButton.hidden = !flows.length;
  spread = layered ? explodeStack : explode;
  room.clear();
  roomMoving = false;
  slider.value = '0';
  scene.add(model);
  enableBvh(model);

  void frame3q(model, arrive);

  title.textContent = name;
  document.title = `${name} · Model Viewer`;
  status.textContent = '';
  drawHud();
  buildTags();
  render();
}

/** Turn file bytes into a scene object. `url` is where the file came from, for relative paths inside it. */
async function fromBytes(data: ArrayBuffer, fileName: string, url?: URL) {
  const name = fileName.replace(/\.[^.]+$/, '');
  if (fileName.toLowerCase().endsWith('.json')) {
    const spec = JSON.parse(new TextDecoder().decode(data)) as unknown;
    if (url && Array.isArray((spec as Tour).levels)) {
      const t = spec as Tour;
      tour = { ...t, url, at: 0 };
      landingShown = false;
      buildRuler();
      const first = await fromUrl(t.levels[0].file, url).load();
      return { ...first, name: t.levels[0].title ?? first.name };
    }
    if (isProject(spec)) {
      if (!url) throw new Error('Open projects from the model list, so their model file can be found');
      const model = await loadModel(new URL(spec.model, url).href);
      // glTF is in metres; the engine works in millimetres.
      if ((spec.units ?? 'm') === 'm') model.scale.multiplyScalar(1000);
      const { root, missing } = applyProject(model, spec);
      if (missing.length) console.warn(`${spec.name}: no objects named ${missing.join(', ')}`);
      return { object: root, name: spec.name, layered: false };
    }
    const assembly = spec as AssemblySpec;
    const object = await buildAssembly(assembly, (u) => loadModel(new URL(u, url ?? base).href));
    return { object, name: assembly.name, layered: true };
  }
  const format = formatOf(fileName);
  if (!format) throw new Error(`Can't open ${fileName}: use .stl, .glb, .gltf or .json`);
  return { object: await parseModel(data, format, name), name, layered: false };
}

type Opened = { object: THREE.Object3D; name: string; layered: boolean };

/** A guided descent: levels walked with the arrows, each zooming into a named part of the last. */
interface TourLevel {
  file: string;
  scale?: string;
  title?: string;
  text?: string;
  caption?: string;
  /** The part of this level to zoom into on the way to the next one. */
  into?: string;
}
interface Tour {
  name: string;
  /** Landing card: a short line above the title, and a sentence under it. */
  lead?: string;
  subtitle?: string;
  /** Shown after the last level. */
  finale?: { title: string; text?: string };
  levels: TourLevel[];
}
let tour: (Tour & { url: URL; at: number }) | null = null;

/** Latest open() wins: a slow load that finishes after a newer one is thrown away. */
let ticket = 0;
async function open(load: () => Promise<Opened>, arrive = 1) {
  const mine = ++ticket;
  status.textContent = 'Loading…';
  try {
    const opened = await load();
    if (mine !== ticket) return disposeObject(opened.object);
    show(opened.object, opened.name, opened.layered, arrive);
  } catch (err) {
    if (mine === ticket) status.textContent = err instanceof Error ? err.message : String(err);
  }
}

// ---------------------------------------------------------------- model list

/** Built-in models live in models/ (models/index.json), editor projects in projects/ (projects/index.json). */
const page = new URL('./', location.href);
const base = new URL('models/', page);
const projects = new URL('projects/', page);
interface Entry {
  name: string;
  note: string;
  load: () => Promise<Opened>;
  /** Where the file is, so its own relative links (models, zoom branches) resolve. */
  url?: URL;
  /** How the address bar names it (?model=…): short for built-ins, page-relative otherwise. */
  file?: string;
}
const library = document.getElementById('library')!;
const menu = document.getElementById('menu')!;
const lists = { builtin: document.getElementById('builtin')!, opened: document.getElementById('opened')! };

/** Where you are: the model picked from the list, then every part dived into. */
const trail: Entry[] = [];
const crumbs = document.getElementById('crumbs')!;
function drawCrumbs() {
  crumbs.replaceChildren();
  if (trail.length < 2) return;
  trail.forEach((e, i) => {
    if (i) crumbs.append(Object.assign(document.createElement('span'), { textContent: '›' }));
    if (i === trail.length - 1) return;
    const b = document.createElement('button');
    b.type = 'button';
    b.textContent = e.name;
    b.addEventListener('click', () => void climb(i));
    crumbs.append(b);
  });
}

const fade = document.getElementById('fade')!;
const wait = (ms: number) => new Promise((r) => setTimeout(r, ms));

/**
 * Open a part's own, more detailed model, one level further down. The camera flies into the
 * part while the view dims, the next model loads meanwhile, then it arrives from far away and
 * the camera keeps flying in: one continuous dive.
 */
async function dive(part: THREE.Object3D, box = boxOf(part), name = part.name) {
  const file = part.userData.detail as string | undefined;
  if (file) return diveTo({ ...fromUrl(file, trail.at(-1)?.url ?? base), name }, box);
}

async function diveTo(entry: Entry, box: THREE.Box3 | null, name?: string) {
  trail.push(entry);
  const loading = entry.load();
  focus(null, false);
  const sphere = box ? box.getBoundingSphere(new THREE.Sphere()) : new THREE.Sphere(controls.target.clone(), 1);
  const into = camera.position
    .clone()
    .sub(sphere.center)
    .setLength(sphere.radius * 0.3)
    .add(sphere.center);
  const flying = flyTo(into, sphere.center, 900);
  await wait(450);
  fade.classList.add('on');
  await flying;
  await open(async () => {
    const opened = await loading;
    entry.name = name ?? opened.name; // the detail model's own name, e.g. "GPU package"
    drawCrumbs();
    return { ...opened, name: entry.name };
  }, 5);
  fade.classList.remove('on');
  syncUrl(true);
}

// ---------------------------------------------------------------- where you are, in the address bar

/** The files of the levels you dived through (up to the first generated one), after the model itself. */
const pathFiles = () => {
  const files: string[] = [];
  for (const e of trail.slice(1)) {
    if (!e.file) break;
    files.push(e.file);
  }
  return files;
};
const resolve = (file: string) => fromUrl(file, /^(projects|models)\//.test(file) ? page : base);

/** ?model=<top>&path=<level>|<level>: a refresh lands on the same level; Back goes one level up. */
function syncUrl(push: boolean) {
  const url = new URL(location.href);
  const files = pathFiles();
  if (files.length) url.searchParams.set('path', files.join('|'));
  else url.searchParams.delete('path');
  if (url.href === location.href) return;
  if (push) history.pushState(null, '', url);
  else history.replaceState(null, '', url);
}

window.addEventListener('popstate', async () => {
  const want = new URL(location.href).searchParams.get('path')?.split('|').filter(Boolean) ?? [];
  const have = pathFiles();
  if (want.length < have.length) {
    if (tour) tour.at = want.length;
    await climb(want.length);
  } else {
    for (const file of want.slice(have.length)) {
      if (tour) tour.at++;
      await diveTo(resolve(file), null);
    }
  }
  drawHud();
});

/** After a refresh: rebuild the trail from ?path and open the deepest level directly. */
async function restorePath(files: string[]) {
  if (!files.length) return;
  for (const file of files) trail.push(resolve(file));
  if (tour) tour.at = Math.min(files.length, tour.levels.length - 1);
  landing.hidden = true;
  const deepest = trail[trail.length - 1];
  await open(async () => {
    const opened = await deepest.load();
    deepest.name = tour?.levels[tour.at]?.title ?? opened.name;
    // Breadcrumb names for the levels in between, read from their files.
    await Promise.all(
      trail.slice(1, -1).map(async (e) => {
        try {
          const spec = (await (await fetch(e.url!)).json()) as { name?: string };
          if (spec.name) e.name = spec.name;
        } catch {
          /* keep the file name */
        }
      }),
    );
    drawCrumbs();
    return { ...opened, name: deepest.name };
  });
  cover();
}

// ---------------------------------------------------------------- tour: arrows, auto, HUD

const hud = document.getElementById('hud')!;
const tourBar = document.querySelector<HTMLElement>('.tour')!;
const autoButton = document.getElementById('auto')!;
let busy = false;
let autoTimer = 0;

/** "30 cm", "45 mm", "0.8 mm", "250 µm": a model's largest dimension, the way the descent labels scales. */
function scaleLabel(root: THREE.Object3D) {
  const s = new THREE.Box3().setFromObject(root, true).getSize(new THREE.Vector3());
  const mm = Math.max(s.x, s.y, s.z);
  if (mm >= 100) return `${Math.round(mm / 10)} cm`;
  if (mm >= 10) return `${Math.round(mm)} mm`;
  if (mm >= 1) return `${mm.toFixed(1)} mm`;
  return `${Math.round(mm * 1000)} µm`;
}

/** The first part of the current model that has a deeper model of its own. */
function nextDive() {
  let found: THREE.Object3D | null = null;
  model?.traverse((o) => {
    if (!found && o !== model && o.userData.detail) found = o;
  });
  return found as THREE.Object3D | null;
}

/**
 * The HUD and arrows. A tour file supplies the text per level; any other model gets its scale,
 * its name and how deep you are, and the arrows follow its "Dive in" links.
 */
/**
 * Where the tour is, from where you actually are: a manual dive that lands on a tour level keeps
 * the tour in step; one that leaves its path makes the HUD describe that model instead.
 */
function syncTour() {
  if (!tour) return false;
  const here = trail.at(-1)?.url?.href;
  if (trail.length <= 1) tour.at = 0;
  else {
    const i = tour.levels.findIndex((l) => new URL(l.file, tour!.url).href === here);
    if (i < 0) return false;
    tour.at = i;
  }
  return true;
}

function drawHud() {
  const onTour = syncTour();
  drawStory(onTour);
  const counter = document.getElementById('counter')!;
  const n = (k: number) => String(k).padStart(2, '0');
  const canStep = !!tour || !!nextDive() || trail.length > 1;
  hud.hidden = tourBar.hidden = counter.hidden = !canStep || !model;
  if (!canStep || !model) return;
  if (tour && onTour) {
    const L = tour.levels[tour.at];
    hud.querySelector('.scale')!.textContent = L.scale ?? '';
    hud.querySelector('.ttl')!.textContent = L.title ?? '';
    hud.querySelector('.txt')!.textContent = L.text ?? '';
    hud.querySelector('.cap')!.textContent = L.caption ?? '';
    counter.textContent = `${n(tour.at + 1)} / ${n(tour.levels.length)}`;
  } else {
    const next = nextDive();
    hud.querySelector('.scale')!.textContent = scaleLabel(model);
    hud.querySelector('.ttl')!.textContent = trail.at(-1)?.name ?? model.name;
    hud.querySelector('.txt')!.textContent = (model.userData.info as string) || '';
    hud.querySelector('.cap')!.textContent = next ? `Next: ${next.name}` : 'Deepest level';
    counter.textContent = `Level ${n(trail.length)}`;
  }
  (document.getElementById('prev') as HTMLButtonElement).disabled = tour ? tour.at === 0 : trail.length < 2;
  (document.getElementById('next') as HTMLButtonElement).disabled = !tour && !nextDive(); // in a tour, ▶ at the end opens the finale
}

async function step(dir: 1 | -1) {
  if (busy || !model) return false;
  if (!tour || !syncTour()) {
    // No tour: follow the model's own Dive-in links down, the breadcrumb back up.
    const part = dir > 0 ? nextDive() : null;
    if (dir > 0 && !part) return false;
    if (dir < 0 && trail.length < 2) return false;
    busy = true;
    if (part) await dive(part);
    else await climb(trail.length - 2);
    busy = false;
    drawHud();
    return true;
  }
  const i = tour.at + dir;
  if (i < 0 || i >= tour.levels.length) return false;
  busy = true;
  const L = tour.levels[i];
  const entry: Entry = fromUrl(L.file, tour.url);
  if (dir > 0) {
    const part = tour.levels[tour.at].into ? model.getObjectByName(tour.levels[tour.at].into!) : null;
    tour.at = i;
    drawHud();
    await diveTo(entry, part ? boxOf(part) : null, L.title);
  } else {
    tour.at = i;
    drawHud();
    await climb(trail.length - 2);
  }
  busy = false;
  return true;
}

// ---- field of view: how much you see, and how much bigger than life it looks

/** Things everyone knows the size of, in mm, for a sense of scale. */
const SIZES: [number, string][] = [
  [1700, 'a person'],
  [180, 'a hand'],
  [70, 'a credit card'],
  [10, 'a fingernail'],
  [1, 'a grain of sand'],
  [0.08, 'a hair'],
  [0.008, 'a red blood cell'],
];
const fovEl = document.getElementById('fov')!;
/** CSS pixels are 1/96 inch: the on-screen width of the view in millimetres. */
const SCREEN_MM_PER_PX = 25.4 / 96;
let fovText = '';
tickers.add(() => {
  if (!model) return false;
  const d = camera.position.distanceTo(controls.target);
  const hfov = 2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(camera.fov) / 2) * camera.aspect);
  const across = 2 * d * Math.tan(hfov / 2); // mm of the model visible across the screen
  const zoom = (innerWidth * SCREEN_MM_PER_PX) / across;
  const like = SIZES.reduce((a, b) => (Math.abs(Math.log(b[0] / across)) < Math.abs(Math.log(a[0] / across)) ? b : a));
  const z = zoom >= 10 ? Math.round(zoom).toLocaleString('en') : zoom.toFixed(1);
  const text = `≈ ${fmtLength(across)} across · ×${z} · about ${like[1]}`;
  if (text !== fovText) fovEl.textContent = fovText = text;
  return false;
});

/** 0.0123 mm -> "12 µm", 95 mm -> "9.5 cm", 1700 -> "1.7 m". */
function fmtLength(mm: number) {
  const f = (v: number) => (v >= 10 ? Math.round(v).toString() : v.toFixed(1));
  if (mm >= 1000) return `${f(mm / 1000)} m`;
  if (mm >= 10) return `${f(mm / 10)} cm`;
  if (mm >= 1) return `${f(mm)} mm`;
  return `${f(mm * 1000)} µm`;
}

// ---- story mode: landing card, scale ruler, finale

const landing = document.getElementById('landing')!;
const finale = document.getElementById('finale')!;
const ruler = document.getElementById('ruler')!;
let landingShown = false;

function buildRuler() {
  ruler.replaceChildren();
  if (!tour) return;
  const logs = tour.levels.map((l) => Math.log10(metres(l.scale ?? '')));
  const hi = Math.max(...logs.filter(Number.isFinite));
  const lo = Math.min(...logs.filter(Number.isFinite));
  tour.levels.forEach((l, i) => {
    const li = document.createElement('li');
    li.textContent = l.scale ?? '';
    // Log position, so each tick sits where its size belongs; evenly spaced if sizes are missing.
    const t =
      Number.isFinite(logs[i]) && hi > lo ? (hi - logs[i]) / (hi - lo) : i / Math.max(1, tour!.levels.length - 1);
    li.style.top = `${t * 100}%`;
    ruler.append(li);
  });
}

/** While the landing or finale card is up, the rest of the interface steps back. */
const cover = () => document.body.classList.toggle('covered', !landing.hidden || !finale.hidden);

function drawStory(onTour = true) {
  ruler.hidden = !tour;
  if (!tour) {
    landing.hidden = finale.hidden = true;
    return;
  }
  [...ruler.children].forEach((li, i) => li.setAttribute('aria-current', String(onTour && i === tour!.at)));
  if (!landingShown && tour.at === 0 && !new URLSearchParams(location.search).has('nointro')) {
    landingShown = true;
    landing.querySelector('.lead')!.textContent = tour.lead ?? `${tour.levels.length} levels`;
    landing.querySelector('h1')!.textContent = tour.name;
    landing.querySelector('.sub')!.textContent = tour.subtitle ?? '';
    landing.hidden = false;
    cover();
    (document.getElementById('start-tour') as HTMLButtonElement).focus();
  }
}

function showFinale() {
  if (!tour) return;
  finale.querySelector('h1')!.textContent = tour.finale?.title ?? 'End of the tour';
  finale.querySelector('.sub')!.textContent = tour.finale?.text ?? '';
  finale.hidden = false;
  cover();
}

document.getElementById('start-tour')!.addEventListener('click', () => {
  landing.hidden = true;
  cover();
  setAuto(true);
});
document.getElementById('explore-own')!.addEventListener('click', () => {
  landing.hidden = true;
  cover();
});
document.getElementById('finale-close')!.addEventListener('click', () => {
  finale.hidden = true;
  cover();
});
document.getElementById('restart')!.addEventListener('click', () => location.reload());

function setAuto(on: boolean) {
  clearInterval(autoTimer);
  autoButton.setAttribute('aria-pressed', String(on));
  if (on)
    autoTimer = window.setInterval(async () => {
      if (busy || (await step(1))) return;
      setAuto(false);
      if (tour) showFinale();
    }, 7000);
}
document.getElementById('prev')!.addEventListener('click', () => void step(-1));
document.getElementById('next')!.addEventListener('click', async () => {
  if (!(await step(1)) && tour && tour.at === tour.levels.length - 1) showFinale();
});
autoButton.addEventListener('click', () => setAuto(autoButton.getAttribute('aria-pressed') !== 'true'));
window.addEventListener('keydown', (e) => {
  if (e.key === 'ArrowRight') void step(1);
  if (e.key === 'ArrowLeft') void step(-1);
});

/** Climb back up to an earlier level: pull out of the current one, arrive close and back away. */
async function climb(i: number) {
  const e = trail[i];
  trail.splice(i + 1);
  drawCrumbs();
  const back = camera.position.clone().sub(controls.target).multiplyScalar(3).add(controls.target);
  const flying = flyTo(back, controls.target.clone(), 600);
  fade.classList.add('on');
  await flying;
  await open(async () => ({ ...(await e.load()), name: e.name }), 0.25);
  fade.classList.remove('on');
  syncUrl(false);
}

function select(entry: Entry) {
  tour = null;
  setAuto(false);
  trail.splice(0, trail.length, entry);
  drawCrumbs();
  for (const b of library.querySelectorAll('button'))
    b.setAttribute('aria-current', String(b.dataset.key === entry.name + entry.note));
  setMenu(false);
  if (entry.file) {
    const url = new URL(location.href);
    url.searchParams.set('model', entry.file);
    url.searchParams.delete('path');
    history.replaceState(null, '', url);
  }
  // The list name wins over the file name in the title bar.
  return open(async () => ({ ...(await entry.load()), name: entry.name })).then(drawHud);
}

function add(entry: Entry, where: keyof typeof lists) {
  const b = document.createElement('button');
  b.type = 'button';
  b.dataset.key = entry.name + entry.note;
  const name = document.createElement('span');
  name.textContent = entry.name;
  const note = document.createElement('small');
  note.textContent = entry.note;
  b.append(name, note);
  b.addEventListener('click', () => select(entry));
  const li = document.createElement('li');
  li.append(b);
  lists[where].append(li);
  if (where === 'opened') document.getElementById('opened-head')!.hidden = false;
}

/** An entry for a file at `file`, relative to `from` (the models folder unless said otherwise). */
function fromUrl(file: string, from: URL = base): Entry {
  const url = new URL(file, from);
  const short = url.href.startsWith(base.href) ? url.href.slice(base.href.length) : url.href.slice(page.href.length);
  return {
    name: file
      .split('/')
      .pop()!
      .replace(/\.[^.]+$/, ''),
    note: short,
    file: short,
    url,
    load: async () => {
      const res = await fetch(url);
      if (!res.ok) throw new Error(`Could not load ${short}: ${res.status}`);
      return fromBytes(await res.arrayBuffer(), url.pathname.split('/').pop()!, url);
    },
  };
}

/** Phones: the model name in the bar opens and closes the list. */
function setMenu(openList: boolean) {
  library.classList.toggle('open', openList);
  menu.setAttribute('aria-expanded', String(openList));
}
menu.addEventListener('click', () => setMenu(!library.classList.contains('open')));

function openFile(file: File) {
  const entry: Entry = {
    name: file.name.replace(/\.[^.]+$/, ''),
    note: `${file.name} · ${(file.size / 1024).toFixed(0)} KB`,
    load: async () => fromBytes(await file.arrayBuffer(), file.name),
  };
  add(entry, 'opened');
  select(entry);
}

const input = document.getElementById('file') as HTMLInputElement;
input.addEventListener('change', () => {
  if (input.files?.[0]) openFile(input.files[0]);
  input.value = '';
});

window.addEventListener('dragover', (e) => {
  e.preventDefault();
  document.body.classList.add('drag');
});
window.addEventListener('dragleave', () => document.body.classList.remove('drag'));
window.addEventListener('drop', (e) => {
  e.preventDefault();
  document.body.classList.remove('drag');
  const file = e.dataTransfer?.files[0];
  if (file) openFile(file);
});

async function listed(index: URL, note?: string) {
  try {
    const res = await fetch(new URL('index.json', index));
    if (!res.ok) return [];
    const list = (await res.json()) as { name: string; file: string; note?: string; hidden?: boolean }[];
    return list
      .filter((m) => !m.hidden)
      .map((m) => ({ ...fromUrl(m.file, index), name: m.name, note: note ?? m.note ?? m.file }));
  } catch {
    return [];
  }
}

async function start() {
  // ?embed: just the model, no model list (the compare page uses this).
  if (new URLSearchParams(location.search).has('embed')) document.body.classList.add('embed');
  const entries = [...(await listed(base)), ...(await listed(projects, 'Project'))];
  if (!entries.length) status.textContent = 'No model list found';
  entries.forEach((e) => add(e, 'builtin'));
  const wanted = new URLSearchParams(location.search).get('model');
  const first =
    entries.find((e) => e.file === wanted) ??
    (wanted ? fromUrl(wanted, /^(projects|models)\//.test(wanted) ? page : base) : entries[0]);
  if (first && !entries.includes(first)) add(first, 'builtin');
  const path = new URLSearchParams(location.search).get('path')?.split('|').filter(Boolean) ?? [];
  if (first) await select(first);
  await restorePath(path);
  syncUrl(false);
}

// ---------------------------------------------------------------- hover, click, inspect

const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();

/** What a hit belongs to: its project part if it has one, else the nearest named object. */
function describe(o: THREE.Object3D | null) {
  for (let p = o; p && p !== model; p = p.parent) if (p.userData.part) return p;
  while (o && o !== model && !o.name) o = o.parent;
  return o && o !== model ? o : null;
}

/** What the pointer is on: a part, and for a row of copies, which copy. */
interface Target {
  part: THREE.Object3D;
  index?: number;
}
const asRow = (o: THREE.Object3D) => ((o as THREE.InstancedMesh).isInstancedMesh ? (o as THREE.InstancedMesh) : null);
const sameTarget = (a: Target | null, b: Target | null) => a?.part === b?.part && a?.index === b?.index;

function pick(x: number, y: number): Target | null {
  if (!model) return null;
  pointer.set((x / window.innerWidth) * 2 - 1, -(y / window.innerHeight) * 2 + 1);
  raycaster.setFromCamera(pointer, camera);
  // Ray tests ignore visibility, so skip anything hidden (or inside something hidden).
  const shown = (o: THREE.Object3D | null): boolean => !o || (o.visible && shown(o.parent));
  const hit = raycaster.intersectObject(model, true).find((h) => shown(h.object) && section.keeps(h.point));
  const part = describe(hit?.object ?? null);
  if (!part) return null;
  // Only rows whose copies have names (memory chips M1..M8) are picked one copy at a time; fins and
  // solder balls select as a whole.
  return hit?.object === part && asRow(part)?.userData.names && hit.instanceId !== undefined
    ? { part, index: hit.instanceId }
    : { part };
}

const nameOf = (t: Target) => (t.index !== undefined ? instanceName(asRow(t.part)!, t.index) : t.part.name);
const boxOfTarget = (t: Target) => (t.index !== undefined ? instanceBox(asRow(t.part)!, t.index) : boxOf(t.part));

/** The hovered part glows faintly in the accent colour, like the descent's hover; a single copy is tinted instead. */
let lit: Target | null = null;
const saved = new Map<THREE.MeshStandardMaterial, [number, number]>();
const TINT = new THREE.Color(0.92, 1.12, 0.86);
const WHITE = new THREE.Color(1, 1, 1);
function highlight(t: Target | null) {
  if (sameTarget(t, lit)) return;
  for (const [m, [hex, k]] of saved) {
    m.emissive.setHex(hex);
    m.emissiveIntensity = k;
  }
  saved.clear();
  const row = lit?.index !== undefined ? asRow(lit.part) : null;
  if (row?.instanceColor) {
    row.setColorAt(lit!.index!, WHITE);
    row.instanceColor.needsUpdate = true;
  }
  lit = t;
  if (t?.index !== undefined) {
    const r = asRow(t.part)!;
    r.setColorAt(t.index, TINT);
    r.instanceColor!.needsUpdate = true;
  } else {
    t?.part.traverse((o) => {
      const mat = (o as THREE.Mesh).material as THREE.MeshStandardMaterial | THREE.MeshStandardMaterial[] | undefined;
      for (const m of Array.isArray(mat) ? mat : mat ? [mat] : []) {
        if (!m.emissive || saved.has(m)) continue;
        saved.set(m, [m.emissive.getHex(), m.emissiveIntensity]);
        m.emissive.lerp(new THREE.Color(0x76b900), 0.25);
        m.emissiveIntensity = Math.max(m.emissiveIntensity, 0.12);
      }
    });
  }
  render();
}

// Corner brackets from the descent: dim on hover, bright on the selected part.
const hoverBracket = makeBracket(0xffffff, 0.35);
const selectBracket = makeBracket(0x9cff3a, 0.95);
scene.add(hoverBracket, selectBracket);
const boxOf = (o: THREE.Object3D) => {
  o.updateWorldMatrix(true, true);
  return new THREE.Box3().setFromObject(o, true);
};

function hover(x: number, y: number) {
  const t = pick(x, y);
  highlight(t);
  const isSelected = sameTarget(t, selected);
  hoverBracket.visible = !!t && !isSelected;
  if (t && !isSelected) fitBracket(hoverBracket, boxOfTarget(t), true);
  tip.hidden = !t || isSelected;
  if (!t) return render();
  tip.querySelector('strong')!.textContent = nameOf(t);
  tip.querySelector('span')!.textContent = t.part.userData.info ?? '';
  tip.style.left = `${Math.min(x + 16, window.innerWidth - tip.offsetWidth - 16)}px`;
  tip.style.top = `${Math.min(y + 16, window.innerHeight - tip.offsetHeight - 16)}px`;
  render();
}

// ---- inspector panel

const panel = document.getElementById('inspector')!;
const diveButton = document.getElementById('dive')!;
let selected: Target | null = null;

function focus(t: Target | THREE.Object3D | null, glide = true) {
  if (t instanceof THREE.Object3D) t = { part: t };
  selected = t;
  makeRoom(t?.part ?? null);
  if (xray) applyXray(); // the selected part stays solid
  selectBracket.visible = !!t;
  panel.hidden = !t;
  if (!t) return render();
  const part = t.part;
  setMenu(false);
  tip.hidden = true;
  hoverBracket.visible = false;
  const box = boxOfTarget(t);
  fitBracket(selectBracket, box, true);

  const r = t.index !== undefined ? inspectInstance(asRow(part)!, t.index) : inspectPart(part);
  panel.querySelector('.kicker')!.textContent = r.category;
  panel.querySelector('.name')!.textContent = r.name;
  panel.querySelector('.info')!.textContent = r.info;
  const dl = panel.querySelector('.specs')!;
  dl.replaceChildren(
    ...r.specs.flatMap(([k, v]) => [
      Object.assign(document.createElement('dt'), { textContent: k }),
      Object.assign(document.createElement('dd'), { textContent: v }),
    ]),
  );
  const consists = panel.querySelector<HTMLElement>('.consists')!;
  consists.hidden = r.children.length === 0;
  consists.querySelector('ul')!.replaceChildren(
    ...r.children.map((c) => {
      const b = Object.assign(document.createElement('button'), { type: 'button', textContent: c.name });
      b.addEventListener('click', () => focus(c));
      b.addEventListener('pointerenter', () => highlight({ part: c }));
      b.addEventListener('pointerleave', () => highlight(null));
      const li = document.createElement('li');
      li.append(b);
      return li;
    }),
  );
  diveButton.hidden = !part.userData.detail;

  if (glide) {
    const pose = focusPose(box, camera.fov, camera.aspect, camera.position);
    flyTo(pose.position, pose.target);
  }
  render();
}

panel.querySelector('.close')!.addEventListener('click', () => focus(null));
document.getElementById('recentre')!.addEventListener('click', () => selected && focus(selected));
diveButton.addEventListener('click', () => selected && dive(selected.part, boxOfTarget(selected), nameOf(selected)));
document.getElementById('cut-here')!.addEventListener('click', () => {
  if (!selected || !model) return;
  if (!section.active) setCut(true);
  section.cutThrough(boxOfTarget(selected).getCenter(new THREE.Vector3()));
  cutPos.value = String(section.position);
  render();
});
window.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') focus(null);
});

let hoverAt = 0;
canvas.addEventListener('pointermove', (e) => {
  if (e.pointerType !== 'mouse' || e.buttons || performance.now() - hoverAt < 40) return;
  hoverAt = performance.now();
  hover(e.clientX, e.clientY);
});
// A click or tap (not a drag) selects what is under it; on empty space it closes the panel.
let down: { x: number; y: number } | null = null;
canvas.addEventListener('pointerdown', (e) => (down = { x: e.clientX, y: e.clientY }));
canvas.addEventListener('pointerup', (e) => {
  if (down && Math.hypot(e.clientX - down.x, e.clientY - down.y) < 6) focus(pick(e.clientX, e.clientY));
  down = null;
});
canvas.addEventListener('pointerleave', () => {
  tip.hidden = true;
  hoverBracket.visible = false;
  highlight(null);
});

start();
