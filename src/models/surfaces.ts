import { route45 } from '../lib/canvas';
import { mulberry32 } from '../lib/random';

/**
 * Procedural slab finish. `pcb` follows the descent board, `carbon` is a twill weave, `brushed` is
 * light metal, `traces` is a copper routing layer on dark substrate (a package's build-up layers),
 * `laminate` is glass-fibre core.
 */
export type SurfaceKind = 'pcb' | 'carbon' | 'brushed' | 'traces' | 'laminate';

/** One part's outline on a slab. Millimetres, centre-relative, the same frame as `PartSpec.at`. */
export interface Footprint {
  name: string;
  /** Centre, relative to the slab centre. */
  x: number;
  z: number;
  /** Width along x and depth along z. */
  w: number;
  d: number;
  /** false: an anchor for buses only (a whole row of parts), no silkscreen outline. */
  silk?: boolean;
}

export interface SurfaceOptions {
  kind: SurfaceKind;
  /** Slab size in millimetres. */
  width: number;
  depth: number;
  footprints?: Footprint[];
  /** Same seed redraws the same texture. */
  seed?: number;
  /** Base colour of the finish (solder mask, resin, metal), e.g. '#121417' for a black board. */
  base?: string;
  /** Pixels per millimetre before the canvas clamp. Default 4. */
  pxPerMm?: number;
}

const MAX_PX = 2048;
const MIN_PX = 64;

/** The descent paints this many traces and vias on a board about 240 x 110 mm. */
const REF_AREA = 240 * 110;
const REF_TRACES = 300;
const REF_VIAS = 900;
/** Stop well short of a huge slab issuing hundreds of thousands of strokes. */
const TRACE_CAP = REF_TRACES * 4;
const VIA_CAP = REF_VIAS * 4;

type Rng = () => number;

/** `hex` moved toward white (k > 0) or black (k < 0), as an rgba() string. */
export function shade(hex: string, k: number, alpha = 1) {
  const n = parseInt(hex.replace('#', ''), 16);
  const ch = [(n >> 16) & 255, (n >> 8) & 255, n & 255].map((c) =>
    Math.round(k >= 0 ? c + (255 - c) * k : c * (1 + k)),
  );
  return `rgba(${ch[0]}, ${ch[1]}, ${ch[2]}, ${alpha})`;
}

/**
 * Pixel size of a slab texture. `width * pxPerMm` by `depth * pxPerMm`, with the long side
 * held inside 64..2048 and the aspect kept. Pixel sizes are integers.
 */
export function surfaceSize(width: number, depth: number, pxPerMm = 4): { width: number; height: number } {
  const w0 = Math.abs(width) * pxPerMm;
  const h0 = Math.abs(depth) * pxPerMm;
  const long = Math.max(w0, h0);
  if (!(long > 0) || !Number.isFinite(long)) return { width: MIN_PX, height: MIN_PX };

  const target = long > MAX_PX ? MAX_PX : long < MIN_PX ? MIN_PX : 0;
  const scale = target ? target / long : 1;
  if (w0 === h0) {
    const side = target || Math.max(1, Math.round(w0));
    return { width: side, height: side };
  }
  let pw = Math.max(1, Math.round(w0 * scale));
  let ph = Math.max(1, Math.round(h0 * scale));
  if (target) {
    if (w0 > h0) pw = target;
    else ph = target;
  }
  return { width: pw, height: ph };
}

/**
 * Paint a procedural surface into `g`. +x is right, +z is down, and the slab centre is the
 * canvas centre. Size the canvas to the returned pixels first; drawing uses that size.
 */
export function paintSurface(g: CanvasRenderingContext2D, opts: SurfaceOptions): { width: number; height: number } {
  const boardW = Math.abs(opts.width);
  const boardD = Math.abs(opts.depth);
  const px = surfaceSize(boardW, boardD, opts.pxPerMm);
  const rng = mulberry32(opts.seed ?? 1);
  if (opts.kind === 'pcb') paintPcb(g, boardW, boardD, px, opts.footprints ?? [], rng, opts.base);
  else if (opts.kind === 'carbon') paintCarbon(g, px.width, px.height, rng);
  else if (opts.kind === 'traces') paintTraces(g, boardW, boardD, px, rng, opts.base);
  else if (opts.kind === 'laminate') paintLaminate(g, px.width, px.height, rng);
  else paintBrushed(g, px.width, px.height, rng, opts.base);
  return px;
}

