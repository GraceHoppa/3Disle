import * as THREE from 'three';
import type { Value } from './logic';

/**
 * Real wires in 3D between any two named parts of an assembly, on any layers: a tube from the
 * side of one part facing the other, through optional waypoints, to the other. What flows in it
 * is drawn on the insulation: current as streaks that run faster and brighter with `level`,
 * motor phases as three staggered waves, data as packets, analog signals, video and RF as a fine
 * fast signal.
 */
export type WireKind = 'power' | 'ground' | 'phase' | 'data' | 'signal' | 'video' | 'rf';

export interface WireSpec {
  name: string;
  /** Part, layer or copy names (M1, Motor 2 …); the wire leaves the side facing its next point. */
  from: string;
  to: string;
  kind?: WireKind;
  /** phase: which of the three motor phases (0, 1, 2); their waves are a third of a turn apart. */
  phase?: number;
  /** Waypoints [x, y, z] in the assembled model, mm. Without them the wire sags a little. */
  via?: [number, number, number][];
  /** Outer diameter, mm (default 1). */
  gauge?: number;
  /** Insulation colour (default by kind). */
  color?: string;
  /** How much flows, 0..1: a number or a rule on a control (default 1). */
  level?: Value;
  info?: string;
  category?: string;
  specs?: Record<string, string>;
}

/** Per kind: insulation colour, the colour of what flows, the shader's mode and the inspector's word. */
export const WIRE_KINDS: Record<WireKind, { color: string; glow: string; mode: number; label: string }> = {
  power: { color: '#a3201a', glow: '#ffae42', mode: 0, label: 'Power' },
  ground: { color: '#141619', glow: '#4f9dff', mode: 0, label: 'Ground return' },
  phase: { color: '#1b1d20', glow: '#ff7a2f', mode: 1, label: 'Motor phase' },
  data: { color: '#2a2e33', glow: '#9cff3a', mode: 2, label: 'Data' },
  signal: { color: '#2a2e33', glow: '#5fe0ff', mode: 3, label: 'Analog signal' },
  video: { color: '#c9a91c', glow: '#ffe066', mode: 3, label: 'Video' },
  rf: { color: '#1e2024', glow: '#c48cff', mode: 3, label: 'RF' },
};

/** Where a name is in the model: the object, and the box of the part (or one copy) in its local space. */
export function locate(root: THREE.Object3D, name: string) {
  let found: { object: THREE.Object3D; box: THREE.Box3 } | null = null;
  root.traverse((o) => {
    if (found || o.userData.wire) return;
    const names = o.userData.names as string[] | undefined;
    const copy = names?.indexOf(name) ?? -1;
    const inst = o as THREE.InstancedMesh;
    if (copy >= 0 && inst.isInstancedMesh) {
      inst.geometry.computeBoundingBox();
      const m = new THREE.Matrix4();
      inst.getMatrixAt(copy, m);
      found = { object: o, box: inst.geometry.boundingBox!.clone().applyMatrix4(m) };
    } else if (o.name === name && o !== root) {
      o.updateWorldMatrix(true, true);
      const world = new THREE.Box3().setFromObject(o, true);
      const toLocal = o.matrixWorld.clone().invert();
      found = { object: o, box: world.applyMatrix4(toLocal) };
    }
  });
  return found as { object: THREE.Object3D; box: THREE.Box3 } | null;
}

interface Anchor {
  object: THREE.Object3D;
  /** The wire's end in the object's local space. */
  local: THREE.Vector3;
}

/** Built wire's moving parts, kept on the mesh for `refreshWires`. */
interface WireState {
  from: Anchor;
  to: Anchor;
  /** Assembled positions (root space) of both ends and every inner point. */
  a0: THREE.Vector3;
  b0: THREE.Vector3;
  inner: { at: THREE.Vector3; t: number }[];
  segments: number;
  radius: number;
  last: string;
}

const toRoot = (root: THREE.Object3D, a: Anchor, out = new THREE.Vector3()) => {
  a.object.updateWorldMatrix(true, false);
  root.updateWorldMatrix(true, false);
  out.copy(a.local).applyMatrix4(a.object.matrixWorld);
  return root.worldToLocal(out);
};

/** The point of a box (in its object's space) nearest to a root-space point, as an anchor. */
function anchorToward(root: THREE.Object3D, at: { object: THREE.Object3D; box: THREE.Box3 }, toward: THREE.Vector3) {
  at.object.updateWorldMatrix(true, false);
  const local = at.object.worldToLocal(root.localToWorld(toward.clone()));
  return { object: at.object, local: at.box.clampPoint(local, new THREE.Vector3()) };
}

