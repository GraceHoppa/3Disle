import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { genericPackage } from '../../src/models/chip';
import { buildAssembly } from '../../src/models/assembly';

describe('genericPackage', () => {
  it('a big chip gets a ball grid that fits under it, and keeps its real specs', async () => {
    const spec = genericPackage({ name: 'ESP32', size: [10, 1, 10], specs: { Cores: '2' } });
    expect(spec.name).toBe('Inside ESP32');
    expect(spec.layers.map((l) => l.name)).toEqual(['Ball grid', 'Substrate', 'Die', 'Mould compound']);
    const root = await buildAssembly(spec);
    const balls = root.getObjectByName('Solder balls') as THREE.InstancedMesh;
    expect(balls.count).toBeGreaterThan(100);
    const box = new THREE.Box3().setFromObject(balls);
    expect(box.max.x).toBeLessThanOrEqual(5);
    expect(box.min.x).toBeGreaterThanOrEqual(-5);
    expect(root.getObjectByName('Die')!.userData.specs).toEqual({ Cores: '2' });
  });

  it('a small chip gets edge pads instead', () => {
    expect(genericPackage({ name: 'Gyro', size: [3, 1, 2.5] }).layers[0].name).toBe('Edge pads');
  });

  it('says plainly that it is generic', () => {
    const spec = genericPackage({ name: 'X', size: [10, 1, 10] });
    for (const l of spec.layers) expect(l.info).toMatch(/Generic package/);
  });
});