/**
 * A routing layer: dark resin with dense copper traces at 0 / 45 / 90 degrees, each ending in a
 * via pad, the way a package's build-up layers look when peeled apart.
 */
function paintTraces(
  g: CanvasRenderingContext2D,
  boardW: number,
  boardD: number,
  px: { width: number; height: number },
  rng: Rng,
  base = '#120d0a',
) {
  const { width: pw, height: ph } = px;
  const s = boardW > 0 ? pw / boardW : 1;
  g.fillStyle = base;
  g.fillRect(0, 0, pw, ph);
  // Routing runs in bundles: several thin parallel traces that turn together at 45°, each
  // ending in a via pad. Sizes follow the layer so any package reads as fine wiring.
  const unit = (Math.max(boardW, boardD) / 60) * s;
  const bundles = Math.min(260, Math.round((boardW * boardD * s * s) / (unit * unit) / 14));
  const dirs = [0, 1, 2, 3, 4, 5, 6, 7].map((k) => [Math.cos((k * Math.PI) / 4), Math.sin((k * Math.PI) / 4)]);
  g.lineCap = 'round';
  g.lineJoin = 'round';
  for (let i = 0; i < bundles; i++) {
    const k = Math.floor(rng() * 4) * 2; // start orthogonal
    const turn = rng() < 0.5 ? 1 : 7;
    const [d1x, d1y] = dirs[k];
    const [d2x, d2y] = dirs[(k + turn) % 8];
    const lanes = 2 + Math.floor(rng() * 6);
    const pitch = unit * (0.32 + rng() * 0.2);
    const l1 = unit * (2 + rng() * 6);
    const l2 = unit * (1 + rng() * 5);
    const x0 = rng() * pw;
    const y0 = rng() * ph;
    const alpha = 0.55 + rng() * 0.4;
    g.strokeStyle = `rgba(214, 128, 72, ${alpha})`;
    g.fillStyle = `rgba(240, 170, 110, ${alpha})`;
    g.lineWidth = Math.max(1, unit * 0.09);
    for (let l = 0; l < lanes; l++) {
      // Offset each lane sideways from the first leg so the bundle stays parallel through the bend.
      const ox = -d1y * pitch * l;
      const oy = d1x * pitch * l;
      const bx = x0 + ox + d1x * (l1 + pitch * l * (turn === 1 ? -0.41 : 0.41));
      const by = y0 + oy + d1y * (l1 + pitch * l * (turn === 1 ? -0.41 : 0.41));
      const ex = bx + d2x * l2;
      const ey = by + d2y * l2;
      g.beginPath();
      g.moveTo(x0 + ox, y0 + oy);
      g.lineTo(bx, by);
      g.lineTo(ex, ey);
      g.stroke();
      g.beginPath();
      g.arc(ex, ey, Math.max(1.2, unit * 0.16), 0, Math.PI * 2);
      g.fill();
    }
  }
}

/** Glass-fibre laminate: pale resin with a faint woven cross-hatch. */
function paintLaminate(g: CanvasRenderingContext2D, w: number, h: number, rng: Rng) {
  g.fillStyle = '#b9a873';
  g.fillRect(0, 0, w, h);
  const step = Math.max(3, Math.round(Math.min(w, h) / 90));
  g.lineWidth = Math.max(1, step * 0.45);
  for (let i = 0; i < w + h; i += step) {
    g.strokeStyle = `rgba(90, 76, 40, ${0.08 + rng() * 0.08})`;
    g.beginPath();
    g.moveTo(i, 0);
    g.lineTo(i, h);
    g.stroke();
    g.strokeStyle = `rgba(255, 245, 210, ${0.05 + rng() * 0.07})`;
    g.beginPath();
    g.moveTo(0, i);
    g.lineTo(w, i);
    g.stroke();
  }
}

