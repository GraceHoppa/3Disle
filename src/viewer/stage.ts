import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js';
import { PostFX } from '../lib/PostFX';
import { fitDistance, measure } from '../models/frame';

/**
 * The 3D stage shared by the viewer and the editor: renderer, studio lighting, the descent's
 * post chain, an orbit camera that eases to a stop, camera glides and framing a model.
 * It renders only when something changes, so an idle page costs nothing.
 */
export function createStage(canvas: HTMLCanvasElement) {
  const renderer = new THREE.WebGLRenderer({
    canvas,
    antialias: false,
    stencil: false,
    powerPreference: 'high-performance',
  });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.toneMapping = THREE.NoToneMapping; // tone mapping happens in the effect chain
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  // Soft shadows from the key light: parts sitting on boards read as sitting, not floating.
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFShadowMap;

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x04070a);
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
  pmrem.dispose();
  scene.environmentIntensity = 0.6;
  // Key light from above and a little behind the camera, so flat glossy faces don't mirror it
  // straight back; a cool rim from behind picks out edges against the dark.
  const key = new THREE.DirectionalLight(0xffffff, 0.55);
  const rim = new THREE.DirectionalLight(0x9fc4ff, 0.7);
  const UP = new THREE.Vector3(0, 2, 0);
  key.castShadow = true;
  key.shadow.mapSize.set(2048, 2048);
  key.shadow.radius = 3; // soft edges
  scene.add(key, key.target, rim);
  /** Size of the shadowed region: set when a model is framed. */
  let shadowReach = 100;

  const camera = new THREE.PerspectiveCamera(40, 1, 0.1, 1000);
  const controls = new OrbitControls(camera, canvas);
  controls.enableDamping = true;
  controls.zoomToCursor = true; // the wheel zooms toward what you point at, not the model's centre

  // The same post chain as the descent (bloom, grade, vignette, ACES, grain), minus depth of field.
  const post = new PostFX(renderer, camera);
  post.setScene(scene);
  post.setGrade(0);
  post.dofPass.enabled = false;
  // Only really bright things bloom (glowing parts), not every shiny highlight on metal or glass.
  post.bloom.luminanceMaterial.threshold = 0.9;
  post.bloom.intensity = 0.8;

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
      // Key light from above and behind the camera, far enough out that its shadow covers the model.
      key.position
        .copy(camera.position)
        .sub(controls.target)
        .setY(0)
        .normalize()
        .add(UP)
        .setLength(shadowReach * 3)
        .add(controls.target);
      key.target.position.copy(controls.target);
      rim.position.copy(controls.target).sub(camera.position).add(controls.target).setY(camera.position.y);
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

  /** Size the key light's shadow box to a model, and let every mesh in it cast and receive. */
  function fitShadows(root: THREE.Object3D, radius: number) {
    shadowReach = radius;
    const cam = key.shadow.camera;
    cam.left = cam.bottom = -radius * 1.2;
    cam.right = cam.top = radius * 1.2;
    cam.near = radius * 0.5;
    cam.far = radius * 6;
    cam.updateProjectionMatrix();
    key.shadow.bias = -0.0004;
    key.shadow.normalBias = radius * 0.002;
    root.traverse((o) => {
      if ((o as THREE.Mesh).isMesh && !o.userData.sectionCap) o.castShadow = o.receiveShadow = true;
    });
  }

  /**
   * Put the camera on a three-quarter view that fits the whole model (narrower view first).
   * `from` scales the starting distance: 4 starts far away, 0.3 starts close, and the camera
   * then glides to the fitted view, which is how a dive arrives (and a climb back leaves).
   */
  function frame3q(root: THREE.Object3D, from = 1) {
    const { center, radius } = measure(root);
    fitShadows(root, radius);
    const hfov = THREE.MathUtils.radToDeg(
      2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(camera.fov) / 2) * camera.aspect),
    );
    const dist = fitDistance(radius, Math.min(camera.fov, hfov)) * 1.15;
    const dir = new THREE.Vector3(1, 0.7, 1.2).normalize();
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
