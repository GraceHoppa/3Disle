import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { buildAssembly, type AssemblySpec } from '../../src/models/assembly';
import { fmtMm, focusPose, inspectPart } from '../../src/models/inspect';
import { SectionCut } from '../../src/models/section';

const spec: AssemblySpec = {
  name: 'board',
  footprint: [100, 40],
  layers: [
    {
      name: 'PCB',
      thickness: 1.6,
      material: 'pcb',
      category: 'Board',
      specs: { Layers: '12' },
      parts: [
        {
          name: 'Memory',
          shape: 'box',
          size: [14, 1.2, 12],
          material: 'chip',
          repeat: { count: 4, step: [20, 0] },
          specs: { Capacity: '2 GB' },
        },
        { name: 'Cap', shape: 'cylinder', radius: 1, height: 3, at: [40, 0] },
      ],
    },
  ],
};

describe('fmtMm', () => {
  it('keeps small sizes readable and drops trailing zeros', () => {
    expect(fmtMm(240)).toBe('240');
    expect(fmtMm(1.6)).toBe('1.6');
    expect(fmtMm(2)).toBe('2');
    expect(fmtMm(0.07)).toBe('0.07');
    expect(fmtMm(0.8)).toBe('0.8');
  });
});

describe('inspectPart', () => {
  it('a layer lists its parts, not its slab, and shows data specs first', async () => {
    const root = await buildAssembly(spec);
    const r = inspectPart(root.getObjectByName('PCB')!);
    expect(r.category).toBe('Board');
    expect(r.children.map((c) => c.name)).toEqual(['Memory', 'Cap']);
    expect(r.specs[0]).toEqual(['Layers', '12']);
    expect(r.specs).toContainEqual(['Material', 'Pcb']);
  });

  it('a repeated part reports one copy and how many there are', async () => {
    const root = await buildAssembly(spec);
    const r = inspectPart(root.getObjectByName('Memory')!);
    expect(r.specs).toContainEqual(['Capacity', '2 GB']);
    expect(r.specs).toContainEqual(['Size (each)', '14 × 1.2 × 12 mm']);
    expect(r.specs).toContainEqual(['Copies', '4']);
    expect(r.category).toBe('Part');
  });

  it('a loaded mesh with no data reports its triangle count', () => {
    const mesh = new THREE.Mesh(new THREE.BoxGeometry(2, 2, 2));
    mesh.name = 'part.stl';
    const r = inspectPart(mesh);
    expect(r.specs).toContainEqual(['Triangles', '12']);
    expect(r.specs).toContainEqual(['Size', '2 × 2 × 2 mm']);
  });
});

describe('focusPose', () => {
  it('centres on the part and keeps the viewing direction', () => {
    const box = new THREE.Mesh(new THREE.BoxGeometry(2, 2, 2));
    box.position.set(5, 0, 0);
    const from = new THREE.Vector3(5, 0, 100);
    const { target, position } = focusPose(box, 40, 1.5, from);
    expect(target.toArray().map((v) => +v.toFixed(6))).toEqual([5, 0, 0]);
    expect(position.x).toBeCloseTo(5);
    expect(position.y).toBeCloseTo(0);
    expect(position.z).toBeGreaterThan(3);
    expect(position.z).toBeLessThan(100);
  });

  it('fits the narrower view on a portrait screen (moves further back)', () => {
    const box = new THREE.Mesh(new THREE.BoxGeometry(2, 2, 2));
    const from = new THREE.Vector3(0, 0, 10);
    const wide = focusPose(box, 40, 2, from).position.z;
    const tall = focusPose(box, 40, 0.5, from).position.z;
    expect(tall).toBeGreaterThan(wide);
  });
});

describe('SectionCut.cutThrough', () => {
  it('puts the plane through the given point', () => {
    const root = new THREE.Group().add(new THREE.Mesh(new THREE.BoxGeometry(10, 2, 2)));
    new THREE.Scene().add(root);
    const cut = new SectionCut({ clippingPlanes: [] });
    cut.enable(root, 'x');
    const p = new THREE.Vector3(2.5, 0, 0);
    cut.cutThrough(p);
    expect(cut.plane.distanceToPoint(p)).toBeCloseTo(0, 6);
    cut.cutThrough(new THREE.Vector3(99, 0, 0));
    expect(cut.position).toBe(1);
  });
});

describe('single copies in a row', () => {
  const rowSpec: AssemblySpec = {
    name: 'r',
    footprint: [100, 20],
    layers: [
      {
        name: 'base',
        thickness: 1,
        parts: [
          {
            name: 'Memory',
            shape: 'box',
            size: [10, 1, 8],
            at: [-30, 0],
            repeat: { count: 3, step: [20, 0], names: ['M1', 'M2'] },
            specs: { Capacity: '2 GB' },
          },
        ],
      },
    ],
  };

  it('boxes one copy where it sits', async () => {
    const { instanceBox } = await import('../../src/models/inspect');
    const root = await buildAssembly(rowSpec);
    const mem = root.getObjectByName('Memory') as THREE.InstancedMesh;
    const c = instanceBox(mem, 1).getCenter(new THREE.Vector3());
    expect(c.x).toBeCloseTo(-10);
    expect(c.y).toBeCloseTo(1.5);
  });

  it('names copies from the list, falling back to a number', async () => {
    const { instanceName } = await import('../../src/models/inspect');
    const root = await buildAssembly(rowSpec);
    const mem = root.getObjectByName('Memory') as THREE.InstancedMesh;
    expect([0, 1, 2].map((i) => instanceName(mem, i))).toEqual(['M1', 'M2', 'Memory 3']);
  });

  it('reports one copy: own size, which copy, data specs kept', async () => {
    const { inspectInstance } = await import('../../src/models/inspect');
    const root = await buildAssembly(rowSpec);
    const r = inspectInstance(root.getObjectByName('Memory') as THREE.InstancedMesh, 1);
    expect(r.name).toBe('M2');
    expect(r.specs[0]).toEqual(['Capacity', '2 GB']);
    expect(r.specs).toContainEqual(['Size', '10 × 1 × 8 mm']);
    expect(r.specs).toContainEqual(['Copy', '2 of 3']);
    expect(r.specs.some(([k]) => k === 'Copies')).toBe(false);
  });

  it('every copy starts uncoloured so one can be lit alone', async () => {
    const root = await buildAssembly(rowSpec);
    const mem = root.getObjectByName('Memory') as THREE.InstancedMesh;
    const c = new THREE.Color();
    mem.getColorAt(2, c);
    expect(c.getHex()).toBe(0xffffff);
  });
});

describe('metres', () => {
  it('reads scale labels in any unit', async () => {
    const { metres } = await import('../../src/models/inspect');
    expect(metres('25 cm')).toBeCloseTo(0.25);
    expect(metres('36 mm')).toBeCloseTo(0.036);
    expect(metres('250 µm')).toBeCloseTo(250e-6);
    expect(metres('2 nm')).toBeCloseTo(2e-9);
    expect(metres('huge')).toBeNaN();
  });
});
