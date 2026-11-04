layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
layout(location=3) in float aAlong;
layout(location=4) in float aLane;
layout(location=5) in vec4 aInst0;
layout(location=6) in vec4 aInst1;
layout(location=7) in vec4 aInst2;
layout(location=8) in vec4 aInst3;
uniform mat4 uViewProj, uModel;
uniform int uTintCopy;
out vec3 vWorld, vNormal;
out vec2 vUv;
out float vAlong, vLane, vTint;
void main() {
  mat4 inst = mat4(aInst0, aInst1, aInst2, aInst3);
  vec4 w = uModel * inst * vec4(aPos, 1.0);
  vWorld = w.xyz;
  vNormal = mat3(uModel) * mat3(inst) * aNormal;
  vUv = aUv;
  vAlong = aAlong;
  vLane = aLane;
  vTint = gl_InstanceID == uTintCopy ? 1.0 : 0.0;
  gl_Position = uViewProj * w;
}
