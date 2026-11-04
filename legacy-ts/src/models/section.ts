import * as THREE from 'three';
import { SectionCaps } from '../lib/sectionCaps';

export type Axis = 'x' | 'y' | 'z';

const NORMALS: Record<Axis, THREE.Vector3> = {
  x: new THREE.Vector3(1, 0, 0),
  y: new THREE.Vector3(0, 1, 0),
  z: new THREE.Vector3(0, 0, 1),
};

/**
 * A cut through any model along one axis. The far side of the plane is clipped away and the
 * opened solids get hatched caps (SectionCaps), so the cut reads as a filled cross-section.
 * A thin outline the size of the model's cross-section marks where the plane is.
 *
 * Only needs something with `clippingPlanes` (a WebGLRenderer), so it can be tested headless.
 */
export class SectionCut {
  readonly plane = new THREE.Plane();
  /** 0: everything cut away, 1: nothing cut. */
  position = 0.5;
  axis: Axis = 'x';
  private root: THREE.Object3D | null = null;
  private readonly caps = new SectionCaps();
  private readonly bounds = new THREE.Box3();
  private outline: THREE.LineSegments | null = null;

  constructor(private readonly renderer: { clippingPlanes: THREE.Plane[] }) {}

  get active() {
    return this.root !== null;
  }

  enable(root: THREE.Object3D, axis: Axis = this.axis) {
    this.disable();
    this.root = root;
    this.axis = axis;
    this.renderer.clippingPlanes = [this.plane];
    this.fit();
  }

  setAxis(axis: Axis) {
    if (this.root) this.enable(this.root, axis);
    else this.axis = axis;
  }

  set(position: number) {
    this.position = THREE.MathUtils.clamp(position, 0, 1);
    if (this.root) this.place();
  }

  /** Move the cut so the plane passes through a world point (clamped to the model). */
  cutThrough(point: THREE.Vector3) {
    if (!this.root || this.bounds.isEmpty()) return;
    const n = NORMALS[this.axis];
    const c = this.bounds.getCenter(new THREE.Vector3());
    const half = this.bounds.getSize(new THREE.Vector3()).multiply(n).length() / 2;
    this.set((point.clone().sub(c).dot(n) / (half * 1.02) + 1) / 2);
  }

  /** Re-measure after the model moved or changed shape (an explode, say). */
  fit() {
    const root = this.root;
    if (!root) return;
    root.updateWorldMatrix(true, true);
    this.removeOutline();
    this.caps.clear();
    this.bounds.setFromObject(root, true);
    if (this.bounds.isEmpty()) return;
    const size = this.bounds.getSize(new THREE.Vector3());
    // The plane's local x / y land on these world axes once it is turned to face the axis.
    const [w, h] = this.axis === 'x' ? [size.z, size.y] : this.axis === 'y' ? [size.x, size.z] : [size.x, size.y];
    const pad = 1.06;
    this.outline = new THREE.LineSegments(
      new THREE.EdgesGeometry(new THREE.PlaneGeometry(w * pad, h * pad)),
      new THREE.LineBasicMaterial({ color: 0x9cff3a, transparent: true, opacity: 0.8, toneMapped: false }),
    );
    this.outline.quaternion.setFromUnitVectors(NORMALS.z, NORMALS[this.axis]);
    this.outline.renderOrder = 998;
    this.outline.userData.sectionCap = true; // not a part: no cap, no picking
    this.outline.raycast = () => {};
    root.parent?.add(this.outline);
    this.caps.build(root, size.length());
    this.place();
  }

  disable() {
    this.renderer.clippingPlanes = [];
    this.caps.clear();
    this.removeOutline();
    this.root = null;
  }

  /** True when a world point is on the side of the cut that is still shown. */
  keeps(point: THREE.Vector3) {
    return !this.root || this.plane.distanceToPoint(point) >= 0;
  }

  private place() {
    if (this.bounds.isEmpty()) return;
    const n = NORMALS[this.axis];
    const c = this.bounds.getCenter(new THREE.Vector3());
    const half = this.bounds.getSize(new THREE.Vector3()).multiply(n).length() / 2;
    const p = c.addScaledVector(n, (this.position * 2 - 1) * half * 1.02);
    // The side the axis points to is removed.
    this.plane.setFromNormalAndCoplanarPoint(n.clone().negate(), p);
    this.outline?.position.copy(p).addScaledVector(n, -half * 0.002);
    this.caps.setPlane(n, p);
  }

  private removeOutline() {
    if (!this.outline) return;
    this.outline.removeFromParent();
    this.outline.geometry.dispose();
    (this.outline.material as THREE.Material).dispose();
    this.outline = null;
  }
}
