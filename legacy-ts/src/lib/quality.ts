/**
 * Device-tiered quality. The viewer has to hold 60 fps from a phone to a workstation,
 * so instead of one hard-coded setting we probe the device once and derive a coherent
 * preset: how much resolution, how much AA, which post effects, and how aggressively
 * we pre-build and cache the next scale.
 *
 * `?quality=low|high` still forces a tier (handy for screenshots and CI).
 */
export type QualityTier = 'high' | 'medium' | 'low';

export interface Quality {
  tier: QualityTier;
  /** Hard cap on device pixel ratio. */
  maxPixelRatio: number;
  /** Render at least this many pixels per CSS pixel on 1x screens (supersampling kills shimmer). */
  supersample: number;
  /** Adaptive resolution may step below the cap when the frame rate drops. */
  adaptive: boolean;
  /** MSAA samples on the composer's render target (0 disables). */
  msaaSamples: number;
  /** Post-process FXAA — the fallback when MSAA is too expensive. */
  fxaa: boolean;
  dof: boolean;
  bloom: boolean;
  /** Film grain opacity (0 disables). */
  grain: number;
  /** Chromatic aberration strength multiplier (0 disables). */
  chromatic: number;
  vignette: number;
  /**
   * Levels kept built and warmed up on each side of the current one (1 = previous + next;
   * 0 = only the current level, built on arrival). `?cache=0` forces 0.
   */
  levelCache: number;
  /** Scales per-point sprite size caps in the heaviest levels. */
  pointBudget: number;
  /** Respect the OS "reduce motion" setting: no parallax, no autoplay. */
  reducedMotion: boolean;
}

const forced = new URLSearchParams(location.search).get('quality');

function probeTier(): QualityTier {
  if (forced === 'low' || forced === 'medium' || forced === 'high') return forced;
  const cores = navigator.hardwareConcurrency || 4;
  const memory = (navigator as Navigator & { deviceMemory?: number }).deviceMemory ?? 4;
  const coarse = window.matchMedia('(pointer: coarse)').matches;
  const small = Math.min(window.innerWidth, window.innerHeight) <= 520;
  const mobile = coarse && small;
  if (mobile) return cores >= 8 && memory >= 6 ? 'medium' : 'low';
  if (cores >= 8 && memory >= 8) return 'high';
  if (cores >= 4) return 'medium';
  return 'low';
}

const PRESETS: Record<QualityTier, Omit<Quality, 'tier' | 'reducedMotion'>> = {
  high: {
    maxPixelRatio: 1.75,
    supersample: 1.5,
    adaptive: true,
    msaaSamples: 4,
    fxaa: false,
    dof: true,
    bloom: true,
    grain: 0.1,
    chromatic: 1,
    vignette: 1,
    levelCache: 1,
    pointBudget: 1,
  },
  medium: {
    maxPixelRatio: 1.5,
    supersample: 1.25,
    adaptive: true,
    msaaSamples: 2,
    fxaa: false,
    dof: true,
    bloom: true,
    grain: 0.08,
    chromatic: 0.7,
    vignette: 1,
    levelCache: 1,
    pointBudget: 0.8,
  },
  low: {
    maxPixelRatio: 1,
    supersample: 1,
    adaptive: true,
    msaaSamples: 0,
    fxaa: true,
    dof: false,
    bloom: true,
    grain: 0.05,
    chromatic: 0.35,
    vignette: 0.9,
    levelCache: 0,
    pointBudget: 0.55,
  },
};

export const QUALITY: Quality = (() => {
  const tier = probeTier();
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const q: Quality = { tier, reducedMotion, ...PRESETS[tier] };
  // Forced high is for screenshots: full resolution, no adaptation. Auto-high stays adaptive.
  if (new URLSearchParams(location.search).get('cache') === '0') q.levelCache = 0;
  if (forced === 'high') {
    q.maxPixelRatio = 2;
    q.adaptive = false;
  }
  return q;
})();

/** A short human label for the debug HUD, e.g. "high · 4× MSAA · cache 2". */
export function qualityLabel(q: Quality = QUALITY) {
  const aa = q.msaaSamples ? `${q.msaaSamples}× MSAA` : q.fxaa ? 'FXAA' : 'no AA';
  return `${q.tier} · ${aa} · cache ±${q.levelCache}`;
}
