in vec3 vWorld, vNormal;
out vec4 fragColor;
uniform float uEmit;
uniform vec3 uLightPos;
void main() {
  if (uEmit > 0.0) {
    fragColor = vec4(vec3(uEmit), 1.0);
    return;
  }
  vec3 d = uLightPos - vWorld;
  float dist = length(d);
  float dotNL = max(dot(normalize(vNormal), d / dist), 0.0);
  float win = pow(clamp(1.0 - pow(dist / 28.0, 4.0), 0.0, 1.0), 2.0);
  float falloff = 900.0 / max(dist * dist, 0.01) * win;
  fragColor = vec4(vec3(dotNL * falloff * 0.3183098861837907), 1.0);
}
