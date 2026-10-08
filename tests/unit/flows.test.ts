import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { presetLinks } from '../../src/models/flows';
import { buildAssembly, type LayerSpec } from '../../src/models/assembly';

const board: LayerSpec = {
  name: 'board',
  thickness: 1,
  flows: 'gpu-board',
  parts: [
    { name: 'GPU', role: 'gpu', shape: 'box', size: [20, 1, 20] },
    {
      name: 'VRAM',
      role: 'memory',
      shape: 'box',
      size: [10, 1, 8],
      at: [-30, 20],
      repeat: { count: 2, step: [60, 0], names: ['M1', 'M2'] },
    },
    { name: 'Phases', role: 'vrm', shape: 'box', size: [5, 1, 5], at: [30, -20], repeat: { count: 3, step: [0, 6] } },
    { name: '12 V', role: 'power-in', shape: 'box', size: [10, 5, 5], at: [45, -25] },
  ],
};

describe('presetLinks', () => {
  it('draws the standard flows between parts by role, skipping roles that are missing', () => {
    const links = presetLinks(board, 100);
    expect(links.map((l) => [l.name, l.from, l.to])).toEqual([
      ['12 V input', '12 V', ['Phases']],
      ['GPU core supply', 'Phases', ['GPU']],
      ['Memory bus', 'GPU', ['M1', 'M2']],
    ]);
  });

  it('explicit links win over a preset flow of the same name', () => {
    const links = presetLinks({ ...board, links: [{ name: 'Memory bus', from: 'GPU', to: 'M1' }] }, 100);
    expect(links.some((l) => l.name === 'Memory bus')).toBe(false);
  });

  it('no preset, no flows', () => {
    expect(presetLinks({ ...board, flows: undefined }, 100)).toEqual([]);
    expect(presetLinks({ ...board, flows: 'nope' }, 100)).toEqual([]);
  });

  it('builds into animated buses in an assembly', async () => {
    const root = await buildAssembly({ name: 'card', footprint: [120, 80], layers: [board] });
    const buses: THREE.Object3D[] = [];
    root.traverse((o) => o.userData.flow && buses.push(o));
    expect(buses.map((b) => b.name).sort()).toEqual(['12 V input', 'GPU core supply', 'Memory bus']);
  });
});
