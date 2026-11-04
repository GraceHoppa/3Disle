layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uViewProj, uModel;
uniform float uFlip;
out vec3 vWorld, vNormal;
void main() {
  vec4 w = uModel * vec4(aPos, 1.0);
  vWorld = w.xyz;
  vNormal = uFlip * transpose(inverse(mat3(uModel))) * aNormal;
  gl_Position = uViewProj * w;
}
