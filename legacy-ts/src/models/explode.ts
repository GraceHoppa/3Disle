import * as THREE from 'three';

const HOME = 'explodeHome';

/** Assembled local position, captured the first time `explode` sees this child. */
function homeOf(child: THREE.Object3D): THREE.Vector3 {
  const stored = child.userData[HOME] as THREE.Vector3 | undefined;
  if (stored) return stored;
  const home = child.position.clone();
  child.userData[HOME] = home;
  return home;
}

/** The pieces that move apart: every direct child except the wiring, which follows its ends. */
const pieces = (root: THREE.Object3D) => root.children.filter((c) => !c.userData.wires);

/**
 * Move each direct child of `root` out from the model centre, along the line from
 * the model's bounding-box centre to the child's. `amount` 0 leaves the assembly
 * as it was; 1 shifts every part by one bounding-sphere radius. The assembled
 * position is stored on the child the first time, and each call places the child
 * at that position plus the offset for `amount`.
 * A child whose centre is the model centre moves up.
 */
export function explode(root: THREE.Object3D, amount: number) {
  if (!Number.isFinite(amount)) return;

  const children = pieces(root);
  for (const child of children) child.position.copy(homeOf(child));
  if (children.length === 0 || amount === 0) return;

  root.updateWorldMatrix(true, true);
  const det = root.matrixWorld.determinant();
  if (!Number.isFinite(det) || det === 0) return;

  const modelBox = new THREE.Box3().setFromObject(root, true);
  if (modelBox.isEmpty()) return;
  const radius = modelBox.getBoundingSphere(new THREE.Sphere()).radius;
  if (!(radius > 0)) return;

  const modelCenter = modelBox.getCenter(new THREE.Vector3());
  const box = new THREE.Box3();
  const center = new THREE.Vector3();
  const dir = new THREE.Vector3();
  const world = new THREE.Vector3();
  // A centre separation this small is numerical dust, not a real direction.
  const minSepSq = (1e-8 * radius) ** 2;
  const distance = amount * radius;

  for (const child of children) {
    box.setFromObject(child, true);
    if (box.isEmpty()) child.getWorldPosition(center);
    else box.getCenter(center);
    dir.subVectors(center, modelCenter);
    if (!(dir.lengthSq() > minSepSq)) dir.set(0, 1, 0);
    else dir.normalize();

    child.getWorldPosition(world).addScaledVector(dir, distance);
    child.position.copy(root.worldToLocal(world));
  }
}

/** Put every direct child back on the position stored by `explode`. */
export function restore(root: THREE.Object3D) {
  for (const child of pieces(root)) {
    const home = child.userData[HOME] as THREE.Vector3 | undefined;
    if (home) child.position.copy(home);
  }
}

/**
 * Spread a stack of layers (direct children) apart along `axis`. Layers are ordered by where
 * their centres sit on the axis, and neighbours open by the same gap however thin they are, so
 * a 0.07 mm copper layer separates as clearly as a 4 mm heatsink. The stack spreads about its
 * middle, so the model stays centred. At 1 the whole spread equals the model's radius.
 * Shares the stored positions with `explode`, so `restore` undoes either.
 */
export function explodeStack(root: THREE.Object3D, amount: number, axis = new THREE.Vector3(0, 1, 0)) {
  if (!Number.isFinite(amount)) return;
  const children = pieces(root);
  for (const child of children) child.position.copy(homeOf(child));
  if (children.length < 2 || amount === 0) return;

  root.updateWorldMatrix(true, true);
  const modelBox = new THREE.Box3().setFromObject(root, true);
  if (modelBox.isEmpty()) return;
  const radius = modelBox.getBoundingSphere(new THREE.Sphere()).radius;
  const dir = axis.clone().normalize();

  const box = new THREE.Box3();
  const center = new THREE.Vector3();
  const ranked = children
    .map((child) => {
      box.setFromObject(child, true);
      if (box.isEmpty()) child.getWorldPosition(center);
      else box.getCenter(center);
      return { child, along: center.dot(dir) };
    })
    .sort((a, b) => a.along - b.along);

  const gap = (radius / (ranked.length - 1)) * amount;
  const mid = (ranked.length - 1) / 2;
  const world = new THREE.Vector3();
  ranked.forEach(({ child }, i) => {
    child.getWorldPosition(world).addScaledVector(dir, (i - mid) * gap);
    child.position.copy(root.worldToLocal(world));
  });
}
