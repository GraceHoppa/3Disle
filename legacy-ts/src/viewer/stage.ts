import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js';
import { PostFX } from '../lib/PostFX';
import { QUALITY } from '../lib/quality';
import { fitDistance, measure } from '../models/frame';

/**
 * The 3D stage shared by the viewer and the editor: renderer, GPU → Atom's lighting and post
 * chain, an orbit camera that eases to a stop, camera glides and framing a model.
 * It renders only when something changes, so an idle page costs nothing.
 */
/** Direction the camera first looks at a model from: front, right, a little above. */
const VIEW = new THREE.Vector3(1, 0.7, 1.2).normalize();

export function createStage(canvas: HTMLCanvasElement) {
  const renderer = new THREE.WebGLRenderer({
    canvas,
    antialias: false,
    stencil: false,
    powerPreference: 'high-performance',
  });
  // Like GPU → Atom: render above 1x on low-density screens (fewer shimmering edges), capped.
  renderer.setPixelRatio(Math.min(Math.max(window.devicePixelRatio, QUALITY.supersample), QUALITY.maxPixelRatio));
  renderer.toneMapping = THREE.NoToneMapping; // tone mapping happens in the effect chain
  renderer.outputColorSpace = THREE.SRGBColorSpace;

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x04070a);
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
  pmrem.dispose();
  // GPU → Atom's card lighting (L1Card): a soft hemisphere, a white key from above, the green
  // rim from behind and a green glow from below. The lights stay put in the world, so shading
  // changes as you orbit; placeLights() scales them to each model.
  const hemi = new THREE.HemisphereLight(0xdfeee0, 0x050806, 0.35);
  const key = new THREE.DirectionalLight(0xffffff, 2.2);
  const rim = new THREE.DirectionalLight(0x76b900, 1.8);
  const under = new THREE.PointLight(0x9cff3a, 30, 60, 1.6);
  scene.add(hemi, key, key.target, rim, rim.target, under);

  const camera = new THREE.PerspectiveCamera(40, 1, 0.1, 1000);
  const controls = new OrbitControls(camera, canvas);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;
  controls.rotateSpeed = 0.7;
  controls.zoomSpeed = 0.9;
  controls.zoomToCursor = true; // the wheel zooms toward what you point at, not the model's centre

  // GPU → Atom's post chain and its first grade (bloom, grade, vignette, ACES, grain). Depth of
  // field is off: the reference also turns it off while you explore.
  const post = new PostFX(renderer, camera);
  post.setScene(scene);
  post.setGrade(0);
  post.dofPass.enabled = false;
  post.bloom.intensity = 1.1;

  /** Called every drawn frame; return true to keep drawing (an animation is running). */
  const tickers = new Set<(now: number) => boolean>();

  let frame = 0;
  let last = performance.now();
  const render = () => {
    if (frame) return;
    frame = requestAnimationFrame((now) => {
      frame = 0;
      let busy = fly(now);
      for (const t of tickers) busy = t(now) || busy;
      busy = controls.update() || busy;
      post.render(Math.min((now - last) / 1000, 0.1));
      last = now;
      if (busy) render();
    });
  };
  controls.addEventListener('change', render);

  /** A short eased glide of camera and orbit centre. */
  let flight: {
    t0: number;
    ms: number;
    from: [THREE.Vector3, THREE.Vector3];
    to: [THREE.Vector3, THREE.Vector3];
  } | null = null;
  let landed: (() => void) | null = null;
  function flyTo(position: THREE.Vector3, target: THREE.Vector3, ms = 700) {
    landed?.();
    landed = null;
    flight = {
      t0: performance.now(),
      ms,
      from: [camera.position.clone(), controls.target.clone()],
      to: [position, target],
    };
    render();
    return new Promise<void>((resolve) => (landed = resolve));
  }
  function fly(now: number) {
    if (!flight) return false;
    const k = Math.min((now - flight.t0) / flight.ms, 1);
    const e = k < 0.5 ? 4 * k * k * k : 1 - (-2 * k + 2) ** 3 / 2;
    camera.position.lerpVectors(flight.from[0], flight.to[0], e);
    controls.target.lerpVectors(flight.from[1], flight.to[1], e);
    if (k === 1) {
      flight = null;
      landed?.();
      landed = null;
    }
    return true;
  }

  /**
   * The reference lights its card for a camera looking from (-30, 22, 40); ours looks along
   * VIEW. Turning the rig by the difference keeps the rim behind the model instead of glaring
   * off the top toward the camera.
   */
  const Y = new THREE.Vector3(0, 1, 0);
  const RIG_TURN = Math.atan2(VIEW.x, VIEW.z) - Math.atan2(-30, 40);

  /**
   * Scale the lights to a model. GPU → Atom lights a card about 17 units in radius; positions,
   * the glow's reach and its strength (it falls off with distance^1.6) scale with the model.
   */
  function placeLights(center: THREE.Vector3, radius: number) {
    const s = radius / 17;
    const at = (x: number, y: number, z: number) =>
      new THREE.Vector3(x, y, z).applyAxisAngle(Y, RIG_TURN).multiplyScalar(s).add(center);
    key.position.copy(at(20, 40, 25));
    key.target.position.copy(center);
    rim.position.copy(at(-30, 10, -30));
    rim.target.position.copy(center);
    under.position.copy(at(0, -6, 6));
    under.distance = 60 * s;
    under.intensity = 30 * Math.pow(s, 1.6);
  }

  /**
   * Put the camera on a three-quarter view that fits the whole model (narrower view first).
   * `from` scales the starting distance: 4 starts far away, 0.3 starts close, and the camera
   * then glides to the fitted view, which is how a dive arrives (and a climb back leaves).
   */
  function frame3q(root: THREE.Object3D, from = 1) {
    const { center, radius } = measure(root);
    placeLights(center, radius);
    const hfov = THREE.MathUtils.radToDeg(
      2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(camera.fov) / 2) * camera.aspect),
    );
    const dist = fitDistance(radius, Math.min(camera.fov, hfov)) * 1.15;
    const dir = VIEW;
    camera.near = radius / 100;
    camera.far = radius * 100 * Math.max(1, from);
    camera.updateProjectionMatrix();
    controls.target.copy(center);
    camera.position.copy(center).addScaledVector(dir, dist * from);
    controls.update();
    if (from !== 1) return flyTo(center.clone().addScaledVector(dir, dist), center.clone(), 1100);
    render();
    return Promise.resolve();
  }

  function resize() {
    renderer.setSize(window.innerWidth, window.innerHeight, false);
    post.setSize(window.innerWidth, window.innerHeight);
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    render();
  }
  window.addEventListener('resize', resize);
  resize();

  return { renderer, scene, camera, controls, post, render, flyTo, frame3q, tickers };
}

export type Stage = ReturnType<typeof createStage>;
