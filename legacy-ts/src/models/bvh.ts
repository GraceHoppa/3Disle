import * as THREE from 'three';
import { MeshBVH, acceleratedRaycast } from 'three-mesh-bvh';

/**
 * Build a bounding-volume tree for every plain mesh under `root`, so hover and click raycasts
 * test a few triangles instead of all of them. Only these meshes get the faster raycast; the
 * rest of the app is untouched. Returns how many meshes were accelerated.
 */
export function enableBvh(root: THREE.Object3D): number {
  let count = 0;
  root.traverse((o) => {
    const mesh = o as THREE.Mesh;
    if (!mesh.isMesh || (o as THREE.InstancedMesh).isInstancedMesh || mesh.geometry.boundsTree) return;
    mesh.geometry.boundsTree = new MeshBVH(mesh.geometry);
    mesh.raycast = acceleratedRaycast;
    count++;
  });
  return count;
}

/** Drop the trees built by enableBvh (geometry disposal does not free them). */
export function disposeBvh(root: THREE.Object3D) {
  root.traverse((o) => {
    const mesh = o as THREE.Mesh;
    if (mesh.isMesh) mesh.geometry.boundsTree = undefined;
  });
}
