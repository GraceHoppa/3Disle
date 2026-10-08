import * as THREE from 'three';
import { BlendFunction, Effect } from 'postprocessing';

/**
 * Draws the next scale into its own buffer and reveals it inside the current one: a soft disc
 * centred on the dive target that grows with it, faded by `mix`. At mix = 1 with the disc
 * covering the screen the frame is the next level alone, which is exactly what the next level
 * renders on its own one frame later. Runs right after the scene render, so DOF, bloom and the
 * grade treat both scales as one image.
 */
const fragmentShader = /* glsl */ `
uniform sampler2D tNext;
uniform float uMix;
uniform vec2 uCentre;
uniform float uRadius;
uniform float uAspect;

void mainImage(const in vec4 inputColor, const in vec2 uv, out vec4 outputColor) {
  vec2 d = (uv - uCentre) * vec2(uAspect, 1.0);
  float r = length(d) / max(uRadius, 1e-4);
  // Fully inside the inner 45% of the target, feathered out to its silhouette.
  float disc = 1.0 - smoothstep(0.45, 1.0, r);
  float m = clamp(uMix * disc + smoothstep(0.75, 1.0, uMix) * (1.0 - disc), 0.0, 1.0);
  vec4 next = texture2D(tNext, uv);
  outputColor = vec4(mix(inputColor.rgb, next.rgb, m), inputColor.a);
}
`;

export class SeamEffect extends Effect {
  /** Scene of the next level (null = nothing to reveal). */
  scene: THREE.Scene | null = null;
  /** Camera of the next level (LevelManager.seamCamera). */
  camera = new THREE.PerspectiveCamera();
  private readonly target: THREE.WebGLRenderTarget;

  constructor(samples: number) {
    super('SeamEffect', fragmentShader, {
      blendFunction: BlendFunction.NORMAL,
      uniforms: new Map<string, THREE.Uniform>([
        ['tNext', new THREE.Uniform(null)],
        ['uMix', new THREE.Uniform(0)],
        ['uCentre', new THREE.Uniform(new THREE.Vector2(0.5, 0.5))],
        ['uRadius', new THREE.Uniform(0.2)],
        ['uAspect', new THREE.Uniform(1)],
      ]),
    });
    this.target = new THREE.WebGLRenderTarget(1, 1, { type: THREE.HalfFloatType, samples });
    this.target.texture.name = 'Seam.Next';
    this.uniforms.get('tNext')!.value = this.target.texture;
  }

  get centre(): THREE.Vector2 {
    return this.uniforms.get('uCentre')!.value;
  }

  set mix(v: number) {
    this.uniforms.get('uMix')!.value = v;
  }

  set radius(v: number) {
    this.uniforms.get('uRadius')!.value = v;
  }

  /** Render the next level before the effect samples it. */
  update(renderer: THREE.WebGLRenderer) {
    if (!this.scene) return;
    const prev = renderer.getRenderTarget();
    renderer.setRenderTarget(this.target);
    renderer.clear();
    renderer.render(this.scene, this.camera);
    renderer.setRenderTarget(prev);
  }

  setSize(width: number, height: number) {
    this.target.setSize(width, height);
    this.uniforms.get('uAspect')!.value = width / Math.max(height, 1);
  }

  dispose() {
    this.target.dispose();
    super.dispose();
  }
}
