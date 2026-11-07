in vec3 vWorld, vNormal;
in vec2 vUv;
in float vAlong, vLane, vTint;
out vec4 fragColor;
uniform vec3 uEye;
uniform int uMode;
uniform vec3 uColor, uEmissive, uGlow;
uniform float uMetal, uRough, uOpacity, uClearcoat, uEnvLods, uUvScale;
uniform sampler2D uMap, uMrMap;
uniform bool uHasMap, uHasMr;
uniform samplerCube uEnv;
uniform vec3 uKeyDir, uKeyColor, uRimDir, uRimColor, uHemiSky, uHemiGround, uPointPos, uPointColor;
uniform float uPointDist, uPointDecay;
uniform float uFlowA, uFlowB, uLevel, uSpeed, uPhase, uLength;
uniform int uWireMode;

const float PI = 3.141592653589793;
const float RECIP_PI = 0.3183098861837907;

float pow2(float x) { return x * x; }
vec3 fSchlick(vec3 f0, float f90, float dotVH) {
  float f = pow(1.0 - dotVH, 5.0);
  return f0 * (1.0 - f) + f90 * f;
}
float vSmith(float a, float dotNL, float dotNV) {
  float a2 = a * a;
  float gv = dotNL * sqrt(a2 + (1.0 - a2) * dotNV * dotNV);
  float gl = dotNV * sqrt(a2 + (1.0 - a2) * dotNL * dotNL);
  return 0.5 / max(gv + gl, 1e-6);
}
float dGGX(float a, float dotNH) {
  float a2 = a * a;
  float d = dotNH * dotNH * (a2 - 1.0) + 1.0;
  return RECIP_PI * a2 / (d * d);
}
vec3 brdfGGX(vec3 L, vec3 V, vec3 N, vec3 f0, float rough) {
  float a = rough * rough;
  vec3 H = normalize(L + V);
  float dotNL = clamp(dot(N, L), 0.0, 1.0), dotNV = clamp(dot(N, V), 0.0, 1.0);
  float dotNH = clamp(dot(N, H), 0.0, 1.0), dotVH = clamp(dot(V, H), 0.0, 1.0);
  return fSchlick(f0, 1.0, dotVH) * vSmith(a, dotNL, dotNV) * dGGX(a, dotNH);
}
vec2 dfgApprox(vec3 N, vec3 V, float rough) {
  float dotNV = clamp(dot(N, V), 0.0, 1.0);
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 r = rough * c0 + c1;
  float a004 = min(r.x * r.x, pow(2.0, -9.28 * dotNV)) * r.x + r.y;
  return vec2(-1.04, 1.04) * a004 + r.zw;
}
vec3 envSample(vec3 dir, float rough) {
  return textureLod(uEnv, dir, rough * uEnvLods).rgb;
}
float wireLit(float s) {
  float L = uLevel;
  if (uWireMode == 0) {
    float p = fract((s - uFlowA) / 10.0);
    return L * (0.3 + 1.1 * smoothstep(0.0, 0.12, p) * (1.0 - smoothstep(0.12, 0.55, p)));
  } else if (uWireMode == 1) {
    float w = sin(uFlowA + uPhase * 2.0944);
    float p = fract((s - sign(w) * uFlowB) / 8.0);
    return L * abs(w) * (0.35 + 0.9 * smoothstep(0.0, 0.2, p) * (1.0 - smoothstep(0.2, 0.6, p)));
  } else if (uWireMode == 2) {
    float x = s - uFlowA;
    float p = fract(x / 16.0);
    float bit = step(0.45, fract(sin(floor(x / 1.6) * 12.9898) * 43758.5453));
    return L * (0.12 + 1.5 * step(p, 0.4) * (0.35 + 0.65 * bit));
  }
  float p = fract((s - uFlowA) / 3.0);
  return L * (0.3 + 0.6 * smoothstep(0.0, 0.5, p) * (1.0 - smoothstep(0.5, 1.0, p)));
}
void main() {
  if (uMode == 2) { // a bus
    float p = fract(vAlong * 2.0 - uFlowA + vLane);
    float pulse = smoothstep(0.0, 0.03, p) * (1.0 - smoothstep(0.03, 0.16, p));
    float idle = mix(0.12, 0.35, clamp(uLevel, 0.0, 1.0));
    fragColor = vec4(uGlow * (idle + pulse * 3.5 * uLevel * step(0.001, uSpeed)), uOpacity);
    return;
  }
  vec4 tex = uHasMap ? texture(uMap, vUv * uUvScale) : vec4(1.0);
  if (uMode == 3) { // a painted decal
    fragColor = vec4(tex.rgb, tex.a * uOpacity);
    return;
  }
  vec3 base = uColor * tex.rgb;
  if (vTint > 0.5) base *= vec3(0.92, 1.12, 0.86);
  vec3 N = normalize(vNormal);
  vec3 V = normalize(uEye - vWorld);
  vec3 dxy = max(abs(dFdx(N)), abs(dFdy(N)));
  float geo = max(max(dxy.x, dxy.y), dxy.z);
  vec2 mr = uHasMr ? texture(uMrMap, vUv * uUvScale).gb : vec2(1.0);
  float metal = uMetal * mr.y;
  float rough = min(max(uRough * mr.x, 0.0525) + geo, 1.0);
  vec3 diffuse = base * (1.0 - metal);
  vec3 f0 = mix(vec3(0.04), base, metal);

  vec3 outDiffuse = vec3(0.0), outSpec = vec3(0.0), ccSpec = vec3(0.0);
  float ccRough = min(0.0525 + geo, 1.0);
  // Direct lights: key, rim, the glow from below.
  for (int i = 0; i < 3; i++) {
    vec3 L, col;
    if (i == 0) { L = uKeyDir; col = uKeyColor; }
    else if (i == 1) { L = uRimDir; col = uRimColor; }
    else {
      vec3 d = uPointPos - vWorld;
      float dist = length(d);
      L = d / max(dist, 1e-5);
      float falloff = 1.0 / max(pow(dist, uPointDecay), 0.01);
      float window = pow2(clamp(1.0 - pow2(pow2(dist / uPointDist)), 0.0, 1.0));
      col = uPointColor * falloff * window;
    }
    float dotNL = clamp(dot(N, L), 0.0, 1.0);
    vec3 irr = dotNL * col;
    outDiffuse += irr * RECIP_PI * diffuse;
    outSpec += irr * brdfGGX(L, V, N, f0, rough);
    if (uClearcoat > 0.0) ccSpec += irr * brdfGGX(L, V, N, vec3(0.04), ccRough);
  }
  // Hemisphere.
  vec3 hemi = mix(uHemiGround, uHemiSky, 0.5 * dot(N, vec3(0.0, 1.0, 0.0)) + 0.5);
  outDiffuse += hemi * RECIP_PI * diffuse;
  // The room's reflections.
  vec3 irr = PI * envSample(N, 1.0);
  vec3 R = reflect(-V, N);
  vec3 radiance = envSample(normalize(mix(R, N, rough * rough)), rough);
  vec2 fab = dfgApprox(N, V, rough);
  vec3 fr = max(vec3(1.0 - rough), f0) - f0;
  vec3 kS = f0 + fr * pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 5.0);
  vec3 single = kS * fab.x + fab.y;
  float ems = 1.0 - (fab.x + fab.y);
  vec3 favg = f0 + (1.0 - f0) * 0.047619;
  vec3 multi = ems * (single * favg / (1.0 - ems * favg));
  vec3 total = single + multi;
  vec3 kd = diffuse * (1.0 - max(max(total.r, total.g), total.b));
  vec3 cosIrr = irr * RECIP_PI;
  outSpec += radiance * single + multi * cosIrr;
  outDiffuse += kd * cosIrr;

  vec3 emissive = uEmissive;
  if (uMode == 1) {
    float lit = wireLit(vAlong * uLength);
    emissive += uGlow * mix(0.3 * uLevel, lit, step(0.001, uSpeed)) * 1.4;
  }
  vec3 color = outDiffuse + outSpec + emissive;
  if (uClearcoat > 0.0) {
    vec3 ccRad = envSample(normalize(mix(R, N, ccRough * ccRough)), ccRough);
    vec2 ccFab = dfgApprox(N, V, ccRough);
    float ccF = 0.04 + 0.96 * pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 5.0);
    ccSpec += ccRad * (vec3(0.04) * ccFab.x + ccFab.y);
    color = color * (1.0 - uClearcoat * ccF) + ccSpec * uClearcoat;
  }
  fragColor = vec4(color, uOpacity);
}
