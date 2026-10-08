/**
 * Page logic without code: controls the visitor moves (sliders the author puts on the page) and
 * values that follow them through simple rules. A rule maps a control's value through a table of
 * points, straight lines between them, like a datasheet curve: { by: "throttle", map: [[0, 0],
 * [100, 30000]] } turns 0–100 % throttle into 0–30 000 rpm.
 */
export interface ControlSpec {
  /** What rules refer to, e.g. "throttle". */
  id: string;
  label: string;
  min?: number;
  max?: number;
  step?: number;
  /** Starting value (default: min). */
  value?: number;
  unit?: string;
  info?: string;
}

export interface Rule {
  /** The control this value follows. */
  by: string;
  /** [control value, result] points in rising control order; flat beyond the ends. */
  map: [number, number][];
}

/** A number, or a rule that computes it from a control. */
export type Value = number | Rule;

/** A live number shown with the controls, e.g. "Total current ≈ 38 A". */
export interface ReadoutSpec extends Rule {
  label: string;
  unit?: string;
  /** Decimal places (default 0). */
  digits?: number;
}

export type Values = Record<string, number>;

export const isRule = (v: unknown): v is Rule =>
  !!v && typeof v === 'object' && typeof (v as Rule).by === 'string' && Array.isArray((v as Rule).map);

/** Look x up in a table of points: straight lines between them, flat beyond the ends. */
export function lookup(map: [number, number][], x: number) {
  if (!map.length || !Number.isFinite(x)) return 0;
  if (x <= map[0][0]) return map[0][1];
  for (let i = 1; i < map.length; i++) {
    const [x0, y0] = map[i - 1];
    const [x1, y1] = map[i];
    if (x <= x1) return x1 === x0 ? y1 : y0 + ((y1 - y0) * (x - x0)) / (x1 - x0);
  }
  return map[map.length - 1][1];
}

/** A value now: a plain number as is, a rule from its control (missing control: its value at 0). */
export function evaluate(v: Value | undefined, values: Values, fallback = 0) {
  if (v === undefined) return fallback;
  if (typeof v === 'number') return v;
  if (!isRule(v)) return fallback;
  return lookup(v.map, values[v.by] ?? 0);
}

/** Every control at its starting value, keeping values already set (e.g. carried from the level above). */
export function startValues(controls: ControlSpec[] = [], keep: Values = {}): Values {
  const out: Values = {};
  for (const c of controls) {
    const min = c.min ?? 0;
    const max = c.max ?? 100;
    const v = keep[c.id] ?? c.value ?? min;
    out[c.id] = Math.min(max, Math.max(min, v));
  }
  return out;
}

/** "38 A", "21 400 rpm": thin-space thousands, the unit after a space. */
export function formatReadout(r: Pick<ReadoutSpec, 'unit' | 'digits'>, value: number) {
  const digits = r.digits ?? 0;
  const [int, frac] = Math.abs(value).toFixed(digits).split('.');
  const grouped = int.replace(/\B(?=(\d{3})+(?!\d))/g, ' ');
  const text = `${value < 0 ? '−' : ''}${grouped}${frac ? '.' + frac : ''}`;
  return r.unit ? `${text} ${r.unit}` : text;
}
