in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform float uThreshold, uSmoothing;
void main() {
  vec4 t = texture(uTex, vUv);
  float l = dot(t.rgb, vec3(0.2126, 0.7152, 0.0722));
  fragColor = t * smoothstep(uThreshold, uThreshold + uSmoothing, l);
}
