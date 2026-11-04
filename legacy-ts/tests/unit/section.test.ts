import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { SectionCut } from '../../src/models/section';

/** A 4 x 2 x 6 box centred at (10, 0, 0), inside a scene so the outline has somewhere to go. */
function setup() {
  const scene = new THREE.Scene();
  const root = new THREE.Group();
  const box = new THREE.Mesh(new THREE.BoxGeometry(4, 2, 6), new THREE.MeshStandardMaterial());
  box.position.x = 10;
  root.add(box);
  scene.add(root);
  const renderer = { clippingPlanes: [] as THREE.Plane[] };
  return { scene, root, box, renderer, cut: new SectionCut(renderer) };
}
const caps = (o: THREE.Object3D) => {
  let n = 0;
  o.traverse((c) => c.userData.sectionCap && (c as THREE.Mesh).isMesh && n++);
  return n;
};

describe('SectionCut', () => {
  it('clips through the middle at 0.5 and keeps the side the axis points away from', () => {
    const { root, renderer, cut } = setup();
    cut.enable(root, 'x');
    expect(renderer.clippingPlanes).toEqual([cut.plane]);
    expect(cut.plane.distanceToPoint(new THREE.Vector3(10, 0, 0))).toBeCloseTo(0);
    expect(cut.keeps(new THREE.Vector3(9, 0, 0))).toBe(true);
    expect(cut.keeps(new THREE.Vector3(11, 0, 0))).toBe(false);
  });

  it('cuts nothing at 1 and everything at 0', () => {
    const { root, cut } = setup();
    cut.enable(root, 'z');
    cut.set(1);
    expect(cut.keeps(new THREE.Vector3(10, 0, 3))).toBe(true);
    cut.set(0);
    expect(cut.keeps(new THREE.Vector3(10, 0, -3))).toBe(false);
  });

  it('clamps the position', () => {
    const { root, cut } = setup();
    cut.enable(root);
    cut.set(7);
    expect(cut.position).toBe(1);
    cut.set(-1);
    expect(cut.position).toBe(0);
  });

  it('adds hatched caps to solid parts and an outline sized to the cross-section', () => {
    const { scene, root, box, cut } = setup();
    cut.enable(root, 'y');
    expect(caps(box)).toBe(1);
    const outline = scene.children.find((c) => (c as THREE.LineSegments).isLineSegments)!;
    expect(outline).toBeTruthy();
    const size = new THREE.Box3().setFromObject(outline).getSize(new THREE.Vector3());
    // Cutting along y shows the x / z footprint (4 x 6), plus a little padding.
    expect(size.x).toBeCloseTo(4 * 1.06, 4);
    expect(size.z).toBeCloseTo(6 * 1.06, 4);
  });

  it('switching axis rebuilds instead of piling up caps', () => {
    const { root, box, cut } = setup();
    cut.enable(root, 'x');
    cut.setAxis('y');
    cut.setAxis('z');
    expect(caps(box)).toBe(1);
    expect(cut.axis).toBe('z');
  });

  it('disable removes the plane, caps and outline', () => {
    const { scene, root, box, renderer, cut } = setup();
    cut.enable(root);
    cut.disable();
    expect(renderer.clippingPlanes).toEqual([]);
    expect(caps(box)).toBe(0);
    expect(scene.children.some((c) => (c as THREE.LineSegments).isLineSegments)).toBe(false);
    expect(cut.active).toBe(false);
    expect(cut.keeps(new THREE.Vector3(100, 0, 0))).toBe(true);
  });

  it('skips see-through parts, which have nothing to fill', () => {
    const { root, cut } = setup();
    const glass = new THREE.Mesh(
      new THREE.BoxGeometry(1, 1, 1),
      new THREE.MeshStandardMaterial({ transparent: true, opacity: 0.3 }),
    );
    root.add(glass);
    cut.enable(root);
    expect(caps(glass)).toBe(0);
  });
});