function scaledCount(area: number, perRef: number, cap: number) {
  if (!(area > 0)) return 0;
  return Math.min(cap, Math.max(0, Math.round((perRef * area) / REF_AREA)));
}

function paintPcb(
  g: CanvasRenderingContext2D,
  boardW: number,
  boardD: number,
  px: { width: number; height: number },
  footprints: Footprint[],
  rng: Rng,
  base?: string,
) {
  const { width: pw, height: ph } = px;
  const sx = boardW > 0 ? pw / boardW : 0;
  const sz = boardD > 0 ? ph / boardD : 0;
  const to = (x: number, z: number): [number, number] => [pw / 2 + x * sx, ph / 2 + z * sz];

  const grad = g.createLinearGradient(0, 0, pw, ph);
  grad.addColorStop(0, base ? shade(base, 0.06) : '#0b3a1d');
  grad.addColorStop(1, base ? shade(base, -0.15) : '#082b15');
  g.fillStyle = grad;
  g.fillRect(0, 0, pw, ph);

  // 45° traces. Reach matches the descent (80 x 50 mm) on a 240 x 110 mm board.
  const area = boardW * boardD;
  const traces = scaledCount(area, REF_TRACES, TRACE_CAP);
  g.lineCap = 'round';
  for (let i = 0; i < traces; i++) {
    const x = (rng() - 0.5) * boardW;
    const z = (rng() - 0.5) * boardD;
    const pts = route45(x, z, x + (rng() - 0.5) * boardW * (80 / 240), z + (rng() - 0.5) * boardD * (50 / 110), rng());
    // Copper under the mask shows as slightly lighter lines in the mask's own colour.
    g.strokeStyle = base ? shade(base, 0.22, 0.25 + rng() * 0.3) : `rgba(60, 140, 70, ${0.25 + rng() * 0.3})`;
    g.lineWidth = sx * (0.2 + rng() * 0.35);
    g.beginPath();
    pts.forEach(([mx, mz], j) => (j ? g.lineTo(...to(mx, mz)) : g.moveTo(...to(mx, mz))));
    g.stroke();
  }

  // Vias: gold ring, dark hole. Radii are millimetres, same as the descent.
  const vias = scaledCount(area, REF_VIAS, VIA_CAP);
  for (let i = 0; i < vias; i++) {
    const [cx, cy] = to((rng() - 0.5) * boardW, (rng() - 0.5) * boardD);
    g.fillStyle = 'rgba(190, 170, 90, 0.55)';
    g.beginPath();
    g.arc(cx, cy, sx * 0.35, 0, Math.PI * 2);
    g.fill();
    g.fillStyle = '#06200f';
    g.beginPath();
    g.arc(cx, cy, sx * 0.15, 0, Math.PI * 2);
    g.fill();
  }

  // Silkscreen sits just outside each part, with the name above the outline.
  if (!footprints.length) return;
  const pad = 0.4;
  g.strokeStyle = 'rgba(235, 240, 230, 0.75)';
  g.fillStyle = 'rgba(235, 240, 230, 0.8)';
  g.lineWidth = sx * 0.25;
  for (const fp of footprints) {
    if (fp.silk === false) continue;
    // Label no wider than its outline (a monospace glyph is about 0.62 em wide), 0.5..2.6 mm tall.
    const em = Math.min(2.6, Math.max(0.5, (fp.w + pad * 2) / (Math.max(fp.name.length, 1) * 0.62)));
    g.font = `${Math.max(1, Math.round(sx * em))}px monospace`;
    const [x, y] = to(fp.x - fp.w / 2 - pad, fp.z - fp.d / 2 - pad);
    g.strokeRect(x, y, (fp.w + pad * 2) * sx, (fp.d + pad * 2) * sz);
    g.fillText(fp.name, x, y - sx * 0.8);
  }
}

