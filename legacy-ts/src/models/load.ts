import * as THREE from 'three';
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';

export type ModelFormat = 'stl' | 'glb' | 'gltf';

/** Who made a model and under what license; shown wherever the model is. */
export interface Credit {
  title?: string;
  author: string;
  license: string;
  source?: string;
}

/**
 * The credit a glTF file carries in `asset.extras` (Sketchfab writes title, author, license and
 * source there). Values like "chiwei (https://…)" keep only the name; null when there is none.
 */
export function creditOf(asset: { extras?: Record<string, unknown> } | undefined): Credit | null {
  const x = asset?.extras;
  const text = (v: unknown) => (typeof v === 'string' && v.trim() ? v.trim() : undefined);
  const author = text(x?.author)?.replace(/\s*\(https?:[^)]*\)\s*$/, '');
  const license = text(x?.license)?.replace(/\s*\(https?:[^)]*\)\s*$/, '');
  if (!author || !license) return null;
  return { title: text(x?.title), author, license, source: text(x?.source) };
}

/** Format from a file name or URL extension; null if it is not one we load. */
export function formatOf(name: string): ModelFormat | null {
  const ext = name.split(/[?#]/)[0].split('.').pop()?.toLowerCase();
  return ext === 'stl' || ext === 'glb' || ext === 'gltf' ? ext : null;
}

/**
 * Turn raw file bytes into a scene object.
 * STL is one mesh with no materials or units, and slicers write it Z-up, so it gets a neutral
 * material and is turned Y-up. GLB / glTF keep their own node tree, materials and orientation.
 */
export async function parseModel(data: ArrayBuffer, format: ModelFormat, name = 'model'): Promise<THREE.Object3D> {
  if (format === 'stl') {
    const geometry = new STLLoader().parse(data) as THREE.BufferGeometry & { hasColors?: boolean };
    if (!geometry.hasAttribute('normal')) geometry.computeVertexNormals();
    const material = new THREE.MeshStandardMaterial({
      color: geometry.hasColors ? 0xffffff : 0xb8bcc4,
      vertexColors: !!geometry.hasColors,
      metalness: 0.1,
      roughness: 0.6,
    });
    const mesh = new THREE.Mesh(geometry, material);
    mesh.name = name;
    mesh.rotation.x = -Math.PI / 2;
    const root = new THREE.Group();
    root.name = name;
    root.add(mesh);
    return root;
  }
  const gltf = await new GLTFLoader().parseAsync(data, '');
  gltf.scene.name ||= name;
  const credit = creditOf(gltf.asset);
  if (credit) gltf.scene.userData.credit = credit;
  return gltf.scene;
}

/** Fetch a model file and parse it by its extension. */
export async function loadModel(url: string): Promise<THREE.Object3D> {
  const format = formatOf(url);
  if (!format) throw new Error(`Unsupported model format: ${url}`);
  const res = await fetch(url);
  if (!res.ok) throw new Error(`Could not load ${url}: ${res.status}`);
  const name = decodeURIComponent(url.split(/[?#]/)[0].split('/').pop() ?? 'model').replace(/\.[^.]+$/, '');
  return parseModel(await res.arrayBuffer(), format, name);
}
