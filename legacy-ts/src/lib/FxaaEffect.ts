import * as THREE from 'three';
import { BlendFunction, Effect } from 'postprocessing';

/**
 * A compact FXAA3 (the "quality lite" variant) written as a postprocessing effect.
 *
 * It exists because MSAA is unaffordable on the low-end mobile preset and postprocessing's
 * bundled SMAA carries ~60 KB of embedded search/area textures that every visitor would
 * download. This is ~40 lines of GLSL and no assets. FXAA is not as clean as MSAA on thin
 * silicon traces, but it costs one cheap pass over the colour buffer.
 */
const fragmentShader = /* glsl */ `
uniform vec2 texelSize;

float luma(vec3 c) {
  return dot(c, vec3(0.299, 0.587, 0.114));
}

void mainImage(const in vec4 inputColor, const in vec2 uv, out vec4 outputColor) {
  vec3 rgbM  = inputColor.rgb;
  vec3 rgbNW = texture2D(inputBuffer, uv + vec2(-1.0, -1.0) * texelSize).rgb;
  vec3 rgbNE = texture2D(inputBuffer, uv + vec2( 1.0, -1.0) * texelSize).rgb;
  vec3 rgbSW = texture2D(inputBuffer, uv + vec2(-1.0,  1.0) * texelSize).rgb;
  vec3 rgbSE = texture2D(inputBuffer, uv + vec2( 1.0,  1.0) * texelSize).rgb;

  float lM  = luma(rgbM);
  float lNW = luma(rgbNW), lNE = luma(rgbNE), lSW = luma(rgbSW), lSE = luma(rgbSE);
  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
  float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

  // Flat area: nothing to antialias.
  if (lMax - lMin < 0.06 * lMax + 0.015) {
    outputColor = inputColor;
    return;
  }

  vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
  float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);
  float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
  dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * texelSize;

  vec3 rgbA = 0.5 * (
    texture2D(inputBuffer, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
    texture2D(inputBuffer, uv + dir * (2.0 / 3.0 - 0.5)).rgb
  );
  vec3 rgbB = rgbA * 0.5 + 0.25 * (
    texture2D(inputBuffer, uv + dir * -0.5).rgb +
    texture2D(inputBuffer, uv + dir * 0.5).rgb
  );

  float lB = luma(rgbB);
  outputColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, inputColor.a);
}
`;

export class FxaaEffect extends Effect {
  private texelSize = new THREE.Vector2(1 / 1920, 1 / 1080);

  constructor() {
    super('FxaaEffect', fragmentShader, {
      blendFunction: BlendFunction.NORMAL,
      uniforms: new Map<string, THREE.Uniform>([['texelSize', new THREE.Uniform(new THREE.Vector2())]]),
    });
  }

  /** postprocessing calls this whenever the pass resolution changes. */
  setSize(width: number, height: number) {
    this.texelSize.set(1 / width, 1 / height);
    (this.uniforms.get('texelSize') as THREE.Uniform<THREE.Vector2>).value.copy(this.texelSize);
  }
}
