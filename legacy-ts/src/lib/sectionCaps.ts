import * as THREE from 'three';
import type { SectionStack } from './sectionStack';

/**
 * Filled cut faces without a stencil buffer. Every opaque mesh gets a twin that draws only its
 * back faces, unlit, in the mesh's own colour with a hatch laid out in the cut plane. Where the
 * clipping plane opens a closed solid, the eye looks into the solid and sees those back faces:
 * the cut reads as a filled cross-section. Where nothing is cut, the front faces hide the twin.
 * Twins share the original geometry (and instance matrices) and are drawn after it, so a
 * double-sided original never z-fights them.
 */
export class SectionCaps {
  private twins: THREE.Mesh[] = [];
  private materials = new Map<string, THREE.ShaderMaterial>();
  /** Shared by every cap material: one update moves the hatch for all of them. */
  private shared = {
    uOrigin: { value: new THREE.Vector3() },
    uNormal: { value: new THREE.Vector3(0, 0, 1) },
    uAxisU: { value: new THREE.Vector3(1, 0, 0) },
    uAxisV: { value: new THREE.Vector3(0, 1, 0) },
    uPitch: { value: 1 },
  };

  build(scene: THREE.Object3D, size: number) {
    this.clear();
    // About 70 hatch lines across the scene, whatever its units.
    this.shared.uPitch.value = size / 70;
    const meshes: THREE.Mesh[] = [];
    scene.traverse((o) => {
      const m = o as THREE.Mesh;
      if (m.isMesh && m.visible && !m.userData.sectionCap) meshes.push(m);
    });
    for (const mesh of meshes) {
      const mat = (Array.isArray(mesh.material) ? mesh.material[0] : mesh.material) as THREE.Material & {
        color?: THREE.Color;
        uniforms?: Record<string, THREE.IUniform>;
      };
      if (!mat || (mat.transparent && mat.opacity < 0.6) || mat.blending === THREE.AdditiveBlending) continue;
      if (mat.side === THREE.BackSide || OPEN_GEOMETRY.has(mesh.geometry.type)) continue;
      const color = mat.color ?? (mat.uniforms?.uColor?.value as THREE.Color | undefined) ?? FALLBACK_CAP;
      const stack = mesh.userData.section as SectionStack | undefined;
      const capMat = stack ? this.layered(color, stack) : this.material(color);
      const inst = mesh as THREE.InstancedMesh;
      let twin: THREE.Mesh;
      if (inst.isInstancedMesh) {
        const t = new THREE.InstancedMesh(inst.geometry, capMat, inst.count);
        t.instanceMatrix = inst.instanceMatrix;
        twin = t;
      } else {
        twin = new THREE.Mesh(mesh.geometry, capMat);
      }
      twin.userData.sectionCap = true;
      twin.frustumCulled = false;
      twin.renderOrder = mesh.renderOrder + 1;
      twin.raycast = () => {};
      mesh.add(twin);
      this.twins.push(twin);
    }
  }

  setPlane(normal: THREE.Vector3, point: THREE.Vector3) {
    this.shared.uOrigin.value.copy(point);
    this.shared.uNormal.value.copy(normal);
    const u = this.shared.uAxisU.value;
    u.set(0, 1, 0);
    if (Math.abs(normal.dot(u)) > 0.9) u.set(1, 0, 0);
    u.cross(normal).normalize();
    this.shared.uAxisV.value.crossVectors(normal, u).normalize();
  }

  clear() {
    for (const t of this.twins) t.removeFromParent();
    this.twins = [];
    this.materials.forEach((m) => m.dispose());
    this.materials.clear();
  }

  /** Parts with an inner structure (a multilayer board) get their own material. */
  private layered(color: THREE.Color, stack: SectionStack) {
    const m = makeCapMaterial(color, this.shared, stack);
    this.materials.set(`layers-${this.materials.size}`, m);
    return m;
  }

  private material(color: THREE.Color) {
    const key = color.getHexString();
    let m = this.materials.get(key);
    if (!m) {
      m = makeCapMaterial(color, this.shared);
      this.materials.set(key, m);
    }
    return m;
  }
}

const FALLBACK_CAP = new THREE.Color(0x8a8f96);
/** Surfaces without a volume: there is nothing inside them to show as a cut. */
const OPEN_GEOMETRY = new Set(['PlaneGeometry', 'ShapeGeometry', 'CircleGeometry', 'RingGeometry']);

