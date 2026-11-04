import * as THREE from 'three';
import { fitDistance } from './frame';

export interface PartReport {
  name: string;
  category: string;
  info: string;
  /** Facts from the data file first, then measured ones (size, material, copies). */
  specs: [string, string][];
  /** Named parts directly inside this one, for "consists of". */
  children: THREE.Object3D[];
}

/** Millimetres, with as many decimals as small sizes need. */
export function fmtMm(v: number) {
  const a = Math.abs(v);
  const s = a >= 10 ? v.toFixed(0) : a >= 1 ? v.toFixed(1) : v.toFixed(2);
  return s.includes('.') ? s.replace(/0+$/, '').replace(/\.$/, '') : s;
}

const title = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);

/** Everything the inspector panel shows about a picked part. Sizes assume model units are mm. */
export function inspectPart(o: THREE.Object3D): PartReport {
  const specs: [string, string][] = Object.entries((o.userData.specs as Record<string, string>) ?? {});
  const inst = o as THREE.InstancedMesh;
  o.updateWorldMatrix(true, true);
  let size: THREE.Vector3;
  if (inst.isInstancedMesh && inst.count > 1) {
    // One copy, not the whole row.
    inst.geometry.computeBoundingBox();
    size = inst.geometry.boundingBox!.getSize(new THREE.Vector3());
    size.multiply(new THREE.Vector3().setFromMatrixScale(inst.matrixWorld));
  } else {
    size = new THREE.Box3().setFromObject(o, true).getSize(new THREE.Vector3());
  }
  specs.push([
    inst.isInstancedMesh && inst.count > 1 ? 'Size (each)' : 'Size',
    `${[size.x, size.y, size.z].map(fmtMm).join(' × ')} mm`,
  ]);
  if (o.userData.material) specs.push(['Material', title(o.userData.material as string)]);
  if (inst.isInstancedMesh && inst.count > 1) specs.push(['Copies', String(inst.count)]);
  if (!o.userData.material && !o.userData.category) {
    // A loaded file: no data about it, so say how detailed it is.
    let triangles = 0;
    o.traverse((c) => {
      const g = (c as THREE.Mesh).geometry;
      if ((c as THREE.Mesh).isMesh && g) triangles += (g.index ? g.index.count : g.getAttribute('position').count) / 3;
    });
    if (triangles) specs.push(['Triangles', triangles.toLocaleString('en')]);
  }
  return {
    name: o.name,
    category: (o.userData.category as string) ?? 'Part',
    info: (o.userData.info as string) ?? '',
    specs,
    children: o.children.filter((c) => c.name && !c.userData.sectionCap && !c.userData.slab),
  };
}

/**
 * Where to put the camera to look at `o`: centred on it, close enough that it fills about
 * two thirds of the view, coming from the direction the camera already looks from.
 */
export function focusPose(o: THREE.Object3D | THREE.Box3, fovDeg: number, aspect: number, from: THREE.Vector3) {
  let box: THREE.Box3;
  if (o instanceof THREE.Box3) box = o;
  else {
    o.updateWorldMatrix(true, true);
    box = new THREE.Box3().setFromObject(o, true);
  }
  const sphere = box.getBoundingSphere(new THREE.Sphere());
  const r = Math.max(sphere.radius, 1e-6);
  const hfov = THREE.MathUtils.radToDeg(2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(fovDeg) / 2) * aspect));
  const dist = fitDistance(r, Math.min(fovDeg, hfov)) * 1.5;
  const dir = from.clone().sub(sphere.center);
  if (dir.lengthSq() < 1e-12) dir.set(0, 0.5, 1);
  return { target: sphere.center.clone(), position: sphere.center.clone().add(dir.normalize().multiplyScalar(dist)) };
}

/** World-space box of one copy in an instanced row. */
export function instanceBox(mesh: THREE.InstancedMesh, index: number) {
  mesh.updateWorldMatrix(true, false);
  mesh.geometry.computeBoundingBox();
  const m = new THREE.Matrix4();
  mesh.getMatrixAt(index, m);
  return mesh.geometry.boundingBox!.clone().applyMatrix4(m.premultiply(mesh.matrixWorld));
}

/** The name of one copy: from the row's `names` list, else "Row name 3". */
export function instanceName(mesh: THREE.InstancedMesh, index: number) {
  const names = mesh.userData.names as string[] | undefined;
  return names?.[index] ?? `${mesh.name} ${index + 1}`;
}

/** The inspector report for one copy in a row: its own name and size, which copy it is. */
export function inspectInstance(mesh: THREE.InstancedMesh, index: number): PartReport {
  const row = inspectPart(mesh);
  const size = instanceBox(mesh, index).getSize(new THREE.Vector3());
  const specs = row.specs.filter(([k]) => k !== 'Size (each)' && k !== 'Copies');
  const own = (mesh.userData.specs as Record<string, string> | undefined) ?? {};
  specs.splice(Object.keys(own).length, 0, ['Size', `${[size.x, size.y, size.z].map(fmtMm).join(' × ')} mm`]);
  specs.push(['Copy', `${index + 1} of ${mesh.count}`]);
  return { ...row, name: instanceName(mesh, index), category: `${row.category} · ${mesh.name}`, specs, children: [] };
}

/** "25 cm", "36 mm", "250 µm", "2 nm" -> metres. */
export function metres(scale: string) {
  const m = /([\d.]+)\s*(km|cm|mm|µm|um|nm|pm|m)/.exec(scale ?? '');
  if (!m) return NaN;
  const unit = { km: 1e3, m: 1, cm: 1e-2, mm: 1e-3, µm: 1e-6, um: 1e-6, nm: 1e-9, pm: 1e-12 }[m[2]]!;
  return parseFloat(m[1]) * unit;
}
