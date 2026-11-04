in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uTexel;
float border(vec2 uv) { return float(uv.s >= 0.0 && uv.s <= 1.0 && uv.t >= 0.0 && uv.t <= 1.0); }
vec4 tap(vec2 o, float w) {
  vec2 uv = vUv + uTexel * o;
  return w * border(uv) * texture(uTex, uv);
}
void main() {
  const float I = 0.125, O = 0.05556;
  vec4 c = tap(vec2(-1, 1), I) + tap(vec2(1, 1), I) + tap(vec2(-1, -1), I) + tap(vec2(1, -1), I);
  c += tap(vec2(-2, 2), O) + tap(vec2(0, 2), O) + tap(vec2(2, 2), O) + tap(vec2(-2, 0), O);
  c += tap(vec2(2, 0), O) + tap(vec2(-2, -2), O) + tap(vec2(0, -2), O) + tap(vec2(2, -2), O);
  c += O * texture(uTex, vUv);
  fragColor = c;
}
