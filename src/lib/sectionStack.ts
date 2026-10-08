import * as THREE from 'three';

/**
 * What a cut through a part looks like inside, for Section mode. Attach as
 * `mesh.userData.section`; without one the cut face is the part's own colour.
 * Layers are listed from `start` (a coordinate along `axis`, in the mesh's local units)
 * going along `axis`.
 */
export interface SectionStack {
  axis: [number, number, number];
  start: number;
  layers: { thickness: number; color: THREE.ColorRepresentation }[];
}

const COPPER = 0xc8743a;
const LAMINATE = 0x6b6a3c; // FR-4 / high-speed laminate, glass-fibre beige-olive
const MASK = 0x1f5a2c;

/**
 * A multilayer board of total `thickness`, top face at local y = top: solder mask, then
 * `copperLayers` copper planes (35 µm) separated by equal laminate, then solder mask.
 * 14 layers is typical for a flagship graphics card board.
 */
export function boardStack(thickness: number, top: number, copperLayers = 14): SectionStack {
  const mask = 0.02;
  const cu = 0.035;
  const dielectric = (thickness - 2 * mask - copperLayers * cu) / (copperLayers - 1);
  const layers: SectionStack['layers'] = [{ thickness: mask, color: MASK }];
  for (let i = 0; i < copperLayers; i++) {
    layers.push({ thickness: cu, color: COPPER });
    if (i < copperLayers - 1) layers.push({ thickness: dielectric, color: LAMINATE });
  }
  layers.push({ thickness: mask, color: MASK });
  return { axis: [0, -1, 0], start: -top, layers };
}

/** Same stack, scaled for a board drawn in other units (e.g. centimetres). */
export function scaleStack(stack: SectionStack, k: number): SectionStack {
  return {
    axis: stack.axis,
    start: stack.start * k,
    layers: stack.layers.map((l) => ({ thickness: l.thickness * k, color: l.color })),
  };
}
