import * as THREE from 'three';
import { route45 } from '../lib/canvas';
import type { Footprint } from './surfaces';
import type { LinkSpec } from './assembly';

/** One routed trace: points in the slab's plane (x, z), mm. */
export type Path = [number, number][];

/** Where a straight line from a box's centre toward a point leaves the box. */
function edgeToward(fp: Footprint, tx: number, tz: number): [number, number] {
  const dx = tx - fp.x;
  const dz = tz - fp.z;
  const k = Math.min(dx ? fp.w / 2 / Math.abs(dx) : Infinity, dz ? fp.d / 2 / Math.abs(dz) : Infinity);
  return Number.isFinite(k) ? [fp.x + dx * Math.min(k, 1), fp.z + dz * Math.min(k, 1)] : [fp.x, fp.z];
}

/**
 * The traces of a bus: for each target, `lanes` parallel 45° routes from the edge of `from`
 * facing the target to the edge of the target facing back. Unknown names are skipped.
 */
export function linkPaths(link: LinkSpec, prints: Footprint[]): Path[] {
  const byName = new Map(prints.filter((p) => p.name).map((p) => [p.name, p]));
  const src = byName.get(link.from);
  if (!src) return [];
  const lanes = Math.max(1, link.lanes ?? 6);
  const gap = (link.width ?? 0.4) * 2.2;
  const out: Path[] = [];
  for (const name of Array.isArray(link.to) ? link.to : [link.to]) {
    const dst = byName.get(name);
    if (!dst) continue;
    const a = edgeToward(src, dst.x, dst.z);
    const b = edgeToward(dst, src.x, src.z);
    // Spread lanes across the direction of travel.
    const len = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
    const nx = -(b[1] - a[1]) / len;
    const nz = (b[0] - a[0]) / len;
    for (let l = 0; l < lanes; l++) {
      const o = (l - (lanes - 1) / 2) * gap;
      out.push(route45(a[0] + nx * o, a[1] + nz * o, b[0] + nx * o, b[1] + nz * o, 0.5) as Path);
    }
  }
  return out;
}

/** Flat ribbons along the paths, with how far along its trace each vertex is (0..1) and a per-lane offset. */
export function ribbonGeometry(paths: Path[], width: number, y: number) {
  const pos: number[] = [];
  const along: number[] = [];
  const lane: number[] = [];
  const index: number[] = [];
  paths.forEach((path, p) => {
    const lengths = [0];
    for (let i = 1; i < path.length; i++)
      lengths.push(lengths[i - 1] + Math.hypot(path[i][0] - path[i - 1][0], path[i][1] - path[i - 1][1]));
    const total = lengths[lengths.length - 1] || 1;
    const offset = (p * 0.618) % 1;
    for (let i = 0; i < path.length - 1; i++) {
      const [ax, az] = path[i];
      const [bx, bz] = path[i + 1];
      const len = Math.hypot(bx - ax, bz - az);
      if (!len) continue;
      const nx = (-(bz - az) / len) * (width / 2);
      const nz = ((bx - ax) / len) * (width / 2);
      const v = pos.length / 3;
      pos.push(ax + nx, y, az + nz, ax - nx, y, az - nz, bx + nx, y, bz + nz, bx - nx, y, bz - nz);
      const ta = lengths[i] / total;
      const tb = lengths[i + 1] / total;
      along.push(ta, ta, tb, tb);
      lane.push(offset, offset, offset, offset);
      index.push(v, v + 2, v + 1, v + 1, v + 2, v + 3);
    }
  });
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('along', new THREE.Float32BufferAttribute(along, 1));
  g.setAttribute('lane', new THREE.Float32BufferAttribute(lane, 1));
  g.setIndex(index);
  g.computeBoundingBox();
  g.computeBoundingSphere();
  return g;
}

/**
 * Faintly lit traces with bright pulses running from -> to, like the descent's memory bus.
 * `uTime` advances in the viewer; `uSpeed` 0 stops the pulses but keeps the traces lit.
 */
export function flowMaterial(color = '#76b900') {
  return new THREE.ShaderMaterial({
    uniforms: {
      uTime: { value: 0 },
      uSpeed: { value: 1 },
      uColor: { value: new THREE.Color(color) },
    },
    side: THREE.DoubleSide,
    vertexShader: /* glsl */ `
      attribute float along;
      attribute float lane;
      varying float vAlong;
      varying float vLane;
      void main() {
        vAlong = along;
        vLane = lane;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }
    `,
    fragmentShader: /* glsl */ `
      uniform float uTime;
      uniform float uSpeed;
      uniform vec3 uColor;
      varying float vAlong;
      varying float vLane;
      void main() {
        float p = fract(vAlong * 2.0 - uTime * uSpeed * 0.6 + vLane);
        float pulse = smoothstep(0.0, 0.03, p) * (1.0 - smoothstep(0.03, 0.16, p));
        gl_FragColor = vec4(uColor * (0.35 + pulse * 3.5 * step(0.001, uSpeed)), 1.0);
      }
    `,
  });
}

/** The bus as one mesh, or null if none of its names were found on the layer. */
export function buildLink(link: LinkSpec, prints: Footprint[], y: number) {
  const paths = linkPaths(link, prints);
  if (!paths.length) return null;
  const mesh = new THREE.Mesh(ribbonGeometry(paths, link.width ?? 0.4, y), flowMaterial(link.color));
  mesh.userData.flow = true;
  return mesh;
}
