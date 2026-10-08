import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/examples/jsm/geometries/RoundedBoxGeometry.js';
import { mergeGeometries } from 'three/examples/jsm/utils/BufferGeometryUtils.js';
import { mulberry32 } from '../lib/random';
import { buildLink } from './links';
import type { ControlSpec, ReadoutSpec, Value } from './logic';
import { buildWires, type WireKind, type WireSpec } from './wires';
import {
  paintPattern,
  paintSurface,
  surfaceSize,
  type Footprint,
  type PatternKind,
  type SurfaceKind,
} from './surfaces';

/**
 * A layered object described as data instead of code: slabs stacked bottom to top, each with
 * parts sitting on it. All sizes are millimetres; the built model is in millimetres too.
 */
export interface AssemblySpec {
  name: string;
  /** Footprint of every slab: width (x) and depth (z). */
  footprint: [number, number];
  layers: LayerSpec[];
  /** Wires in 3D between any parts, on any layers, with what flows in them. */
  wires?: WireSpec[];
  /** Sliders the visitor moves; rules (`{ by, map }`) in spin, wire and link levels follow them. */
  controls?: ControlSpec[];
  /** Live numbers shown with the controls. */
  readouts?: ReadoutSpec[];
}

export interface LayerSpec {
  name: string;
  /** Slab thickness; 0 for a layer that only carries parts. */
  thickness: number;
  /** Where the layer starts, mm above the first layer's bottom; default: above the tallest part below. */
  base?: number;
  color?: string;
  /** Surface preset; see MATERIALS. `color` tints it. */
  material?: MaterialName;
  info?: string;
  /** Short kind shown above the name in the inspector, e.g. "Board · layer". */
  category?: string;
  /** Facts shown in the inspector, e.g. { "Capacity": "2 GB" }. */
  specs?: Record<string, string>;
  /** Another model file showing this layer in more detail ("Dive in"). */
  detail?: string;
  /** Paint the slab's top: a PCB (traces, vias, silkscreen around this layer's parts), carbon or brushed metal. */
  surface?: SurfaceKind;
  parts?: PartSpec[];
  /** Data buses drawn on this layer between its parts, with pulses running along them. */
  links?: LinkSpec[];
  /** Round openings through the slab (fan openings, shaft and screw holes): centre [x, z] and radius, mm. */
  holes?: { at: [number, number]; radius: number }[];
}

/**
 * A bus between parts on the same layer: `lanes` parallel traces routed at 45° from `from` to
 * each of `to`, lit by pulses that travel from -> to. Names are part names or copy names (M1...).
 */
export interface LinkSpec {
  name?: string;
  from: string;
  to: string | string[];
  /** Parallel traces per connection (default 6). */
  lanes?: number;
  /** Trace width in mm (default 0.4). */
  width?: number;
  color?: string;
  /** What it carries, for its colour and pulses: data (default), power, ground, phase, video, rf. */
  kind?: WireKind;
  /** How much flows, 0..1: a number or a rule on a control (default 1). */
  level?: Value;
  info?: string;
  category?: string;
  specs?: Record<string, string>;
}

