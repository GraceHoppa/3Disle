import * as THREE from 'three';
import {
  BlendFunction,
  BloomEffect,
  BrightnessContrastEffect,
  ChromaticAberrationEffect,
  DepthOfFieldEffect,
  EffectComposer,
  EffectPass,
  HueSaturationEffect,
  NoiseEffect,
  RenderPass,
  ToneMappingEffect,
  ToneMappingMode,
  VignetteEffect,
} from 'postprocessing';
import { QUALITY } from './quality';
import { FxaaEffect } from './FxaaEffect';
import { SeamEffect } from './SeamEffect';
import { GRADE_BASE, hueRadians, lerpGrade, resolveGrade, type Grade } from './Grade';

/** Fully-resolved grade, as applied to the effect uniforms. */
export type ResolvedGrade = Required<Grade>;

/**
 * The whole post chain, isolated from the render loop:
 *
 *   render → seam (next scale revealed during a dive) → [FXAA] → depth of field → grade pass (bloom → chromatic → contrast →
 *   hue/sat → vignette → ACES → film grain)
 *
 * Tone mapping is deliberately near the end so grain lands in display space, like film.
 * Per-level looks are applied through `setGrade()`, which only touches uniforms.
 */
export class PostFX {
  readonly composer: EffectComposer;
  readonly renderPass: RenderPass;
  readonly bloom: BloomEffect;
  readonly dof: DepthOfFieldEffect;
  readonly dofPass: EffectPass;
  /** Reveals the next scale inside the current one during a dive (Phase C seamless descent). */
  readonly seam: SeamEffect;
  readonly seamPass: EffectPass;
  readonly chromatic: ChromaticAberrationEffect;
  readonly grain: NoiseEffect;
  readonly vignette: VignetteEffect;
  readonly hueSaturation: HueSaturationEffect;
  readonly brightnessContrast: BrightnessContrastEffect;

  /** The look currently applied (read by the render loop for per-level multipliers). */
  grade: ResolvedGrade = resolveGrade(-1);
  private gradeKey = '';

  /** Debug multipliers, wired to lil-gui. */
  bloomScale = 1;
  bokehScale = 1;
  grainScale = 1;
  chromaticScale = 1;

  private readonly baseChromatic = 0.00055;
  private readonly baseGrain = GRADE_BASE.grain;
  private readonly baseVignette = 0.62;

  constructor(renderer: THREE.WebGLRenderer, camera: THREE.PerspectiveCamera) {
    this.composer = new EffectComposer(renderer, {
      frameBufferType: THREE.HalfFloatType,
      multisampling: QUALITY.msaaSamples,
    });
    this.renderPass = new RenderPass(new THREE.Scene(), camera);

    this.dof = new DepthOfFieldEffect(camera, {
      focusDistance: 10,
      focusRange: 5,
      bokehScale: 1.5,
      resolutionScale: 0.5,
    });
    this.bloom = new BloomEffect({
      mipmapBlur: true,
      luminanceThreshold: 0.62,
      luminanceSmoothing: 0.25,
      intensity: 1.1,
      radius: 0.7,
    });
    this.chromatic = new ChromaticAberrationEffect({
      offset: new THREE.Vector2(this.baseChromatic, this.baseChromatic),
      radialModulation: true,
      modulationOffset: 0.3,
    });
    this.brightnessContrast = new BrightnessContrastEffect({});
    this.hueSaturation = new HueSaturationEffect({});
    this.vignette = new VignetteEffect({ offset: 0.3, darkness: this.baseVignette });
    this.grain = new NoiseEffect({ blendFunction: BlendFunction.OVERLAY, premultiply: true });
    const toneMapping = new ToneMappingEffect({ mode: ToneMappingMode.ACES_FILMIC });

    this.composer.addPass(this.renderPass);
    this.seam = new SeamEffect(QUALITY.msaaSamples);
    this.seamPass = new EffectPass(camera, this.seam);
    this.seamPass.enabled = false;
    this.composer.addPass(this.seamPass);
    // FXAA is the MSAA replacement on devices where multisampling is too costly.
    if (QUALITY.fxaa) {
      this.composer.addPass(new EffectPass(camera, new FxaaEffect()));
    }
    this.dofPass = new EffectPass(camera, this.dof);
    this.composer.addPass(this.dofPass);
    this.composer.addPass(
      new EffectPass(
        camera,
        this.bloom,
        this.chromatic,
        this.brightnessContrast,
        this.hueSaturation,
        this.vignette,
        toneMapping,
        this.grain,
      ),
    );
    this.applyGrade();
  }

  setScene(scene: THREE.Scene) {
    this.renderPass.mainScene = scene;
  }

  setGrade(index: number) {
    this.setGradeBlend(index, index, 0);
  }

  /** Look between two levels (t = 0: `from`, 1: `to`), so a seamless dive never pops. */
  setGradeBlend(from: number, to: number, t: number) {
    const key = `${from}>${to}@${t.toFixed(3)}`;
    if (key === this.gradeKey) return;
    this.gradeKey = key;
    this.grade = t <= 0 ? resolveGrade(from) : lerpGrade(resolveGrade(from), resolveGrade(to), t);
    this.applyGrade();
  }

  private applyGrade() {
    const g = this.grade;
    // bloom / dof are driven per frame by the render loop; only statics live here.
    this.bloom.luminanceMaterial.threshold = g.threshold;
    this.brightnessContrast.brightness = g.brightness * 0.35;
    this.brightnessContrast.contrast = g.contrast * 0.6;
    this.hueSaturation.hue = hueRadians(g.hue);
    this.hueSaturation.saturation = g.saturation * 0.7;
    this.vignette.darkness = this.baseVignette * g.vignette;
    this.grain.blendMode.opacity.value = Math.min(0.35, this.baseGrain * g.grain * this.grainScale);
    const c = this.baseChromatic * g.chromatic * GRADE_BASE.chromatic * this.chromaticScale;
    this.chromatic.offset.set(c, c);
  }

  /** Re-apply after a debug multiplier changed. */
  refresh() {
    this.applyGrade();
  }

  setSize(w: number, h: number) {
    this.composer.setSize(w, h, false);
  }

  render(dt: number) {
    this.composer.render(dt);
  }
}
