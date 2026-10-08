import * as THREE from 'three';

/**
 * See-through and flat-shaded stand-in materials, shared by the descent's view modes and the
 * viewer: 'xray' is an additive Fresnel ghost, 'ghost' a faint white one, 'thermal' a solid
 * colour (set uColor) shaded by facing. All respect clipping planes.
 */
export function makeShellMaterial(kind: 'xray' | 'thermal' | 'ghost') {
  const xray = kind === 'xray';
  const see = kind !== 'thermal';
  return new THREE.ShaderMaterial({
    uniforms: { uColor: { value: new THREE.Color(xray ? 0x9cff3a : 0xffffff) } },
    transparent: see,
    depthWrite: !see,
    blending: xray ? THREE.AdditiveBlending : THREE.NormalBlending,
    side: see ? THREE.DoubleSide : THREE.FrontSide,
    clipping: true,
    vertexShader: /* glsl */ `
      #include <common>
      #include <clipping_planes_pars_vertex>
      varying vec3 vN;
      varying vec3 vV;
      void main() {
        #include <beginnormal_vertex>
        #include <defaultnormal_vertex>
        #include <begin_vertex>
        #include <project_vertex>
        #include <clipping_planes_vertex>
        vN = normalize(transformedNormal);
        vV = -mvPosition.xyz;
      }
    `,
    fragmentShader: see
      ? /* glsl */ `
      #include <clipping_planes_pars_fragment>
      uniform vec3 uColor;
      varying vec3 vN;
      varying vec3 vV;
      void main() {
        #include <clipping_planes_fragment>
        float f = 1.0 - abs(dot(normalize(vN), normalize(vV)));
        ${xray ? 'float a = pow(f, 2.2) * 0.85 + 0.035;' : 'float a = pow(f, 2.0) * 0.55 + 0.1;'}
        gl_FragColor = vec4(uColor * ${xray ? 'a' : '1.0'}, a);
      }`
      : /* glsl */ `
      #include <clipping_planes_pars_fragment>
      uniform vec3 uColor;
      varying vec3 vN;
      varying vec3 vV;
      void main() {
        #include <clipping_planes_fragment>
        float ndv = abs(dot(normalize(vN), normalize(vV)));
        vec3 c = uColor * (0.55 + 0.45 * ndv);
        // hot parts glow a little (bloom picks it up)
        float heat = max(max(uColor.r, uColor.g), uColor.b);
        gl_FragColor = vec4(c * (1.0 + 0.6 * smoothstep(0.85, 1.0, uColor.g)) * mix(1.0, 1.15, heat), 1.0);
      }`,
  });
}
