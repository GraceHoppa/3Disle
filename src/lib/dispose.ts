import * as THREE from 'three';

/** Dispose every geometry, material and texture under root, except shared textures. */
export function disposeObject(root: THREE.Object3D, keep: Set<THREE.Texture> = new Set()) {
  const materials = new Set<THREE.Material>();
  const geometries = new Set<THREE.BufferGeometry>();

  root.traverse((obj) => {
    const o = obj as THREE.Mesh;
    if (o.geometry) geometries.add(o.geometry);
    if (o.material) {
      const mats = Array.isArray(o.material) ? o.material : [o.material];
      mats.forEach((m) => materials.add(m));
    }
    if ((obj as THREE.InstancedMesh).isInstancedMesh) (obj as THREE.InstancedMesh).dispose();
  });

  geometries.forEach((g) => g.dispose());
  materials.forEach((m) => {
    for (const value of Object.values(m)) {
      if (value instanceof THREE.Texture && !keep.has(value)) value.dispose();
    }
    const uniforms = (m as THREE.ShaderMaterial).uniforms;
    if (uniforms) {
      for (const u of Object.values(uniforms)) {
        if (u.value instanceof THREE.Texture && !keep.has(u.value)) u.value.dispose();
      }
    }
    m.dispose();
  });
}
