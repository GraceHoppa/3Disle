in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex, uBase;
uniform vec2 uTexel;
uniform float uRadius;
void main() {
  vec4 c = vec4(0.0);
  c += texture(uTex, vUv + uTexel * vec2(-1, 1)) * 0.0625;
  c += texture(uTex, vUv + uTexel * vec2(0, 1)) * 0.125;
  c += texture(uTex, vUv + uTexel * vec2(1, 1)) * 0.0625;
  c += texture(uTex, vUv + uTexel * vec2(-1, 0)) * 0.125;
  c += texture(uTex, vUv) * 0.25;
  c += texture(uTex, vUv + uTexel * vec2(1, 0)) * 0.125;
  c += texture(uTex, vUv + uTexel * vec2(-1, -1)) * 0.0625;
  c += texture(uTex, vUv + uTexel * vec2(0, -1)) * 0.125;
  c += texture(uTex, vUv + uTexel * vec2(1, -1)) * 0.0625;
  fragColor = mix(texture(uBase, vUv), c, uRadius);
}
