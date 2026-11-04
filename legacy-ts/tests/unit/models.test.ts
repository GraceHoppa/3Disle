import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { STLExporter } from 'three/examples/jsm/exporters/STLExporter.js';
import { formatOf, parseModel } from '../../src/models/load';
import { fitDistance, frameKeys, measure } from '../../src/models/frame';
import { enableBvh } from '../../src/models/bvh';

const box = (w: number, h: number, d: number) => new THREE.Mesh(new THREE.BoxGeometry(w, h, d));
const size = (o: THREE.Object3D) => new THREE.Box3().setFromObject(o).getSize(new THREE.Vector3());

describe('formatOf', () => {
  it('reads the extension, ignoring case and query strings', () => {
    expect(formatOf('part.STL')).toBe('stl');
    expect(formatOf('/m/robot.glb?v=2')).toBe('glb');
    expect(formatOf('scene.gltf#x')).toBe('gltf');
    expect(formatOf('notes.txt')).toBeNull();
    expect(formatOf('noext')).toBeNull();
  });
});

describe('parseModel: STL', () => {
  // A 2 x 4 x 6 box written Z-up, as a slicer would: height 6 is along Z.
  const zUp = box(2, 4, 6);

  it('parses binary STL and turns it Y-up', async () => {
    const view = new STLExporter().parse(zUp, { binary: true }) as DataView;
    const model = await parseModel(view.buffer as ArrayBuffer, 'stl', 'cube');
    const s = size(model);
    expect([s.x, s.y, s.z].map((v) => +v.toFixed(4))).toEqual([2, 6, 4]);
    expect(model.name).toBe('cube');
  });

  it('parses ASCII STL', async () => {
    const text = new STLExporter().parse(zUp) as string;
    const model = await parseModel(new TextEncoder().encode(text).buffer as ArrayBuffer, 'stl');
    let triangles = 0;
    model.traverse((o) => {
      if ((o as THREE.Mesh).isMesh) triangles += (o as THREE.Mesh).geometry.getAttribute('position').count / 3;
    });
    expect(triangles).toBe(12);
  });
});

describe('parseModel: GLB', () => {
  /** Pack a glTF JSON and its binary buffer into a .glb (12-byte header, JSON chunk, BIN chunk). */
  const glb = (json: object, bin: Uint8Array) => {
    const pad = (b: Uint8Array, fill: number) => {
      const out = new Uint8Array(Math.ceil(b.length / 4) * 4).fill(fill);
      out.set(b);
      return out;
    };
    const j = pad(new TextEncoder().encode(JSON.stringify(json)), 0x20);
    const b = pad(bin, 0);
    const out = new DataView(new ArrayBuffer(12 + 8 + j.length + 8 + b.length));
    out.setUint32(0, 0x46546c67, true);
    out.setUint32(4, 2, true);
    out.setUint32(8, out.byteLength, true);
    out.setUint32(12, j.length, true);
    out.setUint32(16, 0x4e4f534a, true);
    new Uint8Array(out.buffer, 20).set(j);
    out.setUint32(20 + j.length, b.length, true);
    out.setUint32(24 + j.length, 0x004e4942, true);
    new Uint8Array(out.buffer, 28 + j.length).set(b);
    return out.buffer;
  };

  it('keeps the node tree and names', async () => {
    const positions = new Float32Array([0, 0, 0, 1, 0, 0, 0, 1, 0]);
    const json = {
      asset: { version: '2.0' },
      scene: 0,
      scenes: [{ nodes: [0] }],
      nodes: [{ name: 'lid', mesh: 0 }],
      meshes: [{ primitives: [{ attributes: { POSITION: 0 } }] }],
      buffers: [{ byteLength: 36 }],
      bufferViews: [{ buffer: 0, byteLength: 36 }],
      accessors: [{ bufferView: 0, componentType: 5126, count: 3, type: 'VEC3', min: [0, 0, 0], max: [1, 1, 0] }],
    };
    const model = await parseModel(glb(json, new Uint8Array(positions.buffer)), 'glb');
    expect(model.getObjectByName('lid')).toBeTruthy();
  });
});

describe('frameKeys', () => {
  it('keeps the whole model in view on the opening keys, all aimed at the centre', () => {
    const root = new THREE.Group().add(box(10, 2, 3));
    root.position.set(50, -7, 3);
    const f = measure(root);
    const keys = frameKeys(f, 40);
    expect(keys.map((k) => k.t)).toEqual([0, 0.5, 1]);
    for (const k of keys) expect(k.look).toEqual([f.center.x, f.center.y, f.center.z]);
    for (const k of keys.slice(0, 2)) {
      const dist = new THREE.Vector3(...k.pos).distanceTo(f.center);
      expect(dist).toBeGreaterThanOrEqual(fitDistance(f.radius, 40));
    }
  });

  it('refuses an empty model', () => {
    expect(() => measure(new THREE.Group())).toThrow();
  });
});

describe('enableBvh', () => {
  it('gives the same hit as a brute-force raycast', () => {
    const plain = new THREE.Mesh(new THREE.TorusKnotGeometry(1, 0.3, 200, 32));
    const fast = new THREE.Mesh(plain.geometry.clone());
    expect(enableBvh(new THREE.Group().add(fast))).toBe(1);
    const ray = new THREE.Raycaster(new THREE.Vector3(0.3, 0.2, 5), new THREE.Vector3(0, 0, -1));
    const a = ray.intersectObject(plain)[0];
    const b = ray.intersectObject(fast)[0];
    // The tree reorders triangles, so compare where the ray hits rather than face numbers.
    expect(b.distance).toBeCloseTo(a.distance, 6);
    expect(b.point.distanceTo(a.point)).toBeLessThan(1e-6);
  });
});
