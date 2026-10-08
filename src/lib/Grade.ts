import { QUALITY } from './quality';

/**
 * Per-level colour grade. Everything is a small delta from the neutral look, so a level
 * only declares what makes it different: the SEM level is near-monochrome and grainy, the
 * die is saturated so the thin-film interference reads, the atom is dark and bloomy.
 *
 * Indexed by level order (names are translatable) so levels stay free of post-processing concerns.
 */
export interface Grade {
  /** Bloom intensity multiplier on top of the tier default. */
  bloom?: number;
  /** Bloom luminance threshold (lower = more of the scene glows). */
  threshold?: number;
  /** -1..1 */
  brightness?: number;
  /** -1..1 */
  contrast?: number;
  /** -1..1 (=-1 grayscale) */
  saturation?: number;
  /** Hue rotation in degrees. */
  hue?: number;
  /** Multiplier on the tier grain amount. */
  grain?: number;
  /** Multiplier on the tier chromatic aberration amount. */
  chromatic?: number;
  /** Multiplier on the tier vignette amount. */
  vignette?: number;
}

export const NEUTRAL: Required<Grade> = {
  bloom: 1,
  threshold: 0.62,
  brightness: 0,
  contrast: 0,
  saturation: 0,
  hue: 0,
  grain: 1,
  chromatic: 1,
  vignette: 1,
};

/** Hand-tuned looks by level index, from the product shot down to the atom. */
export const GRADES: Grade[] = [
  { contrast: 0.08, saturation: 0.04, grain: 0.8, bloom: 1.0, chromatic: 1.1 }, // GeForce RTX 5090
  { contrast: 0.06, saturation: 0.1, hue: -4, grain: 1.0, bloom: 1.05 }, // Main PCB
  { contrast: 0.1, saturation: -0.08, hue: -6, grain: 1.0, bloom: 0.9 }, // GB202 package
  { contrast: 0.12, saturation: 0.28, hue: 6, grain: 0.9, bloom: 1.15, threshold: 0.55 }, // GB202 die
  { contrast: 0.24, saturation: -0.55, grain: 1.7, bloom: 0.85, vignette: 1.25, chromatic: 0.6 }, // Metal stack
  { contrast: 0.1, saturation: 0.14, hue: -10, grain: 1.0, bloom: 1.2, threshold: 0.5 }, // FinFET transistors
  { contrast: 0.08, saturation: 0.2, hue: 12, grain: 0.7, bloom: 1.3, threshold: 0.42 }, // Silicon lattice
  { contrast: 0.14, saturation: 0.06, hue: -6, grain: 0.55, bloom: 1.35, threshold: 0.35, chromatic: 1.3 }, // Silicon atom
];

/** Merge a level's grade with the neutral defaults. */
export function resolveGrade(index: number): Required<Grade> {
  return { ...NEUTRAL, ...(GRADES[index] ?? {}) };
}

/** Per-field linear blend of two resolved grades. */
export function lerpGrade(a: Required<Grade>, b: Required<Grade>, t: number): Required<Grade> {
  const out = { ...a };
  for (const k of Object.keys(a) as (keyof Grade)[]) out[k] = a[k] + (b[k] - a[k]) * t;
  return out;
}

/** The post-processing baseline the grade multiplies: from the device tier. */
export const GRADE_BASE = {
  grain: QUALITY.grain,
  chromatic: QUALITY.chromatic,
  vignette: QUALITY.vignette,
};

/** Convert a hue offset in degrees to the radians the effect wants. */
export function hueRadians(deg: number) {
  return (deg * Math.PI) / 180;
}
