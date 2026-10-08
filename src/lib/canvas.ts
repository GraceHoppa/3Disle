import * as THREE from 'three';

/** Draw into an offscreen canvas and wrap it as a texture. */
export function canvasTexture(
  width: number,
  height: number,
  draw: (g: CanvasRenderingContext2D, w: number, h: number) => void,
  opts: { srgb?: boolean; anisotropy?: number; repeat?: [number, number] } = {},
) {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const g = canvas.getContext('2d')!;
  draw(g, width, height);
  const tex = new THREE.CanvasTexture(canvas);
  tex.colorSpace = opts.srgb === false ? THREE.NoColorSpace : THREE.SRGBColorSpace;
  tex.anisotropy = opts.anisotropy ?? 8;
  if (opts.repeat) {
    tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
    tex.repeat.set(...opts.repeat);
  }
  return tex;
}

/** Route a polyline with 45° bends from a to b (orthogonal-diagonal-orthogonal), PCB style. */
export function route45(ax: number, ay: number, bx: number, by: number, bias = 0.5): [number, number][] {
  const dx = bx - ax;
  const dy = by - ay;
  if (Math.abs(dx) > Math.abs(dy)) {
    const diag = Math.abs(dy);
    const straight = Math.abs(dx) - diag;
    const s1 = straight * bias;
    const x1 = ax + Math.sign(dx) * s1;
    const x2 = x1 + Math.sign(dx) * diag;
    return [
      [ax, ay],
      [x1, ay],
      [x2, by],
      [bx, by],
    ];
  }
  const diag = Math.abs(dx);
  const straight = Math.abs(dy) - diag;
  const s1 = straight * bias;
  const y1 = ay + Math.sign(dy) * s1;
  const y2 = y1 + Math.sign(dy) * diag;
  return [
    [ax, ay],
    [ax, y1],
    [bx, y2],
    [bx, by],
  ];
}
