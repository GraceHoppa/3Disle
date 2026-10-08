import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { buildAssembly, type AssemblySpec } from '../../src/models/assembly';

const bounds = (o: THREE.Object3D) => {
  o.updateWorldMatrix(true, true);
  return new THREE.Box3().setFromObject(o, true);
};

const card: AssemblySpec = {
  name: 'card',
  footprint: [100, 40],
  layers: [
    { name: 'board', thickness: 1.6, color: '#1b5e20', info: 'FR-4' },
    {
      name: 'chips',
      thickness: 0,
      parts: [
        { name: 'cpu', shape: 'box', size: [20, 2, 20], info: 'main chip' },
        { name: 'caps', shape: 'cylinder', radius: 1, height: 3, at: [-40, -15], repeat: { count: 5, step: [5, 0] } },
      ],
    },
    { name: 'lid', thickness: 2 },
  ],
};

describe('buildAssembly', () => {
  it('makes one direct child per layer, in order', async () => {
    const root = await buildAssembly(card);
    expect(root.children.map((c) => c.name)).toEqual(['board', 'chips', 'lid']);
  });

  it('stacks layers: each starts above the tallest thing on the one below', async () => {
    const root = await buildAssembly(card);
    // board slab 1.6; chips layer has no slab but its tallest part (caps) is 3.
    const ys = root.children.map((c) => c.position.y);
    expect(ys[0]).toBe(0);
    expect(ys[1]).toBeCloseTo(1.6);
    expect(ys[2]).toBeCloseTo(4.6);
    const lid = bounds(root.getObjectByName('lid')!);
    expect(lid.min.y).toBeCloseTo(4.6);
    expect(lid.max.y).toBeCloseTo(6.6);
  });

  it('never lets a layer overlap the parts below it', async () => {
    const root = await buildAssembly(card);
    for (let i = 1; i < root.children.length; i++) {
      expect(bounds(root.children[i]).min.y).toBeGreaterThanOrEqual(bounds(root.children[i - 1]).max.y - 1e-9);
    }
  });

  it('sits parts on top of their slab and keeps their info for inspect', async () => {
    const root = await buildAssembly(card);
    const cpu = root.getObjectByName('cpu')!;
    const b = bounds(cpu);
    expect(b.min.y).toBeCloseTo(1.6);
    expect(b.max.y).toBeCloseTo(3.6);
    expect(cpu.userData.info).toBe('main chip');
    expect(root.getObjectByName('board')!.userData.info).toBe('FR-4');
  });

  it('draws repeated parts as one instanced mesh spaced by the step', async () => {
    const root = await buildAssembly(card);
    const caps = root.getObjectByName('caps') as THREE.InstancedMesh;
    expect(caps.isInstancedMesh).toBe(true);
    expect(caps.count).toBe(5);
    const b = bounds(caps);
    expect(b.min.x).toBeCloseTo(-41);
    expect(b.max.x).toBeCloseTo(-19);
  });

  it('loads file parts through the loader and seats them on the slab', async () => {
    const spec: AssemblySpec = {
      name: 'x',
      footprint: [10, 10],
      layers: [{ name: 'base', thickness: 1, parts: [{ name: 'knob', shape: 'file', file: 'knob.stl', at: [2, 3] }] }],
    };
    const loaded: string[] = [];
    const root = await buildAssembly(spec, async (url) => {
      loaded.push(url);
      const m = new THREE.Mesh(new THREE.BoxGeometry(2, 4, 2));
      m.position.set(50, -20, 7); // arbitrary origin, as real files have
      return m;
    });
    expect(loaded).toEqual(['knob.stl']);
    const b = bounds(root.getObjectByName('knob')!);
    expect(b.min.y).toBeCloseTo(1);
    expect(b.getCenter(new THREE.Vector3()).x).toBeCloseTo(2);
    expect(b.getCenter(new THREE.Vector3()).z).toBeCloseTo(3);
  });

  it('refuses a file part without a loader', async () => {
    const spec: AssemblySpec = {
      name: 'x',
      footprint: [1, 1],
      layers: [{ name: 'a', thickness: 1, parts: [{ name: 'p', shape: 'file', file: 'p.stl' }] }],
    };
    await expect(buildAssembly(spec)).rejects.toThrow(/loader/);
  });
});

describe('RTX 5070 Ti card', () => {
  it('builds from public/models/rtx5070ti/card.json', async () => {
    const { readFileSync } = await import('node:fs');
    const spec = JSON.parse(readFileSync('public/models/rtx5070ti/card.json', 'utf8')) as AssemblySpec;
    const root = await buildAssembly(spec);
    expect(root.children).toHaveLength(spec.layers.length);
    const size = bounds(root).getSize(new THREE.Vector3());
    expect(size.x).toBeGreaterThan(290); // 303 mm card
    expect(root.getObjectByName('GDDR7 memory bus')!.userData.flow).toBe(true);
    const chips = spec.layers.flatMap((l) => l.parts ?? []).filter((p) => p.name.startsWith('GDDR7 memory'));
    expect(chips.reduce((n, p) => n + (p.repeat?.names?.length ?? 1), 0)).toBe(8); // 8 × 2 GB = 16 GB
  });
});