const MAX_LAYERS = 32;

function makeCapMaterial(color: THREE.Color, shared: Record<string, THREE.IUniform>, stack?: SectionStack) {
  const edges = new Array<number>(MAX_LAYERS).fill(0);
  const colors = Array.from({ length: MAX_LAYERS }, () => new THREE.Color());
  const n = Math.min(stack?.layers.length ?? 0, MAX_LAYERS);
  let acc = 0;
  for (let i = 0; i < n; i++) {
    acc += stack!.layers[i].thickness;
    edges[i] = acc;
    colors[i].set(stack!.layers[i].color);
  }
  return new THREE.ShaderMaterial({
    uniforms: {
      ...shared,
      uColor: { value: color.clone() },
      uLayerAxis: { value: new THREE.Vector3(...(stack?.axis ?? [0, 1, 0])) },
      uLayerStart: { value: stack?.start ?? 0 },
      uLayerEdge: { value: edges },
      uLayerColor: { value: colors },
    },
    defines: { LAYERS: n },
    side: THREE.BackSide,
    clipping: true,
    vertexShader: /* glsl */ `
      #include <common>
      #include <clipping_planes_pars_vertex>
      varying vec3 vWorld;
      varying vec3 vLocal;
      varying vec3 vCamLocal;
      void main() {
        #include <begin_vertex>
        vLocal = transformed;
        #include <project_vertex>
        #include <clipping_planes_vertex>
        mat4 toWorld = modelMatrix;
        #ifdef USE_INSTANCING
          toWorld = modelMatrix * instanceMatrix;
        #endif
        vWorld = (toWorld * vec4(transformed, 1.0)).xyz;
        vCamLocal = (inverse(toWorld) * vec4(cameraPosition, 1.0)).xyz;
      }
    `,
    fragmentShader: /* glsl */ `
      #include <clipping_planes_pars_fragment>
      uniform vec3 uColor;
      uniform vec3 uOrigin;
      uniform vec3 uAxisU;
      uniform vec3 uAxisV;
      uniform float uPitch;
      uniform vec3 uNormal;
      uniform vec3 uLayerAxis;
      uniform float uLayerStart;
      uniform float uLayerEdge[${MAX_LAYERS}];
      uniform vec3 uLayerColor[${MAX_LAYERS}];
      varying vec3 vWorld;
      varying vec3 vLocal;
      varying vec3 vCamLocal;

      vec3 baseColor(vec3 local) {
        #if LAYERS > 0
          float h = dot(local, uLayerAxis) - uLayerStart;
          for (int i = 0; i < LAYERS; i++) {
            if (h <= uLayerEdge[i]) return uLayerColor[i];
          }
          return uLayerColor[LAYERS - 1];
        #else
          return uColor;
        #endif
      }

      void main() {
        #include <clipping_planes_fragment>
        // The back face we are drawing lies somewhere inside the solid; what the eye should see
        // is the solid where the view ray crosses the cut plane. Same ray, in world and local
        // space (the map between them is affine, so the ray parameter is shared).
        vec3 ray = vWorld - cameraPosition;
        float denom = dot(ray, uNormal);
        float t = abs(denom) > 1e-6 ? clamp(dot(uOrigin - cameraPosition, uNormal) / denom, 0.0, 1.0) : 1.0;
        vec3 onPlane = cameraPosition + ray * t;
        vec3 local = vCamLocal + (vLocal - vCamLocal) * t;
        // Engineering-drawing hatch at 45 degrees, in the plane of the cut.
        vec3 d = onPlane - uOrigin;
        float s = (dot(d, uAxisU) + dot(d, uAxisV)) / uPitch;
        float w = fwidth(s);
        float dist = 0.5 - abs(fract(s) - 0.5); // 0 on a line, 0.5 between two
        float line = 1.0 - smoothstep(0.09, 0.09 + w * 1.5, dist);
        vec3 c = baseColor(local) * 0.85 + 0.03;
        // A layered part already reads as a section: keep its hatch faint.
        gl_FragColor = vec4(mix(c, c * 0.45, line * (LAYERS > 0 ? 0.25 : 0.6)), 1.0);
      }
    `,
  });
}