export interface PartSpec {
  name: string;
  /** fan: a hub with curved blades (radius, height, blades). sphere: a ball (radius). */
  shape: 'box' | 'cylinder' | 'sphere' | 'fan' | 'port' | 'outline' | 'file';
  /** outline: the shape's corners [x, z] in mm around `at`, extruded `height` thick. */
  points?: [number, number][];
  /** outline: round holes through it, [x, z] around `at`, radius in mm. */
  cutouts?: { at: [number, number]; radius: number }[];
  /** outline: a tiling finish on its faces (carbon weave, brushed metal), at real scale. */
  finish?: 'carbon' | 'brushed';
  /** port: which receptacle (opening profile); the opening faces -x, turned by `facing`. */
  port?: 'dp' | 'hdmi' | 'usbc';
  /** port: turn about the vertical axis, degrees (0: opening faces -x). */
  facing?: number;
  /** fan: number of blades (default 7). */
  blades?: number;
  /** fan: a band joining the blade tips. */
  ring?: boolean;
  /** Spin around the vertical axis while shown, in revolutions per minute (fans, props, rotors);
   *  negative turns the other way. A rule makes it follow a control (throttle → rpm). */
  spin?: Value;
  /** fan: blades of the opposite hand, for props that turn the other way. */
  mirror?: boolean;
  /** fan: `prop` draws slim propeller blades on a small hub instead of a fan's wide ones. */
  blade?: 'fan' | 'prop';
  /** Sprinkle copies at random over an area centred on `at`: [width, depth] in mm. Count comes from repeat.count. */
  scatter?: [number, number];
  /** box: [width, height, depth]. */
  size?: [number, number, number];
  /** cylinder: radius and height (axis is vertical). */
  radius?: number;
  height?: number;
  /** file: model URL, loaded through the `load` callback given to buildAssembly. */
  file?: string;
  color?: string;
  /** Surface preset; see MATERIALS. `color` tints it. */
  material?: MaterialName;
  /** Corner radius for boxes, in mm. */
  round?: number;
  /** Make the part light up (and bloom): true for the accent green, or a colour. */
  glow?: boolean | string;
  info?: string;
  category?: string;
  specs?: Record<string, string>;
  /** Another model file showing this part in more detail ("Dive in"). */
  detail?: string;
  /** Centre of the part's base on the slab's top face, relative to the slab centre: [x, z]. */
  at?: [number, number];
  /** Silicon structure painted on the part's top (die floorplan blocks): see paintPattern. */
  pattern?: PatternKind;
  /** sm-grid: columns × rows of SM tiles (default 2 × 6). */
  cells?: [number, number];
  /** Text etched on a chip's top (chip parts); defaults to the part's name. */
  marking?: string;
  /** Raise the part above the slab top, e.g. to sit a chip on its substrate. */
  lift?: number;
  /** Copies in a row: count and the step between them as [x, z]. Drawn as one instanced mesh.
   *  `names` labels each copy, so a single one can be picked and inspected. */
  repeat?: {
    count: number;
    /** A row: the step between copies as [x, z]. */
    step?: [number, number];
    /** A grid instead: [columns, rows], spaced by `step`; count is columns × rows. */
    grid?: [number, number];
    /** A circle instead: copies on this radius around `at`, each turned to face outward. */
    around?: number;
    /** Where the circle starts, in degrees (0 = +x). */
    startAngle?: number;
    names?: string[];
  };
}

export type FileLoader = (url: string) => Promise<THREE.Object3D>;

/**
 * Surface presets, so a model looks like what it is made of without tuning numbers. Metals,
 * PCB, plastic and mould take GPU → Atom's values (L1Card, L2Pcb).
 */
export const MATERIALS = {
  default: { color: '#9aa3ad', metalness: 0.2, roughness: 0.6 },
  pcb: { color: '#0d3a1c', metalness: 0.1, roughness: 0.55 },
  copper: { color: '#d07a45', metalness: 1, roughness: 0.28 },
  gold: { color: '#ffc35a', metalness: 1, roughness: 0.22 },
  aluminium: { color: '#c3c8cf', metalness: 1, roughness: 0.32 },
  steel: { color: '#8d939b', metalness: 1, roughness: 0.45 },
  /** Dark painted metal, like a cooler shroud. */
  gunmetal: { color: '#24282d', metalness: 0.85, roughness: 0.34 },
  chip: { color: '#0c0d0e', metalness: 0.3, roughness: 0.42 },
  /** Bare silicon: a polished, mirror-like die. */
  die: { color: '#8c96a6', metalness: 1, roughness: 0.08 },
  plastic: { color: '#0b0d0f', metalness: 0.1, roughness: 0.55 },
  rubber: { color: '#16181b', metalness: 0, roughness: 0.92 },
  /** Ceramic capacitor bodies (MLCC tan), as in GPU → Atom. */
  ceramic: { color: '#b08a5a', metalness: 0.2, roughness: 0.5 },
  fibreglass: { color: '#c9b77a', metalness: 0, roughness: 0.75 },
  /** Epoxy mould compound, drawn see-through so the die shows. */
  resin: { color: '#2a2d33', metalness: 0, roughness: 0.55, transparent: true, opacity: 0.4 },
  glass: { color: '#a9c4dc', metalness: 0, roughness: 0.12, transparent: true, opacity: 0.35, clearcoat: 0.6 },
} satisfies Record<string, THREE.MeshPhysicalMaterialParameters>;
export type MaterialName = keyof typeof MATERIALS;

const ACCENT = '#76b900';
const WHITE = new THREE.Color(1, 1, 1);

/** A port's shell (its own material, steel by default) and the dark plastic contact tongue inside. */
function portMaterials(part: PartSpec) {
  return [
    material({ ...part, material: part.material ?? 'steel' }),
    material({ material: 'plastic', color: '#101215' }),
  ];
}