/** The inner points of a wire: its waypoints, or one sagging midpoint when it has none. */
function innerPoints(a: THREE.Vector3, b: THREE.Vector3, via?: [number, number, number][]) {
  const pts = via?.length
    ? via.map((v) => new THREE.Vector3(...v))
    : [
        a
          .clone()
          .lerp(b, 0.5)
          .add(new THREE.Vector3(0, -0.06 * Math.hypot(b.x - a.x, b.z - a.z), 0)),
      ];
  const all = [a, ...pts, b];
  const lengths = [0];
  for (let i = 1; i < all.length; i++) lengths.push(lengths[i - 1] + all[i].distanceTo(all[i - 1]));
  const total = lengths.at(-1) || 1;
  return pts.map((at, i) => ({ at, t: lengths[i + 1] / total }));
}

function curveOf(points: THREE.Vector3[]) {
  return new THREE.CatmullRomCurve3(points, false, 'centripetal', 0.5);
}

/** Uniforms every wire shares with the viewer's clock, and its own. */
export interface WireUniforms {
  uTime: { value: number };
  uSpeed: { value: number };
  uLevel: { value: number };
  uMode: { value: number };
  uPhase: { value: number };
  uLength: { value: number };
  uGlow: { value: THREE.Color };
}

/**
 * Lit insulation with what flows drawn on it as light. `along` is the distance from the start
 * (0..1); the patterns run in millimetres, so a long wire and a short one move alike.
 */
function wireMaterial(spec: WireSpec, length: number) {
  const kind = WIRE_KINDS[spec.kind ?? 'data'];
  const uniforms: WireUniforms = {
    uTime: { value: 0 },
    uSpeed: { value: 1 },
    uLevel: { value: 1 },
    uMode: { value: kind.mode },
    uPhase: { value: spec.phase ?? 0 },
    uLength: { value: length },
    uGlow: { value: new THREE.Color(kind.glow) },
  };
  const m = new THREE.MeshPhysicalMaterial({
    color: spec.color ?? kind.color,
    roughness: 0.42,
    metalness: 0,
    clearcoat: 0.3,
  });
  m.onBeforeCompile = (shader) => {
    Object.assign(shader.uniforms, uniforms);
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', '#include <common>\nattribute float along;\nvarying float vAlong;')
      .replace('#include <begin_vertex>', '#include <begin_vertex>\nvAlong = along;');
    shader.fragmentShader = shader.fragmentShader
      .replace(
        '#include <common>',
        `#include <common>
        uniform float uTime, uSpeed, uLevel, uMode, uPhase, uLength;
        uniform vec3 uGlow;
        varying float vAlong;`,
      )
      .replace(
        '#include <emissivemap_fragment>',
        `#include <emissivemap_fragment>
        {
          float s = vAlong * uLength;
          float t = uTime * uSpeed;
          float lit = 0.0;
          if (uMode < 0.5) {
            // Current: streaks, faster and brighter the more flows.
            float p = fract((s - t * (25.0 + 160.0 * uLevel)) / 10.0);
            lit = uLevel * (0.3 + 1.1 * smoothstep(0.0, 0.12, p) * (1.0 - smoothstep(0.12, 0.55, p)));
          } else if (uMode < 1.5) {
            // A motor phase: the drive swings + and -, a third of a turn after the previous phase.
            float w = sin(t * 6.2832 * (0.5 + 3.0 * uLevel) + uPhase * 2.0944);
            float p = fract((s - sign(w) * t * 70.0) / 8.0);
            lit = uLevel * abs(w) * (0.35 + 0.9 * smoothstep(0.0, 0.2, p) * (1.0 - smoothstep(0.2, 0.6, p)));
          } else if (uMode < 2.5) {
            // Data: packets of bits.
            float x = s - t * 90.0;
            float p = fract(x / 16.0);
            float bit = step(0.45, fract(sin(floor(x / 1.6) * 12.9898) * 43758.5453));
            lit = uLevel * (0.12 + 1.5 * step(p, 0.4) * (0.35 + 0.65 * bit));
          } else {
            // Video / RF: a continuous fast signal.
            float p = fract((s - t * 160.0) / 3.0);
            lit = uLevel * (0.3 + 0.6 * smoothstep(0.0, 0.5, p) * (1.0 - smoothstep(0.5, 1.0, p)));
          }
          totalEmissiveRadiance += uGlow * mix(0.3 * uLevel, lit, step(0.001, uSpeed)) * 1.4;
        }`,
      );
  };
  m.customProgramCacheKey = () => 'wire';
  m.userData.uniforms = uniforms;
  return m;
}

