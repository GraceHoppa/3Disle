import * as THREE from 'three';

const _size = new THREE.Vector3();

/** Corner-bracket reticle (8 corners x 3 short edges), drawn on top of everything. */
export function makeBracket(color: number, opacity: number) {
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(48 * 3), 3));
  const mat = new THREE.LineBasicMaterial({ color, transparent: true, opacity, depthTest: false, toneMapped: false });
  const lines = new THREE.LineSegments(geo, mat);
  lines.renderOrder = 999;
  lines.frustumCulled = false;
  lines.visible = false;
  return lines;
}

/**
 * Fit a reticle around a world-space box: the "instrument" look instead of a full wireframe.
 * `tight` keeps the padding and tick length small on long, flat things (board layers), where
 * the default ticks would run far across the screen.
 */
export function fitBracket(lines: THREE.LineSegments, box: THREE.Box3, tight = false) {
  const pos = lines.geometry.getAttribute('position') as THREE.BufferAttribute;
  const min = box.min;
  const max = box.max;
  const size = _size.subVectors(max, min);
  const big = Math.max(size.x, size.y, size.z);
  const pad = big * (tight ? 0.012 : 0.04);
  const lo = [min.x - pad, min.y - pad, min.z - pad];
  const hi = [max.x + pad, max.y + pad, max.z + pad];
  const len = [size.x, size.y, size.z].map((s) => Math.min((s + 2 * pad) * 0.22, tight ? big * 0.08 : Infinity));
  let k = 0;
  for (let c = 0; c < 8; c++) {
    const corner = [c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2]];
    const sign = [c & 1 ? -1 : 1, c & 2 ? -1 : 1, c & 4 ? -1 : 1];
    for (let axis = 0; axis < 3; axis++) {
      pos.setXYZ(k++, corner[0], corner[1], corner[2]);
      const end = [...corner];
      end[axis] += sign[axis] * len[axis];
      pos.setXYZ(k++, end[0], end[1], end[2]);
    }
  }
  pos.needsUpdate = true;
  lines.visible = true;
}
