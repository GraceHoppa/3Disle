import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { buildLink, linkPaths, ribbonGeometry } from '../../src/models/links';
import { buildAssembly } from '../../src/models/assembly';

const prints = [
  { name: 'GPU', x: 0, z: 0, w: 20, d: 20 },
  { name: 'M1', x: 40, z: 0, w: 10, d: 10 },
  { name: 'M2', x: 0, z: -40, w: 10, d: 10 },
];

describe('linkPaths', () => {
  it('routes `lanes` traces per target, from the source edge to the target edge', () => {
    const paths = linkPaths({ from: 'GPU', to: ['M1', 'M2'], lanes: 4 }, prints);
    expect(paths).toHaveLength(8);
    const toM1 = paths.slice(0, 4);
    for (const p of toM1) {
      expect(p[0][0]).toBeCloseTo(10); // GPU's right edge
      expect(p[p.length - 1][0]).toBeCloseTo(35); // M1's left edge
    }
    // Lanes sit side by side, not on top of each other.
    expect(new Set(toM1.map((p) => p[0][1].toFixed(3))).size).toBe(4);
  });

  it('skips names that are not on the layer', () => {
    expect(linkPaths({ from: 'GPU', to: ['M9'] }, prints)).toEqual([]);
    expect(linkPaths({ from: 'Nope', to: 'M1' }, prints)).toEqual([]);
    expect(buildLink({ from: 'Nope', to: 'M1' }, prints, 0)).toBeNull();
  });
});

describe('ribbonGeometry', () => {
  it('marks how far along its trace every vertex is, start 0 to end 1', () => {
    const g = ribbonGeometry(
      [
        [
          [0, 0],
          [10, 0],
          [20, 0],
        ],
      ],
      0.5,
      2,
    );
    const along = g.getAttribute('along').array as Float32Array;
    expect(Math.min(...along)).toBe(0);
    expect(Math.max(...along)).toBe(1);
    expect(along).toContain(0.5);
    const pos = g.getAttribute('position');
    for (let i = 0; i < pos.count; i++) expect(pos.getY(i)).toBe(2);
  });
});

describe('links in an assembly', () => {
  it('draws a named, inspectable bus between named parts on the layer', async () => {
    const root = await buildAssembly({
      name: 'card',
      footprint: [100, 60],
      layers: [
        {
          name: 'board',
          thickness: 1.6,
          parts: [
            { name: 'GPU', shape: 'box', size: [20, 2, 20] },
            {
              name: 'Memory',
              shape: 'box',
              size: [10, 1, 8],
              at: [-30, 20],
              repeat: { count: 2, step: [60, 0], names: ['M1', 'M2'] },
            },
          ],
          links: [{ name: 'Memory bus', from: 'GPU', to: ['M1', 'M2'], lanes: 3, info: '2 × 32-bit', category: 'Bus' }],
        },
      ],
    });
    const bus = root.getObjectByName('Memory bus') as THREE.Mesh;
    expect(bus.userData.flow).toBe(true);
    expect(bus.userData).toMatchObject({ info: '2 × 32-bit', category: 'Bus' });
    expect((bus.material as THREE.ShaderMaterial).uniforms.uTime).toBeTruthy();
    expect(bus.geometry.getAttribute('position').count).toBeGreaterThan(0);
  });
});