/** A tube along the points, with how far along each vertex is (0..1) as `along`. */
function tube(points: THREE.Vector3[], segments: number, radius: number) {
  const g = new THREE.TubeGeometry(curveOf(points), segments, radius, 8, false);
  const uv = g.getAttribute('uv');
  const along = new Float32Array(uv.count);
  for (let i = 0; i < uv.count; i++) along[i] = uv.getX(i);
  g.setAttribute('along', new THREE.BufferAttribute(along, 1));
  return g;
}

/**
 * Build the wires of an assembly as one group (not a layer: Disassemble leaves it alone, and
 * `refreshWires` stretches each wire to follow its ends). Names that aren't found are reported.
 */
export function buildWires(root: THREE.Object3D, specs: WireSpec[]) {
  const group = new THREE.Group();
  group.name = 'Wiring';
  group.userData.wires = true;
  group.userData.info = 'Every wire between the parts, drawn with what flows in it.';
  const missing: string[] = [];
  root.updateWorldMatrix(true, true);
  for (const spec of specs) {
    const from = locate(root, spec.from);
    const to = locate(root, spec.to);
    if (!from || !to) {
      missing.push(...[!from && spec.from, !to && spec.to].filter((n): n is string => !!n));
      continue;
    }
    const centre = (at: { object: THREE.Object3D; box: THREE.Box3 }) =>
      root.worldToLocal(at.box.getCenter(new THREE.Vector3()).applyMatrix4(at.object.matrixWorld));
    const firstVia = spec.via?.[0] ? new THREE.Vector3(...spec.via[0]) : centre(to);
    const lastVia = spec.via?.length ? new THREE.Vector3(...spec.via[spec.via.length - 1]) : centre(from);
    const a = anchorToward(root, from, firstVia);
    const b = anchorToward(root, to, lastVia);
    const a0 = toRoot(root, a);
    const b0 = toRoot(root, b);
    const inner = innerPoints(a0, b0, spec.via);
    const points = [a0, ...inner.map((p) => p.at), b0];
    const length = curveOf(points).getLength();
    const segments = Math.min(240, Math.max(24, Math.round(length / 1.2)));
    const radius = (spec.gauge ?? 1) / 2;
    const kind = WIRE_KINDS[spec.kind ?? 'data'];
    const mesh = new THREE.Mesh(tube(points, segments, radius), wireMaterial(spec, length));
    mesh.name = spec.name;
    mesh.userData.info = spec.info ?? '';
    mesh.userData.category = spec.category ?? `Wire · ${kind.label}`;
    if (spec.specs) mesh.userData.specs = spec.specs;
    mesh.userData.wire = true;
    mesh.userData.level = spec.level ?? 1;
    const state: WireState = { from: a, to: b, a0, b0, inner, segments, radius, last: '' };
    mesh.userData.wireState = state;
    group.add(mesh);
  }
  return { group, missing };
}

/** Re-lay every wire whose ends moved (Disassemble, making room): inner points follow both ends. */
export function refreshWires(root: THREE.Object3D) {
  const a = new THREE.Vector3();
  const b = new THREE.Vector3();
  const da = new THREE.Vector3();
  const db = new THREE.Vector3();
  root.traverse((o) => {
    const s = o.userData.wireState as WireState | undefined;
    if (!s) return;
    toRoot(root, s.from, a);
    toRoot(root, s.to, b);
    const key = [a.x, a.y, a.z, b.x, b.y, b.z].map((v) => v.toFixed(3)).join();
    if (key === s.last) return;
    s.last = key;
    da.subVectors(a, s.a0);
    db.subVectors(b, s.b0);
    const points = [a.clone(), ...s.inner.map((p) => p.at.clone().add(da.clone().lerp(db, p.t))), b.clone()];
    // The wire mesh sits in the root's space (its group is never moved), so root space is its space.
    const mesh = o as THREE.Mesh;
    const next = tube(points, s.segments, s.radius);
    const pos = mesh.geometry.getAttribute('position') as THREE.BufferAttribute;
    const nor = mesh.geometry.getAttribute('normal') as THREE.BufferAttribute;
    (pos.array as Float32Array).set(next.getAttribute('position').array as Float32Array);
    (nor.array as Float32Array).set(next.getAttribute('normal').array as Float32Array);
    pos.needsUpdate = true;
    nor.needsUpdate = true;
    next.dispose();
    mesh.geometry.computeBoundingBox();
    mesh.geometry.computeBoundingSphere();
    (mesh.geometry as { boundsTree?: { refit(): void } }).boundsTree?.refit();
  });
}
