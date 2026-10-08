import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { buildAssembly, mirrorX, type AssemblySpec } from '../../src/models/assembly';
import { explodeStack } from '../../src/models/explode';
import { evaluate, formatReadout, lookup, startValues } from '../../src/models/logic';
import { locate, refreshWires } from '../../src/models/wires';

describe('rules', () => {
  const map: [number, number][] = [
    [0, 0],
    [50, 10],
    [100, 40],
  ];
  it('interpolates between points and stays flat beyond the ends', () => {
    expect(lookup(map, 25)).toBe(5);
    expect(lookup(map, 75)).toBe(25);
    expect(lookup(map, -10)).toBe(0);
    expect(lookup(map, 200)).toBe(40);
    expect(lookup([], 3)).toBe(0);
    expect(lookup(map, NaN)).toBe(0);
  });
  it('evaluates numbers, rules and missing controls', () => {
    expect(evaluate(7, {})).toBe(7);
    expect(evaluate({ by: 'throttle', map }, { throttle: 50 })).toBe(10);
    expect(evaluate({ by: 'throttle', map }, {})).toBe(0);
    expect(evaluate(undefined, {}, 1)).toBe(1);
  });
  it('starts controls at their value, clamped, keeping values carried from another level', () => {
    const controls = [
      { id: 'throttle', label: 'Throttle', value: 30 },
      { id: 'temp', label: 'Temperature', min: 20, max: 80 },
    ];
    expect(startValues(controls)).toEqual({ throttle: 30, temp: 20 });
    expect(startValues(controls, { throttle: 140, other: 1 })).toEqual({ throttle: 100, temp: 20 });
  });
  it('formats readouts with grouped thousands', () => {
    expect(formatReadout({ unit: 'rpm' }, 21400)).toBe('21 400 rpm');
    expect(formatReadout({ unit: 'V', digits: 1 }, 22.2)).toBe('22.2 V');
    expect(formatReadout({}, -3)).toBe('−3');
  });
});

/** Two boards on two layers, a wire from the lower to the upper, a throttle and a spinning fan. */
const spec: AssemblySpec = {
  name: 'Two boards',
  footprint: [40, 40],
  controls: [{ id: 'throttle', label: 'Throttle', value: 20 }],
  readouts: [
    {
      label: 'Current',
      by: 'throttle',
      map: [
        [0, 0],
        [100, 40],
      ],
      unit: 'A',
    },
  ],
  layers: [
    {
      name: 'Lower',
      thickness: 1,
      parts: [
        { name: 'Pad', shape: 'box', size: [4, 1, 4], at: [10, 0] },
        {
          name: 'Pins',
          shape: 'box',
          size: [1, 1, 1],
          at: [-10, 0],
          repeat: { count: 2, step: [0, 5], names: ['P1', 'P2'] },
        },
      ],
    },
    { name: 'Upper', thickness: 1, base: 20, parts: [{ name: 'Socket', shape: 'box', size: [4, 2, 4], at: [10, 0] }] },
    {
      name: 'Rotor',
      thickness: 0,
      parts: [
        {
          name: 'Prop',
          shape: 'fan',
          radius: 8,
          height: 1,
          blades: 3,
          spin: {
            by: 'throttle',
            map: [
              [0, 0],
              [100, 30000],
            ],
          },
        },
      ],
    },
  ],
  wires: [
    {
      name: 'Feed',
      from: 'Pad',
      to: 'Socket',
      kind: 'power',
      gauge: 1.5,
      level: {
        by: 'throttle',
        map: [
          [0, 0],
          [100, 1],
        ],
      },
    },
    { name: 'Signal', from: 'P2', to: 'Socket', via: [[-10, 10, 5]] },
    { name: 'Lost', from: 'Pad', to: 'Nowhere' },
  ],
};

