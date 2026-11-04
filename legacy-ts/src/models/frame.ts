import * as THREE from 'three';

/** A camera position and look-at point at time t along a path. */
export interface CameraKey {
  t: number;
  pos: [number, number, number];
  look: [number, number, number];
}

export interface Framing {
  center: THREE.Vector3;
  radius: number;
}

/** Bounding sphere of everything visible under `root`, in world space. */
export function measure(root: THREE.Object3D): Framing {
  root.updateWorldMatrix(true, true);
  const box = new THREE.Box3().setFromObject(root, true);
  if (box.isEmpty()) throw new Error('Model has no geometry');
  const sphere = box.getBoundingSphere(new THREE.Sphere());
  return { center: sphere.center, radius: Math.max(sphere.radius, 1e-6) };
}

/** Distance at which a sphere of `radius` just fills a vertical field of view of `fovDeg`. */
export function fitDistance(radius: number, fovDeg: number) {
  return radius / Math.sin(THREE.MathUtils.degToRad(fovDeg) / 2);
}

/**
 * A default camera path for any model: arrive from a three-quarter view, swing round to the
 * other side, then move in closer. Every key looks at the centre, and the first two keep
 * the whole model in frame.
 */
export function frameKeys({ center, radius }: Framing, fovDeg = 40): CameraKey[] {
  const d = fitDistance(radius, fovDeg) * 1.15;
  const at = (t: number, dir: [number, number, number], dist: number): CameraKey => {
    const p = new THREE.Vector3(...dir).normalize().multiplyScalar(dist).add(center);
    return { t, pos: [p.x, p.y, p.z], look: [center.x, center.y, center.z] };
  };
  return [at(0, [1, 0.6, 1.2], d * 1.3), at(0.5, [-1.1, 0.45, 1], d), at(1, [-0.4, 0.3, 1], d * 0.7)];
}