/** Dark 2x2 twill. Band count follows the (already clamped) canvas, so it stays bounded. */
function paintCarbon(g: CanvasRenderingContext2D, w: number, h: number, rng: Rng) {
  g.fillStyle = '#121418';
  g.fillRect(0, 0, w, h);

  const tow = Math.max(5, Math.min(22, Math.round(Math.min(w, h) / 28)));
  const phase = rng() * tow * 2;
  const span = w + h;
  g.lineCap = 'square';
  g.lineWidth = tow - 1;
  for (let i = -h + phase; i < w + phase; i += tow) {
    const family = Math.floor((i - phase) / tow / 2) % 2 === 0;
    g.strokeStyle = family ? '#3a414b' : '#1e2228';
    g.beginPath();
    g.moveTo(i, 0);
    g.lineTo(i + h, h);
    g.stroke();
  }

  // Crossing tows, the other diagonal, so the ribs read as a weave rather than stripes.
  g.lineWidth = Math.max(1, (tow - 1) * 0.55);
  for (let i = phase; i < span + phase; i += tow) {
    const over = Math.floor((i - phase) / tow / 2) % 2 === 0;
    g.strokeStyle = over ? 'rgba(92, 98, 108, 0.55)' : 'rgba(8, 9, 11, 0.55)';
    g.beginPath();
    g.moveTo(w - i, 0);
    g.lineTo(w - i - h, h);
    g.stroke();
  }

  g.lineWidth = 1;
  g.strokeStyle = 'rgba(210, 214, 220, 0.14)';
  for (let i = -h + phase + tow * 0.28; i < w + phase; i += tow) {
    g.beginPath();
    g.moveTo(i, 0);
    g.lineTo(i + h, h);
    g.stroke();
  }
}

/** Light metal with fine horizontal grain. Streak count is capped with the canvas height. */
function paintBrushed(g: CanvasRenderingContext2D, w: number, h: number, rng: Rng, base?: string) {
  const grad = g.createLinearGradient(0, 0, 0, h);
  grad.addColorStop(0, base ? shade(base, 0.12) : '#e8ecf1');
  grad.addColorStop(0.5, base ?? '#d2d7de');
  grad.addColorStop(1, base ? shade(base, -0.12) : '#c4cad2');
  g.fillStyle = grad;
  g.fillRect(0, 0, w, h);

  const n = Math.min(640, Math.max(48, Math.round(h * 0.55)));
  g.lineCap = 'butt';
  for (let i = 0; i < n; i++) {
    const y = rng() * h;
    const x0 = rng() * w;
    const len = (0.1 + rng() * 0.55) * w;
    const v = Math.floor((rng() > 0.55 ? 198 : 108) + rng() * 42);
    g.strokeStyle = `rgba(${v}, ${v}, ${Math.min(255, v + 5)}, ${0.08 + rng() * 0.22})`;
    g.lineWidth = 0.45 + rng() * 0.7;
    g.beginPath();
    g.moveTo(x0, y);
    g.lineTo(x0 + len, y);
    g.stroke();
  }
}

/** What a block on a die is made of, painted on its top: see `paintPattern`. */
export type PatternKind = 'sm-grid' | 'sram' | 'phy' | 'logic';

/**
 * Silicon structures for floorplan blocks, as seen in die shots: `sm-grid` tiles of streaming
 * multiprocessors (`cells` columns × rows) each with register files, cores and cache;
 * `sram` dense memory banks; `phy` the striped analog lanes of memory / IO interfaces; `logic`
 * standard-cell rows. A thin-film sheen runs diagonally across, as on real silicon.
 */
