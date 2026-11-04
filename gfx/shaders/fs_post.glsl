in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex, uBloom;
uniform vec2 uChromatic;
uniform vec3 uHue;
uniform float uBloomAmt, uContrast, uBrightness, uSaturation, uVignette, uVigOffset, uGrain, uTime, uFade;
const float PI = 3.141592653589793;
float rand(vec2 uv) {
  float dt = dot(uv, vec2(12.9898, 78.233));
  return fract(sin(mod(dt, PI)) * 43758.5453);
}
vec3 RRT(vec3 v) {
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return a / b;
}
vec3 aces(vec3 c) {
  const mat3 I = mat3(vec3(0.59719, 0.07600, 0.02840), vec3(0.35458, 0.90834, 0.13383), vec3(0.04823, 0.01566, 0.83777));
  const mat3 O = mat3(vec3(1.60475, -0.10208, -0.00327), vec3(-0.53108, 1.10813, -0.07276), vec3(-0.07367, -0.00605, 1.07602));
  c *= 1.0 / 0.6;
  c = I * c;
  c = RRT(c);
  c = O * c;
  return clamp(c, 0.0, 1.0);
}
vec3 toSrgb(vec3 c) {
  return mix(pow(c, vec3(0.41666)) * 1.055 - 0.055, c * 12.92, step(c, vec3(0.0031308)));
}
void main() {
  vec3 color = texture(uTex, vUv).rgb;
  // Red and blue split apart away from the centre.
  float d = max(distance(vUv, vec2(0.5)) * 2.0 - 0.3, 0.0);
  if (d > 0.0) {
    color.r = texture(uTex, mix(vUv, vUv - uChromatic, d)).r;
    color.b = texture(uTex, mix(vUv, vUv + uChromatic, d)).b;
  }
  vec3 src = texture(uBloom, vUv).rgb * uBloomAmt;
  color = color + src - min(color * src, vec3(1.0));
  color = color + vec3(uBrightness - 0.5);
  if (uContrast > 0.0) color /= vec3(1.0 - uContrast);
  else color *= vec3(1.0 + uContrast);
  color += 0.5;
  vec3 h = vec3(dot(color, uHue.xyz), dot(color, uHue.zxy), dot(color, uHue.yzx));
  float avg = (h.r + h.g + h.b) / 3.0;
  vec3 diff = avg - h;
  if (uSaturation > 0.0) h += diff * (1.0 - 1.0 / (1.001 - uSaturation));
  else h += diff * -uSaturation;
  color = min(h, vec3(1.0));
  float dv = distance(vUv, vec2(0.5));
  color *= smoothstep(0.8, uVigOffset * 0.799, dv * (uVignette + uVigOffset));
  color = aces(max(color, vec3(0.0)));
  vec3 noise = min(color * vec3(rand(vUv * (1.0 + uTime))), vec3(1.0));
  vec3 a = 2.0 * noise * color, b = 1.0 - 2.0 * (1.0 - noise) * (1.0 - color);
  color = mix(color, mix(a, b, step(0.5, color)), uGrain);
  color *= 1.0 - uFade;
  fragColor = vec4(toSrgb(max(color, vec3(0.0))), 1.0);
}
