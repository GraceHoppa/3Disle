in vec2 vUv;
out vec4 fragColor;
uniform samplerCube uSrc;
uniform int uFace;
uniform float uRough, uSrcSize;
const float PI = 3.141592653589793;
vec3 faceDir(int f, vec2 uv) {
  if (f == 0) return vec3(1.0, -uv.y, -uv.x);
  if (f == 1) return vec3(-1.0, -uv.y, uv.x);
  if (f == 2) return vec3(uv.x, 1.0, uv.y);
  if (f == 3) return vec3(uv.x, -1.0, -uv.y);
  if (f == 4) return vec3(uv.x, -uv.y, 1.0);
  return vec3(-uv.x, -uv.y, -1.0);
}
float radInv(uint b) {
  b = (b << 16u) | (b >> 16u);
  b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
  b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
  b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
  b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
  return float(b) * 2.3283064365386963e-10;
}
void main() {
  vec3 N = normalize(faceDir(uFace, vUv));
  vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
  vec3 T = normalize(cross(up, N)), B = cross(N, T);
  float a = uRough * uRough, sum = 0.0;
  vec3 col = vec3(0.0);
  const uint COUNT = 256u;
  for (uint i = 0u; i < COUNT; i++) {
    vec2 Xi = vec2(float(i) / float(COUNT), radInv(i));
    float ph = 2.0 * PI * Xi.x;
    float ct = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float st = sqrt(1.0 - ct * ct);
    vec3 H = normalize(T * (cos(ph) * st) + B * (sin(ph) * st) + N * ct);
    vec3 L = normalize(2.0 * dot(N, H) * H - N);
    float nl = dot(N, L);
    if (nl <= 0.0) continue;
    float nh = max(dot(N, H), 0.0);
    float d = (nh * nh * (a * a - 1.0) + 1.0);
    float D = a * a / (PI * d * d);
    float pdf = D * nh / (4.0 * max(nh, 1e-4)) + 1e-4;
    float saTexel = 4.0 * PI / (6.0 * uSrcSize * uSrcSize);
    float saSample = 1.0 / (float(COUNT) * pdf + 1e-4);
    float lod = uRough == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);
    col += textureLod(uSrc, L, max(lod, 0.0)).rgb * nl;
    sum += nl;
  }
  fragColor = vec4(col / max(sum, 1e-4), 1.0);
}