describe('lift', () => {
  it('raises a part above the slab so it can sit on another part', async () => {
    const root = await buildAssembly({
      name: 'x',
      footprint: [10, 10],
      layers: [{ name: 'a', thickness: 1, parts: [{ name: 'die', shape: 'box', size: [2, 0.5, 2], lift: 1.2 }] }],
    });
    expect(bounds(root.getObjectByName('die')!).min.y).toBeCloseTo(2.2);
  });
});

describe('circular repeat', () => {
  it('puts copies on a circle around `at`, facing outward', async () => {
    const root = await buildAssembly({
      name: 'motor',
      footprint: [30, 30],
      layers: [
        {
          name: 'bell',
          thickness: 1,
          parts: [{ name: 'Magnets', shape: 'box', size: [1, 4, 3], at: [5, 0], repeat: { count: 4, around: 10 } }],
        },
      ],
    });
    const mags = root.getObjectByName('Magnets') as THREE.InstancedMesh;
    const m = new THREE.Matrix4();
    const p = new THREE.Vector3();
    const centres = [0, 1, 2, 3].map((i) => {
      mags.getMatrixAt(i, m);
      p.setFromMatrixPosition(m);
      return [+p.x.toFixed(4), +p.z.toFixed(4)];
    });
    expect(centres).toEqual([
      [15, 0],
      [5, 10],
      [-5, 0],
      [5, -10],
    ]);
    // Copy 1 sits at +z, so its local x (the thin side) must point along +z: facing outward.
    mags.getMatrixAt(1, m);
    const x = new THREE.Vector3(1, 0, 0).transformDirection(m);
    expect(x.z).toBeCloseTo(1);
  });
});

describe('fans, grids, scatter, spin', () => {
  const build = (parts: AssemblySpec['layers'][0]['parts']) =>
    buildAssembly({ name: 'x', footprint: [100, 100], layers: [{ name: 'base', thickness: 1, parts }] });
  const centres = (m: THREE.InstancedMesh) =>
    Array.from({ length: m.count }, (_, i) => {
      const mat = new THREE.Matrix4();
      m.getMatrixAt(i, mat);
      return new THREE.Vector3().setFromMatrixPosition(mat);
    });

  it('a fan is a hub with blades reaching the rim, as one geometry', async () => {
    const root = await build([{ name: 'Fan', shape: 'fan', radius: 40, height: 4, blades: 7 }]);
    const fan = root.getObjectByName('Fan') as THREE.Mesh;
    fan.geometry.computeBoundingBox();
    const size = fan.geometry.boundingBox!.getSize(new THREE.Vector3());
    expect(size.x).toBeGreaterThan(70);
    expect(size.x).toBeLessThanOrEqual(82);
    expect(fan.geometry.getAttribute('position').count).toBeGreaterThan(100);
  });

  it('a grid lays out columns × rows', async () => {
    const root = await build([
      {
        name: 'Balls',
        shape: 'cylinder',
        radius: 0.3,
        height: 0.5,
        at: [-10, -5],
        repeat: { count: 0, grid: [3, 2], step: [10, 5] },
      },
    ]);
    const balls = root.getObjectByName('Balls') as THREE.InstancedMesh;
    expect(balls.count).toBe(6);
    expect(centres(balls).map((p) => [p.x, p.z])).toEqual([
      [-10, -5],
      [0, -5],
      [10, -5],
      [-10, 0],
      [0, 0],
      [10, 0],
    ]);
  });

  it('scatter stays inside its area and is the same every time', async () => {
    const spec = [
      {
        name: 'Caps',
        shape: 'box' as const,
        size: [1, 0.5, 0.5] as [number, number, number],
        at: [10, 0] as [number, number],
        scatter: [20, 8] as [number, number],
        repeat: { count: 200 },
      },
    ];
    const a = centres((await build(spec)).getObjectByName('Caps') as THREE.InstancedMesh);
    const b = centres((await build(spec)).getObjectByName('Caps') as THREE.InstancedMesh);
    expect(a).toHaveLength(200);
    for (const p of a) {
      expect(Math.abs(p.x - 10)).toBeLessThanOrEqual(10);
      expect(Math.abs(p.z)).toBeLessThanOrEqual(4);
    }
    expect(a.map((p) => p.toArray())).toEqual(b.map((p) => p.toArray()));
    expect(new Set(a.map((p) => p.x.toFixed(3))).size).toBeGreaterThan(150);
  });

  it('spinning parts are separate meshes that carry their speed', async () => {
    const root = await build([
      {
        name: 'Fans',
        shape: 'fan',
        radius: 10,
        height: 2,
        spin: 600,
        at: [-20, 0],
        repeat: { count: 2, step: [40, 0], names: ['Fan 1', 'Fan 2'] },
      },
    ]);
    const fans = root.getObjectByName('Fans')!;
    expect(fans.children.map((c) => [c.name, c.userData.spin])).toEqual([
      ['Fan 1', 600],
      ['Fan 2', 600],
    ]);
    expect(fans.children[1].position.x).toBeCloseTo(20);
  });
});