describe('wires', () => {
  it('finds parts, layers and single copies of a row', async () => {
    const root = await buildAssembly(spec);
    expect(locate(root, 'Socket')?.object.name).toBe('Socket');
    const p2 = locate(root, 'P2')!;
    expect(p2.object.name).toBe('Pins');
    expect(p2.box.getCenter(new THREE.Vector3()).z).toBeCloseTo(5);
    expect(locate(root, 'Nowhere')).toBeNull();
  });

  it('builds a tube per wire between its parts, and reports names it cannot find', async () => {
    const root = await buildAssembly(spec);
    const wiring = root.children.find((c) => c.userData.wires)!;
    expect(wiring.children.map((c) => c.name)).toEqual(['Feed', 'Signal']);
    expect(root.userData.missing).toEqual(['Nowhere']);
    const feed = wiring.children[0] as THREE.Mesh;
    expect(feed.userData.category).toBe('Wire · Power');
    expect(feed.userData.level).toEqual({
      by: 'throttle',
      map: [
        [0, 0],
        [100, 1],
      ],
    });
    feed.geometry.computeBoundingBox();
    const box = feed.geometry.boundingBox!;
    // From the pad's top (y 2) up to the socket's underside (y 21), at x ≈ 10.
    expect(box.min.y).toBeLessThan(2.5);
    expect(box.max.y).toBeGreaterThan(20.5);
    expect(box.getCenter(new THREE.Vector3()).x).toBeCloseTo(10, 0);
    expect(feed.geometry.getAttribute('along')).toBeTruthy();
  });

  it('passes through its waypoints', async () => {
    const root = await buildAssembly(spec);
    const signal = root.getObjectByName('Signal') as THREE.Mesh;
    signal.geometry.computeBoundingBox();
    expect(signal.geometry.boundingBox!.containsPoint(new THREE.Vector3(-10, 10, 5))).toBe(true);
  });

  it('a layer with `base` starts there; the others stack as before', async () => {
    const root = await buildAssembly(spec);
    expect(root.getObjectByName('Upper')!.position.y).toBe(20);
    expect(root.getObjectByName('Rotor')!.position.y).toBeCloseTo(23);
  });

  it('Disassemble leaves the wiring in place and the wires stretch to follow their ends', async () => {
    const root = await buildAssembly(spec);
    const feed = root.getObjectByName('Feed') as THREE.Mesh;
    feed.geometry.computeBoundingBox();
    const before = feed.geometry.boundingBox!.clone();
    explodeStack(root, 1);
    expect(root.children.find((c) => c.userData.wires)!.position.y).toBe(0);
    refreshWires(root);
    feed.geometry.computeBoundingBox();
    const after = feed.geometry.boundingBox!;
    // The middle layer (the socket) stays; the lower one (the pad) moves down, and the wire with it.
    const pad = new THREE.Box3().setFromObject(root.getObjectByName('Pad')!);
    expect(after.min.y).toBeLessThan(before.min.y - 1);
    expect(after.min.y).toBeCloseTo(pad.max.y, 0);
    expect(after.max.y).toBeCloseTo(before.max.y, 1);
  });

  it('keeps controls, readouts and spin rules on the model', async () => {
    const root = await buildAssembly(spec);
    expect(root.userData.controls[0].id).toBe('throttle');
    expect(root.userData.readouts[0].label).toBe('Current');
    const prop = root.getObjectByName('Prop')!;
    expect(evaluate(prop.userData.spin, { throttle: 50 })).toBe(15000);
  });
});

describe('mirrored fan', () => {
  it('flips left to right and keeps its faces facing out', () => {
    const g = new THREE.BoxGeometry(2, 1, 1).translate(3, 0, 0);
    mirrorX(g);
    g.computeBoundingBox();
    expect(g.boundingBox!.max.x).toBeCloseTo(-2);
    // The +x face of the original box now faces -x, and its normal says so.
    const n = g.getAttribute('normal');
    const p = g.getAttribute('position');
    let checked = 0;
    for (let i = 0; i < p.count; i++)
      if (p.getX(i) < -3.99 && Math.abs(n.getX(i)) > 0.5) {
        expect(n.getX(i)).toBeLessThan(0);
        checked++;
      }
    expect(checked).toBe(4);
  });
});
