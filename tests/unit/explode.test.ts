import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { explode, explodeStack, restore } from '../../src/models/explode';

const box = (w: number, h: number, d: number) => new THREE.Mesh(new THREE.BoxGeometry(w, h, d));

/** Two unit boxes whose centres sit at x = -2 and x = 2. */
function pair() {
  const root = new THREE.Group();
  const left = box(1, 1, 1);
  left.position.set(-2, 0, 0);
  const right = box(1, 1, 1);
  right.position.set(2, 0, 0);
  root.add(left, right);
  return { root, left, right };
}

function radiusOf(root: THREE.Object3D) {
  root.updateWorldMatrix(true, true);
  return new THREE.Box3().setFromObject(root, true).getBoundingSphere(new THREE.Sphere()).radius;
}

function worldCenter(obj: THREE.Object3D) {
  obj.updateWorldMatrix(true, true);
  return new THREE.Box3().setFromObject(obj, true).getCenter(new THREE.Vector3());
}

const finite = (o: THREE.Object3D) => o.position.toArray().every(Number.isFinite);

describe('explode', () => {
  it('amount 0 changes nothing', () => {
    const { root, left, right } = pair();
    const a = left.position.clone();
    const b = right.position.clone();
    explode(root, 0);
    expect(left.position.toArray()).toEqual(a.toArray());
    expect(right.position.toArray()).toEqual(b.toArray());

    explode(root, 1);
    explode(root, 0);
    expect(left.position.toArray()).toEqual(a.toArray());
    expect(right.position.toArray()).toEqual(b.toArray());
  });

  it('restore is exact', () => {
    const fresh = pair();
    const home = fresh.left.position.clone();
    restore(fresh.root);
    expect(fresh.left.position.toArray()).toEqual(home.toArray());

    const { root, left, right } = pair();
    const a = left.position.clone();
    const b = right.position.clone();
    explode(root, 0.3);
    explode(root, 1);
    restore(root);
    expect(left.position.toArray()).toEqual(a.toArray());
    expect(right.position.toArray()).toEqual(b.toArray());
  });

  it('parts move outward', () => {
    const { root, left, right } = pair();
    const radius = radiusOf(root);
    const modelCenter = worldCenter(root);
    const leftBefore = worldCenter(left).distanceTo(modelCenter);
    const rightBefore = worldCenter(right).distanceTo(modelCenter);

    explode(root, 1);
    expect(worldCenter(left).distanceTo(modelCenter)).toBeGreaterThan(leftBefore);
    expect(worldCenter(right).distanceTo(modelCenter)).toBeGreaterThan(rightBefore);
    expect(left.position.distanceTo(new THREE.Vector3(-2, 0, 0))).toBeCloseTo(radius, 5);
    expect(right.position.x).toBeCloseTo(2 + radius, 5);

    const half = pair();
    explode(half.root, 0.5);
    expect(half.left.position.x).toBeCloseTo(-2 - radius * 0.5, 5);
  });

  it('keeps the outward direction when the model is moved', () => {
    const { root, left } = pair();
    root.rotation.y = Math.PI / 5;
    root.position.set(3, -2, 4);
    const radius = radiusOf(root);
    const modelCenter = worldCenter(root);
    const before = worldCenter(left);
    const away = before.clone().sub(modelCenter).normalize();

    explode(root, 1);
    const moved = worldCenter(left).sub(before);
    expect(moved.length()).toBeCloseTo(radius, 4);
    expect(moved.dot(away)).toBeCloseTo(moved.length(), 4);
    expect(finite(left)).toBe(true);
  });

  it('scales the offset with the model', () => {
    const small = pair();
    const big = pair();
    big.root.scale.setScalar(3);
    const radius = radiusOf(small.root);
    explode(small.root, 1);
    explode(big.root, 1);
    expect(Math.abs(small.left.position.x + 2)).toBeCloseTo(radius, 5);
    expect(Math.abs(big.left.position.x + 2)).toBeCloseTo(radius, 5);
  });

  it('uses the bounding-box centre, not the object origin', () => {
    const root = new THREE.Group();
    const shifted = box(1, 1, 1);
    shifted.geometry.translate(2, 0, 0);
    const other = box(1, 1, 1);
    other.position.set(-2, 0, 0);
    root.add(shifted, other);
    explode(root, 1);
    expect(shifted.position.x).toBeGreaterThan(0);
    expect(finite(shifted)).toBe(true);
    expect(finite(other)).toBe(true);
  });

  it('moves direct children only', () => {
    const root = new THREE.Group();
    const part = new THREE.Group();
    const mesh = box(1, 1, 1);
    mesh.position.set(0, 0.4, 0);
    part.add(mesh);
    part.position.set(-2, 0, 0);
    const other = box(1, 1, 1);
    other.position.set(2, 0, 0);
    root.add(part, other);
    const nested = mesh.position.clone();
    explode(root, 1);
    expect(mesh.position.toArray()).toEqual(nested.toArray());
    expect(part.position.x).toBeLessThan(-2);
  });

  it('repeated explode calls do not drift', () => {
    const { root, left, right } = pair();
    explode(root, 0.4);
    const leftAt = left.position.clone();
    const rightAt = right.position.clone();
    explode(root, 1);
    explode(root, 0.1);
    explode(root, 0.4);
    expect(left.position.toArray()).toEqual(leftAt.toArray());
    expect(right.position.toArray()).toEqual(rightAt.toArray());

    const once = pair();
    explode(once.root, 0.4);
    expect(left.position.toArray()).toEqual(once.left.position.toArray());
    expect(right.position.toArray()).toEqual(once.right.position.toArray());
  });

  it('single-child and centred-child models stay finite', () => {
    const only = box(1, 2, 3);
    only.position.set(4, 1, -2);
    const single = new THREE.Group().add(only);
    const pivot = worldCenter(only);
    explode(single, 1);
    expect(finite(only)).toBe(true);
    expect(only.position.x).toBeCloseTo(4, 6);
    expect(only.position.z).toBeCloseTo(-2, 6);
    expect(only.position.y).toBeGreaterThan(1);
    expect(worldCenter(only).distanceTo(pivot)).toBeGreaterThan(0);

    const root = new THREE.Group();
    const mid = box(1, 1, 1);
    const left = box(1, 1, 1);
    const right = box(1, 1, 1);
    left.position.set(-2, 0, 0);
    right.position.set(2, 0, 0);
    root.add(left, mid, right);
    const midBefore = worldCenter(mid).distanceTo(worldCenter(root));
    explode(root, 1);
    for (const child of root.children) expect(finite(child)).toBe(true);
    expect(mid.position.x).toBeCloseTo(0, 6);
    expect(mid.position.z).toBeCloseTo(0, 6);
    expect(mid.position.y).toBeGreaterThan(0);
    expect(worldCenter(mid).distanceTo(worldCenter(root))).toBeGreaterThan(midBefore);

    explode(root, Number.NaN);
    for (const child of root.children) expect(finite(child)).toBe(true);
  });
});