function material(spec: {
  material?: MaterialName;
  color?: string;
  glow?: boolean | string;
  finish?: 'carbon' | 'brushed';
}) {
  const preset = MATERIALS[spec.material ?? 'default'] ?? MATERIALS.default;
  const m = new THREE.MeshPhysicalMaterial({ ...preset, color: spec.color ?? preset.color });
  if (spec.finish) {
    m.map = finishTexture(spec.finish, spec.color);
    m.color.set(0xffffff);
  }
  if (spec.glow) {
    m.emissive.set(spec.glow === true ? ACCENT : spec.glow);
    m.emissiveIntensity = 2.2;
  }
  return m;
}

/** What hover / inspect read from a built object. */
const tag = (
  o: THREE.Object3D,
  name: string,
  info?: string,
  extra: { category?: string; specs?: Record<string, string>; material?: string; detail?: string } = {},
) => {
  o.name = name;
  o.userData.info = info ?? '';
  if (extra.category) o.userData.category = extra.category;
  if (extra.specs) o.userData.specs = extra.specs;
  if (extra.material) o.userData.material = extra.material;
  if (extra.detail) o.userData.detail = extra.detail;
  return o;
};

/**
 * Build a scene object from an assembly spec. Every layer becomes one direct child group
 * (slab plus its parts), so exploding the result separates it layer by layer.
 */
export async function buildAssembly(spec: AssemblySpec, load?: FileLoader): Promise<THREE.Group> {
  const root = tag(new THREE.Group(), spec.name) as THREE.Group;
  if (spec.controls?.length) root.userData.controls = spec.controls;
  if (spec.readouts?.length) root.userData.readouts = spec.readouts;
  const [w, d] = spec.footprint;
  let y = 0;
  for (const layer of spec.layers) {
    if (layer.base !== undefined && Number.isFinite(layer.base)) y = layer.base;
    // The slab itself stays unnamed, so picking it selects the whole layer (slab plus parts).
    const group = tag(new THREE.Group(), layer.name, layer.info, {
      category: layer.category ?? 'Layer',
      specs: layer.specs,
      material: layer.material,
      detail: layer.detail,
    });
    group.position.y = y;
    if (layer.thickness > 0) {
      const slab = new THREE.Mesh(
        layer.holes?.length
          ? holedSlab(w, d, layer.thickness, layer.holes)
          : new THREE.BoxGeometry(w, layer.thickness, d),
        material(layer),
      );
      if (layer.surface && !layer.holes?.length) paintSlab(slab, layer, w, d);
      slab.position.y = layer.thickness / 2;
      slab.userData.slab = true;
      group.add(slab);
    }
    const prints = footprints(layer);
    for (const part of layer.parts ?? []) {
      group.add(await buildPart(part, layer.thickness, load, prints));
    }
    for (const link of layer.links ?? []) {
      const bus = buildLink(link, prints, layer.thickness + 0.03);
      if (bus)
        group.add(
          tag(bus, link.name ?? `${link.from} bus`, link.info, {
            category: link.category ?? (link.kind && link.kind !== 'data' ? 'Power path' : 'Data bus'),
            specs: link.specs,
          }),
        );
    }
    // The next layer starts above the tallest thing on this one, not just above the slab.
    const top = group.children.length ? new THREE.Box3().setFromObject(group, true).max.y - y : 0;
    root.add(group);
    y += Math.max(layer.thickness, top);
  }
  if (spec.wires?.length) {
    const { group, missing } = buildWires(root, spec.wires);
    root.add(group);
    if (missing.length) root.userData.missing = missing;
  }
  return root;
}