describe('scatter keeps clear of other parts', () => {
  it('no copy lands inside a chip outline on the same layer', async () => {
    const root = await buildAssembly({
      name: 'b',
      footprint: [60, 60],
      layers: [
        {
          name: 'board',
          thickness: 1,
          parts: [
            { name: 'Chip', shape: 'box', size: [20, 1, 20] },
            { name: 'Caps', shape: 'box', size: [1, 0.5, 0.5], scatter: [50, 50], repeat: { count: 300 } },
          ],
        },
      ],
    });
    const caps = root.getObjectByName('Caps') as THREE.InstancedMesh;
    const m = new THREE.Matrix4();
    const p = new THREE.Vector3();
    let inside = 0;
    for (let i = 0; i < caps.count; i++) {
      caps.getMatrixAt(i, m);
      p.setFromMatrixPosition(m);
      if (Math.abs(p.x) < 10 && Math.abs(p.z) < 10) inside++;
    }
    expect(inside).toBe(0);
  });
});

describe('holes', () => {
  it('cuts round openings through a slab where asked', async () => {
    const root = await buildAssembly({
      name: 'shroud',
      footprint: [100, 40],
      layers: [{ name: 'plate', thickness: 2, holes: [{ at: [-25, 0], radius: 15 }] }],
    });
    const plate = root.getObjectByName('plate')!;
    const ray = new THREE.Raycaster(new THREE.Vector3(-25, 10, 0), new THREE.Vector3(0, -1, 0));
    expect(ray.intersectObject(plate, true)).toHaveLength(0); // straight through the opening
    ray.set(new THREE.Vector3(25, 10, 0), new THREE.Vector3(0, -1, 0));
    expect(ray.intersectObject(plate, true)[0].point.y).toBeCloseTo(2); // solid elsewhere, top at 2
  });
});

describe('ports', () => {
  it('a DisplayPort is a shell with an opening on its -x face and a tongue inside', async () => {
    const root = await buildAssembly({
      name: 'io',
      footprint: [40, 40],
      layers: [{ name: 'board', thickness: 1, parts: [{ name: 'DP', shape: 'port', port: 'dp', size: [9, 6, 16] }] }],
    });
    const dp = root.getObjectByName('DP') as THREE.Mesh;
    expect(Array.isArray(dp.material) && dp.material.length).toBe(2);
    const b = bounds(dp);
    expect(b.max.x - b.min.x).toBeCloseTo(9);
    expect(b.max.z - b.min.z).toBeCloseTo(16);
    // A ray into the middle of the opening goes past the front face and hits the tongue or back, deeper in.
    const ray = new THREE.Raycaster(new THREE.Vector3(-20, 4, 4), new THREE.Vector3(1, 0, 0));
    const hit = ray.intersectObject(dp)[0];
    expect(hit.point.x).toBeGreaterThan(-4.4);
    // Through the solid shell wall near the top edge, the hit is right at the front face.
    ray.set(new THREE.Vector3(-20, 1 + 5.85, 0), new THREE.Vector3(1, 0, 0));
    expect(ray.intersectObject(dp)[0].point.x).toBeCloseTo(-4.5, 1);
  });
});

describe('outline', () => {
  it('extrudes a flat outline with holes, sitting on the slab', async () => {
    const root = await buildAssembly({
      name: 'frame',
      footprint: [200, 200],
      layers: [
        {
          name: 'base',
          thickness: 0,
          parts: [
            {
              name: 'Plate',
              shape: 'outline',
              height: 3,
              points: [
                [-20, -50],
                [20, -50],
                [20, 50],
                [-20, 50],
              ],
              cutouts: [{ at: [0, 30], radius: 5 }],
            },
          ],
        },
      ],
    });
    const plate = root.getObjectByName('Plate')!;
    const b = bounds(plate);
    expect(b.max.x - b.min.x).toBeGreaterThan(39.9);
    expect(b.max.z - b.min.z).toBeGreaterThan(99.9);
    expect(b.min.y).toBeGreaterThan(-0.5);
    const down = new THREE.Vector3(0, -1, 0);
    const ray = new THREE.Raycaster(new THREE.Vector3(0, 10, 30), down);
    expect(ray.intersectObject(plate)).toHaveLength(0); // through the hole, which is at +z as written
    ray.set(new THREE.Vector3(0, 10, -30), down);
    expect(ray.intersectObject(plate).length).toBeGreaterThan(0);
  });
});