describe('explodeStack', () => {
  /** Three slabs of very different thickness, stacked bottom to top but added out of order. */
  function stack() {
    const root = new THREE.Group();
    const thin = box(10, 0.1, 10);
    thin.position.y = 0.05;
    const thick = box(10, 4, 10);
    thick.position.y = 2.1;
    const top = box(10, 1, 10);
    top.position.y = 4.6;
    root.add(top, thin, thick);
    return { root, thin, thick, top };
  }
  const bottomOf = (o: THREE.Object3D) => {
    o.updateWorldMatrix(true, true);
    return new THREE.Box3().setFromObject(o).min.y;
  };
  const topOf = (o: THREE.Object3D) => {
    o.updateWorldMatrix(true, true);
    return new THREE.Box3().setFromObject(o).max.y;
  };

  it('opens every pair of neighbours by the same gap, ordered by height not child order', () => {
    const { root, thin, thick, top } = stack();
    explodeStack(root, 1);
    const g1 = bottomOf(thick) - topOf(thin);
    const g2 = bottomOf(top) - topOf(thick);
    expect(g1).toBeGreaterThan(0.5);
    expect(g2).toBeCloseTo(g1, 6);
  });

  it('keeps the stack centred', () => {
    const { root } = stack();
    root.updateWorldMatrix(true, true);
    const before = new THREE.Box3().setFromObject(root).getCenter(new THREE.Vector3());
    explodeStack(root, 1);
    const after = new THREE.Box3().setFromObject(root).getCenter(new THREE.Vector3());
    expect(after.distanceTo(before)).toBeLessThan(1e-6);
  });

  it('restores exactly and mixes with explode without drift', () => {
    const { root, thin, thick, top } = stack();
    const homes = [thin, thick, top].map((o) => o.position.toArray());
    explodeStack(root, 0.7);
    explode(root, 1);
    explodeStack(root, 0.2);
    restore(root);
    expect([thin, thick, top].map((o) => o.position.toArray())).toEqual(homes);
  });

  it('ignores a single layer and NaN', () => {
    const one = new THREE.Group().add(box(1, 1, 1));
    explodeStack(one, 1);
    expect(one.children[0].position.toArray()).toEqual([0, 0, 0]);
    const { root, thin } = stack();
    explodeStack(root, Number.NaN);
    expect(thin.position.y).toBe(0.05);
  });
});