async function buildPart(
  part: PartSpec,
  slabTop: number,
  load?: FileLoader,
  avoid: Footprint[] = [],
): Promise<THREE.Object3D> {
  const [x, z] = part.at ?? [0, 0];
  const base = slabTop + (part.lift ?? 0);
  if (part.shape === 'file') {
    if (!part.file || !load) throw new Error(`Part "${part.name}" needs a file and a loader`);
    const model = await load(part.file);
    // Sit the model's lowest point on the slab, centred on `at`.
    const box = new THREE.Box3().setFromObject(model);
    const c = box.getCenter(new THREE.Vector3());
    model.position.add(new THREE.Vector3(x - c.x, base - box.min.y, z - c.z));
    return tag(new THREE.Group().add(model), part.name, part.info, part);
  }

  let geometry: THREE.BufferGeometry;
  let h: number;
  if (part.shape === 'box') {
    const [bw, bh, bd] = part.size ?? [1, 1, 1];
    const r = Math.min(part.round ?? 0, bw / 2, bh / 2, bd / 2);
    geometry = r > 0 ? new RoundedBoxGeometry(bw, bh, bd, 3, r) : new THREE.BoxGeometry(bw, bh, bd);
    h = bh;
  } else if (part.shape === 'fan') {
    h = part.height ?? 1;
    geometry = fanGeometry(part.radius ?? 10, h, part.blades ?? 7, part.ring, part.blade);
    if (part.mirror) mirrorX(geometry);
  } else if (part.shape === 'outline') {
    h = part.height ?? 1;
    geometry = outlineGeometry(part.points ?? [], h, part.cutouts ?? []);
  } else if (part.shape === 'port') {
    const [pw, ph, pd] = part.size ?? [9, 6, 16];
    h = ph;
    geometry = portGeometry(part.port ?? 'dp', pw, ph, pd);
    if (part.facing) geometry.rotateY(THREE.MathUtils.degToRad(part.facing));
  } else if (part.shape === 'sphere') {
    const r = part.radius ?? 0.5;
    h = r * 2;
    geometry = new THREE.SphereGeometry(r, 16, 12);
  } else {
    h = part.height ?? 1;
    geometry = new THREE.CylinderGeometry(part.radius ?? 0.5, part.radius ?? 0.5, h, 32);
  }

  const count = copies(part);
  if (part.spin) {
    // Spinning parts are separate meshes so each turns about its own centre.
    const group = new THREE.Group();
    for (let i = 0; i < count; i++) {
      const mesh = new THREE.Mesh(geometry, material(part));
      mesh.applyMatrix4(copyMatrix(part, i, count, x, base + h / 2, z));
      mesh.userData.spin = part.spin;
      if (count > 1) tag(mesh, part.repeat?.names?.[i] ?? `${part.name} ${i + 1}`, part.info, part);
      group.add(mesh);
    }
    return tag(count === 1 ? group.children[0] : group, part.name, part.info, part);
  }
  if (count === 1) {
    const mesh = new THREE.Mesh(geometry, part.shape === 'port' ? portMaterials(part) : material(part));
    mesh.position.set(x, base + h / 2, z);
    markChip(mesh, part, h);
    return tag(mesh, part.name, part.info, part);
  }
  const mesh = new THREE.InstancedMesh(geometry, part.shape === 'port' ? portMaterials(part) : material(part), count);
  if (part.scatter) mesh.userData.scatter = true;
  const m = new THREE.Matrix4();
  for (let i = 0; i < count; i++) mesh.setMatrixAt(i, copyMatrix(part, i, count, x, base + h / 2, z, m, avoid));
  mesh.instanceMatrix.needsUpdate = true;
  // Per-copy colour (white = unchanged) so one copy can be highlighted on its own.
  for (let i = 0; i < count; i++) mesh.setColorAt(i, WHITE);
  if (part.repeat?.names) mesh.userData.names = part.repeat.names;
  mesh.computeBoundingBox();
  mesh.computeBoundingSphere();
  if (!part.scatter) markChip(mesh, part, h);
  return tag(mesh, part.name, part.info, part);
}

/**
 * Laser-etched marking on top of a chip: its name (or `marking`) in light grey and a pin-1 dot,
 * as a thin decal just above the top face. Rows get one decal per copy. Browser only.
 */
/** A painted plane just above a box's top face, following every copy of an instanced row. */
function addDecal(mesh: THREE.Mesh, canvas: HTMLCanvasElement, w: number, d: number, h: number, inset: number) {
  const map = new THREE.CanvasTexture(canvas);
  map.colorSpace = THREE.SRGBColorSpace;
  map.anisotropy = 8;
  const decalMat = new THREE.MeshBasicMaterial({ map, transparent: true, depthWrite: false, toneMapped: false });
  const plane = new THREE.PlaneGeometry(w * inset, d * inset).rotateX(-Math.PI / 2);
  plane.translate(0, h / 2 + Math.max(0.002, h * 0.02), 0);
  const inst = mesh as THREE.InstancedMesh;
  const decal = inst.isInstancedMesh
    ? new THREE.InstancedMesh(plane, decalMat, inst.count)
    : new THREE.Mesh(plane, decalMat);
  if (inst.isInstancedMesh) {
    (decal as THREE.InstancedMesh).instanceMatrix.copy(inst.instanceMatrix);
    decal.frustumCulled = false;
  }
  // Part of the part's look, not a part of its own: no picking, no section cap, no shadow.
  decal.userData.sectionCap = true;
  decal.raycast = () => {};
  mesh.add(decal);
}