export function paintPattern(
  g: CanvasRenderingContext2D,
  kind: PatternKind,
  w: number,
  h: number,
  base = '#3a4a3a',
  seed = 1,
  cells: [number, number] = [2, 6],
) {
  const rng = mulberry32(seed);
  g.fillStyle = shade(base, -0.35);
  g.fillRect(0, 0, w, h);
  const gut = Math.max(1, Math.min(w, h) * 0.012);
  if (kind === 'sm-grid') {
    const [cx, cy] = cells;
    const tw = (w - gut * (cx + 1)) / cx;
    const th = (h - gut * (cy + 1)) / cy;
    for (let i = 0; i < cx; i++)
      for (let j = 0; j < cy; j++) {
        const x = gut + i * (tw + gut);
        const y = gut + j * (th + gut);
        g.fillStyle = shade(base, -0.05 + rng() * 0.08);
        g.fillRect(x, y, tw, th);
        // Register file (big block), four processing blocks, L1 / shared memory strip.
        g.fillStyle = shade(base, 0.25);
        g.fillRect(x + tw * 0.06, y + th * 0.08, tw * 0.4, th * 0.5);
        for (let q = 0; q < 4; q++) {
          g.fillStyle = shade(base, 0.08 + (q % 2) * 0.06);
          g.fillRect(
            x + tw * (0.52 + (q % 2) * 0.22),
            y + th * (0.08 + Math.floor(q / 2) * 0.27),
            tw * 0.19,
            th * 0.23,
          );
        }
        g.fillStyle = shade(base, -0.18);
        g.fillRect(x + tw * 0.06, y + th * 0.66, tw * 0.88, th * 0.26);
        g.strokeStyle = shade(base, 0.35, 0.35);
        g.lineWidth = 1;
        for (let k = 0; k < 6; k++) {
          const yy = y + th * 0.68 + (k * th * 0.22) / 6;
          g.beginPath();
          g.moveTo(x + tw * 0.08, yy);
          g.lineTo(x + tw * 0.92, yy);
          g.stroke();
        }
      }
  } else if (kind === 'sram') {
    const bx = Math.max(2, Math.round(w / Math.max(h, 1)) * 2);
    const by = 2;
    const bw = (w - gut * (bx + 1)) / bx;
    const bh = (h - gut * (by + 1)) / by;
    for (let i = 0; i < bx; i++)
      for (let j = 0; j < by; j++) {
        const x = gut + i * (bw + gut);
        const y = gut + j * (bh + gut);
        g.fillStyle = shade(base, 0.05);
        g.fillRect(x, y, bw, bh);
        // Bit-cell array: a fine regular grid.
        g.fillStyle = shade(base, 0.3, 0.55);
        const pitch = Math.max(2, Math.min(bw, bh) / 14);
        for (let yy = y + pitch * 0.5; yy < y + bh - pitch * 0.3; yy += pitch)
          for (let xx = x + pitch * 0.5; xx < x + bw - pitch * 0.3; xx += pitch)
            g.fillRect(xx, yy, pitch * 0.55, pitch * 0.4);
        g.fillStyle = shade(base, -0.2);
        g.fillRect(x, y + bh * 0.46, bw, bh * 0.08); // decoder spine
      }
  } else if (kind === 'phy') {
    const along = w >= h;
    const n = 24;
    for (let i = 0; i < n; i++) {
      g.fillStyle = shade(base, i % 3 === 0 ? 0.3 : 0.05 + rng() * 0.1);
      const t = i / n;
      if (along) g.fillRect(gut + t * (w - gut * 2), gut, (w - gut * 2) / n - gut * 0.5, h - gut * 2);
      else g.fillRect(gut, gut + t * (h - gut * 2), w - gut * 2, (h - gut * 2) / n - gut * 0.5);
    }
  } else {
    // Standard cells in rows of varying widths.
    const row = Math.max(2, h / 40);
    for (let y = gut; y < h - row; y += row * 1.15) {
      let x = gut;
      while (x < w - gut) {
        const cw = row * (0.4 + rng() * 2.2);
        g.fillStyle = shade(base, -0.05 + rng() * 0.3);
        g.fillRect(x, y, Math.min(cw, w - gut - x), row);
        x += cw + row * 0.15;
      }
    }
  }
  // Thin-film sheen: hues drifting diagonally, as interference colours do on silicon.
  const sheen = g.createLinearGradient(0, 0, w, h);
  sheen.addColorStop(0, 'rgba(140, 90, 255, 0.22)');
  sheen.addColorStop(0.35, 'rgba(60, 170, 255, 0.16)');
  sheen.addColorStop(0.65, 'rgba(90, 255, 160, 0.14)');
  sheen.addColorStop(1, 'rgba(255, 190, 80, 0.2)');
  g.globalCompositeOperation = 'screen';
  g.fillStyle = sheen;
  g.fillRect(0, 0, w, h);
  g.globalCompositeOperation = 'source-over';
}