/**
 * The top of a box part: its silicon pattern if it has one, else (for chips) a laser-etched
 * marking with its name and a pin-1 dot. Browser only.
 */
function markChip(mesh: THREE.Mesh, part: PartSpec, h: number) {
  if (typeof document === 'undefined' || part.shape !== 'box') return;
  const [w, , d] = part.size ?? [1, 1, 1];
  if (part.pattern) {
    const px = Math.min(96, 1024 / Math.max(w, d));
    const canvas = document.createElement('canvas');
    canvas.width = Math.max(16, Math.round(w * px));
    canvas.height = Math.max(16, Math.round(d * px));
    paintPattern(
      canvas.getContext('2d')!,
      part.pattern,
      canvas.width,
      canvas.height,
      part.color,
      part.name.length,
      part.cells,
    );
    return addDecal(mesh, canvas, w, d, h, 1);
  }
  if (part.material !== 'chip' || Math.min(w, d) < 2) return;
  const text = part.marking ?? part.name.replace(/\s*\(.*\)$/, '');
  const px = 48; // pixels per mm, enough for crisp small text
  const canvas = document.createElement('canvas');
  canvas.width = Math.min(1024, Math.round(w * px));
  canvas.height = Math.min(1024, Math.round(d * px));
  const g = canvas.getContext('2d')!;
  const cw = canvas.width;
  const ch = canvas.height;
  g.fillStyle = 'rgba(200, 206, 212, 0.55)';
  g.beginPath();
  g.arc(ch * 0.12, ch * 0.12, Math.min(cw, ch) * 0.05, 0, Math.PI * 2);
  g.fill();
  const lines = text.split(/\s+/).reduce<string[]>((acc, word) => {
    const last = acc.at(-1);
    if (last && (last + ' ' + word).length <= 12) acc[acc.length - 1] = last + ' ' + word;
    else acc.push(word);
    return acc;
  }, []);
  const size = Math.min(ch / (lines.length + 1.5), (cw * 0.85) / (Math.max(...lines.map((l) => l.length)) * 0.62));
  g.font = `${Math.round(size)}px monospace`;
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.fillStyle = 'rgba(200, 206, 212, 0.7)';
  lines.forEach((l, i) => g.fillText(l, cw / 2, ch / 2 + (i - (lines.length - 1) / 2) * size * 1.15));
  addDecal(mesh, canvas, w, d, h, 0.92);
}

/** Every copy of every box part on a layer, as silkscreen outlines for its painted surface. */
export function footprints(layer: LayerSpec): Footprint[] {
  const out: Footprint[] = [];
  for (const p of layer.parts ?? []) {
    if (p.shape === 'file') continue;
    const [pw, , pd] = p.shape === 'box' ? (p.size ?? [1, 1, 1]) : [(p.radius ?? 0.5) * 2, 0, (p.radius ?? 0.5) * 2];
    const [x, z] = p.at ?? [0, 0];
    if (p.scatter) continue; // too small and too many for silkscreen
    const n = copies(p);
    const at = new THREE.Vector3();
    let x0 = Infinity;
    let x1 = -Infinity;
    let z0 = Infinity;
    let z1 = -Infinity;
    for (let i = 0; i < n; i++) {
      at.setFromMatrixPosition(copyMatrix(p, i, n, x, 0, z));
      out.push({ name: p.repeat?.names?.[i] ?? (n > 1 ? '' : p.name), x: at.x, z: at.z, w: pw, d: pd });
      [x0, x1, z0, z1] = [
        Math.min(x0, at.x - pw / 2),
        Math.max(x1, at.x + pw / 2),
        Math.min(z0, at.z - pd / 2),
        Math.max(z1, at.z + pd / 2),
      ];
    }
    // A row also answers to its own name, so a bus can run to the whole row (PCIe fingers, power stages).
    if (n > 1) out.push({ name: p.name, x: (x0 + x1) / 2, z: (z0 + z1) / 2, w: x1 - x0, d: z1 - z0, silk: false });
  }
  return out;
}

/** Put a painted surface on the slab's top face (browser only; tests build without it). */
function paintSlab(slab: THREE.Mesh, layer: LayerSpec, w: number, d: number) {
  if (typeof document === 'undefined') return;
  // About 1500 px across whatever the slab's size, so a 45 mm package is as crisp as a 30 cm board.
  const pxPerMm = Math.min(64, Math.max(4, 1536 / Math.max(w, d)));
  const size = surfaceSize(w, d, pxPerMm);
  const side = slab.material as THREE.MeshPhysicalMaterial;
  /** One face's finish; the underside gets the same finish without the parts' silkscreen. */
  const face = (withParts: boolean, seed: number) => {
    const canvas = document.createElement('canvas');
    canvas.width = size.width;
    canvas.height = size.height;
    paintSurface(canvas.getContext('2d')!, {
      kind: layer.surface!,
      pxPerMm,
      width: w,
      depth: d,
      footprints: withParts ? footprints(layer) : [],
      seed,
      base: layer.color,
    });
    const map = new THREE.CanvasTexture(canvas);
    map.colorSpace = THREE.SRGBColorSpace;
    map.anisotropy = 8;
    const m = side.clone();
    m.map = map;
    m.color.set(0xffffff); // the texture carries the colour
    return m;
  };
  // BoxGeometry faces: +x, -x, +y (top), -y (bottom), +z, -z.
  slab.material = [side, side, face(true, layer.name.length), face(false, layer.name.length + 101), side, side];
}

/** Where copy i of a repeated part goes: along a row, or around a circle facing outward. */
export function copyMatrix(
  part: PartSpec,
  i: number,
  count: number,
  x: number,
  y: number,
  z: number,
  out = new THREE.Matrix4(),
  /** Outlines a scatter keeps clear of. */
  avoid: Footprint[] = [],
) {
  const r = part.repeat;
  if (part.scatter) {
    // Same part, same seed, same layout every time; a spot inside another part's outline is
    // re-drawn (a few tries), so components don't land on top of chips.
    const rng = mulberry32(seedOf(part.name) + i * 7919);
    const [w, d] = part.scatter;
    let px = 0;
    let pz = 0;
    for (let k = 0; k < 12; k++) {
      px = x + (rng() - 0.5) * w;
      pz = z + (rng() - 0.5) * d;
      if (!avoid.some((f) => Math.abs(px - f.x) < f.w / 2 + 1 && Math.abs(pz - f.z) < f.d / 2 + 1)) break;
    }
    out.makeRotationY(rng() < 0.5 ? 0 : Math.PI / 2);
    return out.setPosition(px, y, pz);
  }
  if (r?.grid) {
    const [sx, sz] = r.step ?? [0, 0];
    const cols = Math.max(1, r.grid[0]);
    return out.makeTranslation(x + sx * (i % cols), y, z + sz * Math.floor(i / cols));
  }
  if (r?.around !== undefined) {
    const a = THREE.MathUtils.degToRad(r.startAngle ?? 0) + (i / count) * Math.PI * 2;
    out.makeRotationY(-a);
    return out.setPosition(x + Math.cos(a) * r.around, y, z + Math.sin(a) * r.around);
  }
  const [sx, sz] = r?.step ?? [0, 0];
  return out.makeTranslation(x + sx * i, y, z + sz * i);
}

/** How many copies a part has: grid columns × rows, or repeat.count, at least one. */
export function copies(part: PartSpec) {
  const r = part.repeat;
  const n = r?.grid ? r.grid[0] * r.grid[1] : (r?.count ?? 1);
  return Math.max(1, Math.floor(n));
}

const seedOf = (s: string) => [...s].reduce((h, c) => (h * 31 + c.charCodeAt(0)) >>> 0, 7);

/**
 * A fan or propeller: a hub and curved, pitched blades, centred on the origin, `height` thick.
 * One geometry, so it draws in one call and spins as one piece.
 */
export function fanGeometry(
  radius: number,
  height: number,
  blades: number,
  ring = false,
  blade: 'fan' | 'prop' = 'fan',
) {
  const prop = blade === 'prop';
  // A prop's hub is small and its blades slim: widest a third of the way out, narrow at the tip.
  const hub = radius * (prop ? 0.11 : 0.3);
  const parts: THREE.BufferGeometry[] = [new THREE.CylinderGeometry(hub, hub, height, 32)];
  const sweep = (Math.PI * 2) / blades;
  for (let b = 0; b < blades; b++) {
    // A blade outline in the plane: narrow at the hub, widening toward the tip, swept back.
    const shape = new THREE.Shape();
    const steps = 10;
    const edge = (t: number, side: number) => {
      const r = hub * 0.95 + (radius * (ring ? 0.97 : 1) - hub * 0.95) * t;
      const width = prop
        ? ((0.5 + 0.8 * Math.sin(Math.PI * Math.min(1, t * 1.4 + 0.15))) *
            Math.min(1, (1 - t) * 6 + 0.25) *
            radius *
            0.17) /
          Math.max(r * sweep, 1e-6) // a chord in mm, as a share of the slot at this radius
        : 0.45 + 0.4 * t; // share of the slot the blade covers
      const a = sweep * (0.5 + (side - 0.5) * width) + t * sweep * (prop ? 0.08 : 0.3);
      return new THREE.Vector2(Math.cos(a) * r, Math.sin(a) * r);
    };
    shape.moveTo(edge(0, 0).x, edge(0, 0).y);
    for (let k = 1; k <= steps; k++) shape.lineTo(edge(k / steps, 0).x, edge(k / steps, 0).y);
    for (let k = steps; k >= 0; k--) shape.lineTo(edge(k / steps, 1).x, edge(k / steps, 1).y);
    const blade = new THREE.ExtrudeGeometry(shape, { depth: Math.max(height * 0.1, 0.2), bevelEnabled: false });
    blade.rotateX(-Math.PI / 2); // lie flat: the outline is in x / z
    blade.translate(0, -height * 0.05, 0);
    blade.rotateY(-b * sweep);
    // Pitch: tip each blade about its own radial line.
    const mid = -b * sweep - sweep * 0.65;
    const axis = new THREE.Vector3(Math.cos(mid), 0, -Math.sin(mid));
    blade.applyMatrix4(new THREE.Matrix4().makeRotationAxis(axis, 0.22));
    parts.push(blade.index ? blade.toNonIndexed() : blade);
  }
  if (ring) {
    // A band joining the blade tips.
    const band = new THREE.LatheGeometry(
      [
        new THREE.Vector2(radius * 0.96, -height * 0.25),
        new THREE.Vector2(radius, -height * 0.25),
        new THREE.Vector2(radius, height * 0.25),
        new THREE.Vector2(radius * 0.96, height * 0.25),
        new THREE.Vector2(radius * 0.96, -height * 0.25),
      ],
      64,
    );
    parts.push(band);
  }
  const merged = mergeGeometries(parts.map((g) => (g.index ? g.toNonIndexed() : g)));
  merged.computeVertexNormals();
  return merged;
}

/** Mirror a geometry left to right, keeping its faces facing out. */
export function mirrorX(g: THREE.BufferGeometry) {
  g.scale(-1, 1, 1);
  const index = g.index;
  if (index) {
    for (let i = 0; i < index.count; i += 3) {
      const b = index.getX(i + 1);
      index.setX(i + 1, index.getX(i + 2));
      index.setX(i + 2, b);
    }
  } else {
    for (const attr of Object.values(g.attributes) as THREE.BufferAttribute[]) {
      for (let i = 0; i < attr.count; i += 3) {
        for (let k = 0; k < attr.itemSize; k++) {
          const b = attr.getComponent(i + 1, k);
          attr.setComponent(i + 1, k, attr.getComponent(i + 2, k));
          attr.setComponent(i + 2, k, b);
        }
      }
    }
  }
  g.computeVertexNormals();
  return g;
}

/** A slab with round openings: the footprint rectangle minus circles, extruded to its thickness, centred like a box. */
export function holedSlab(w: number, d: number, t: number, holes: { at: [number, number]; radius: number }[]) {
  const shape = new THREE.Shape();
  shape.moveTo(-w / 2, -d / 2);
  shape.lineTo(w / 2, -d / 2);
  shape.lineTo(w / 2, d / 2);
  shape.lineTo(-w / 2, d / 2);
  shape.closePath();
  for (const h of holes) {
    const hole = new THREE.Path();
    // Shape y becomes -z after laying the extrusion flat, so mirror z here.
    hole.absarc(h.at[0], -h.at[1], h.radius, 0, Math.PI * 2, true);
    shape.holes.push(hole);
  }
  const g = new THREE.ExtrudeGeometry(shape, { depth: t, bevelEnabled: false, curveSegments: 48 });
  g.rotateX(-Math.PI / 2); // extrusion along +y, outline in x / z
  g.translate(0, -t / 2, 0); // centred on its thickness, like BoxGeometry
  return g;
}

/**
 * A connector receptacle, opening toward -x: a metal shell with the port's real opening profile
 * (DisplayPort: one chamfered corner; HDMI: tapered; USB-C: rounded slot), a closed back and the
 * contact tongue inside. Groups: 0 shell, 1 tongue. `w` along x (depth), `h` up, `d` along z.
 */
export function portGeometry(kind: 'dp' | 'hdmi' | 'usbc', w: number, h: number, d: number) {
  const outer = (sh: THREE.Shape) => {
    sh.moveTo(-d / 2, -h / 2);
    sh.lineTo(d / 2, -h / 2);
    sh.lineTo(d / 2, h / 2);
    sh.lineTo(-d / 2, h / 2);
    sh.closePath();
    return sh;
  };
  const t = Math.min(h, d) * 0.12; // shell wall
  const ow = d / 2 - t;
  const oh = h / 2 - t;
  const hole = new THREE.Path();
  if (kind === 'dp') {
    const c = oh * 0.7; // the chamfered corner that keys the plug
    hole.moveTo(-ow + c, -oh);
    hole.lineTo(ow, -oh);
    hole.lineTo(ow, oh);
    hole.lineTo(-ow, oh);
    hole.lineTo(-ow, -oh + c);
  } else if (kind === 'hdmi') {
    const c = oh * 0.55; // both lower corners cut: the familiar tapered outline
    hole.moveTo(-ow + c, -oh);
    hole.lineTo(ow - c, -oh);
    hole.lineTo(ow, -oh + c);
    hole.lineTo(ow, oh);
    hole.lineTo(-ow, oh);
    hole.lineTo(-ow, -oh + c);
  } else {
    hole.absarc(-ow + oh, 0, oh, Math.PI / 2, (Math.PI * 3) / 2, false);
    hole.absarc(ow - oh, 0, oh, -Math.PI / 2, Math.PI / 2, false);
  }
  hole.closePath();
  const front = outer(new THREE.Shape());
  front.holes.push(hole);
  const shell = new THREE.ExtrudeGeometry(front, { depth: w * 0.6, bevelEnabled: false, curveSegments: 12 });
  const back = new THREE.ExtrudeGeometry(outer(new THREE.Shape()), { depth: w * 0.4, bevelEnabled: false });
  back.translate(0, 0, w * 0.6);
  const tongue = new THREE.BoxGeometry(ow * 1.4, oh * 0.45, w * 0.55);
  tongue.translate(0, kind === 'usbc' ? 0 : -oh * 0.1, w * 0.6 - w * 0.275);
  const merged = mergeGeometries(
    [shell, back, tongue].map((g) => (g.index ? g.toNonIndexed() : g)),
    true,
  )!;
  // Shape space (z across, y up, extrusion depth) -> engine space (opening at -x).
  merged.rotateY(Math.PI / 2);
  merged.translate(-w / 2, 0, 0);
  merged.clearGroups();
  // Vertices once unindexed: the index length when there is one, else the position count.
  const count = (g: THREE.BufferGeometry) => g.index?.count ?? g.getAttribute('position').count;
  const n1 = count(shell) + count(back);
  merged.addGroup(0, n1, 0);
  merged.addGroup(n1, Infinity, 1);
  merged.computeVertexNormals();
  return merged;
}

/** A flat outline (frame plate, arm, bracket) with round holes, extruded upward and centred on its thickness. */
export function outlineGeometry(
  points: [number, number][],
  t: number,
  cutouts: { at: [number, number]; radius: number }[],
) {
  const shape = new THREE.Shape();
  // Shape y becomes -z once laid flat, so mirror z here.
  points.forEach(([x, z], i) => (i ? shape.lineTo(x, -z) : shape.moveTo(x, -z)));
  shape.closePath();
  for (const c of cutouts) {
    const hole = new THREE.Path();
    hole.absarc(c.at[0], -c.at[1], c.radius, 0, Math.PI * 2, true);
    shape.holes.push(hole);
  }
  const g = new THREE.ExtrudeGeometry(shape, {
    depth: t,
    bevelEnabled: true,
    bevelThickness: t * 0.08,
    bevelSize: t * 0.08,
    bevelSegments: 2,
    curveSegments: 24,
  });
  g.rotateX(-Math.PI / 2);
  g.translate(0, -t / 2, 0);
  g.computeVertexNormals();
  return g;
}

/** Tiling finishes: extruded outlines get UVs in millimetres, so one tile covers TILE mm. */
const TILE = 24;
const finishes = new Map<string, THREE.Texture>();
function finishTexture(kind: 'carbon' | 'brushed', base?: string) {
  const key = `${kind}:${base ?? ''}`;
  let tex = finishes.get(key);
  if (tex) return tex;
  if (typeof document === 'undefined') return null;
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = 256;
  paintSurface(canvas.getContext('2d')!, { kind, width: TILE, depth: TILE, pxPerMm: 256 / TILE, base });
  tex = new THREE.CanvasTexture(canvas);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.repeat.set(1 / TILE, 1 / TILE);
  tex.anisotropy = 8;
  finishes.set(key, tex);
  return tex;
}
