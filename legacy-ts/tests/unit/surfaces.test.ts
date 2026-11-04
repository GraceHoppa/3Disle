import { describe, expect, it } from 'vitest';
import { paintSurface, surfaceSize, type SurfaceKind } from '../../src/models/surfaces';

type LogEntry = { op: 'call'; name: string; args: unknown[] } | { op: 'set'; name: string; value: unknown };

function isGradient(value: unknown): value is { addColorStop: unknown } {
  return typeof value === 'object' && value !== null && 'addColorStop' in value;
}

/** Records calls and property sets. Gradients collapse to a token so two runs can be compared. */
function recordingContext() {
  const log: LogEntry[] = [];
  const call = (name: string, args: unknown[]) => log.push({ op: 'call', name, args });
  const api = {
    log,
    createLinearGradient(x0: number, y0: number, x1: number, y1: number) {
      call('createLinearGradient', [x0, y0, x1, y1]);
      return {
        addColorStop(offset: number, color: string) {
          call('addColorStop', [offset, color]);
        },
      };
    },
    fillRect(x: number, y: number, w: number, h: number) {
      call('fillRect', [x, y, w, h]);
    },
    strokeRect(x: number, y: number, w: number, h: number) {
      call('strokeRect', [x, y, w, h]);
    },
    beginPath() {
      call('beginPath', []);
    },
    moveTo(x: number, y: number) {
      call('moveTo', [x, y]);
    },
    lineTo(x: number, y: number) {
      call('lineTo', [x, y]);
    },
    stroke() {
      call('stroke', []);
    },
    fill() {
      call('fill', []);
    },
    arc(x: number, y: number, r: number, a0: number, a1: number) {
      call('arc', [x, y, r, a0, a1]);
    },
    fillText(text: string, x: number, y: number) {
      call('fillText', [text, x, y]);
    },
  };
  for (const prop of ['fillStyle', 'strokeStyle', 'lineWidth', 'lineCap', 'font'] as const) {
    Object.defineProperty(api, prop, {
      enumerable: true,
      set(value: unknown) {
        log.push({ op: 'set', name: prop, value: isGradient(value) ? '[gradient]' : value });
      },
    });
  }
  return api as typeof api & CanvasRenderingContext2D;
}

function calls(g: ReturnType<typeof recordingContext>, name: string) {
  return g.log.filter((e) => e.op === 'call' && e.name === name);
}

describe('surfaceSize', () => {
  it('uses pxPerMm, defaulting to 4', () => {
    expect(surfaceSize(100, 50)).toEqual({ width: 400, height: 200 });
    expect(surfaceSize(100, 50, 2)).toEqual({ width: 200, height: 100 });
  });

  it('clamps a huge board to 2048 on the long side and keeps the aspect', () => {
    expect(surfaceSize(1000, 500, 4)).toEqual({ width: 2048, height: 1024 });
    expect(surfaceSize(500, 1000, 4)).toEqual({ width: 1024, height: 2048 });
    expect(surfaceSize(1000, 1000, 4)).toEqual({ width: 2048, height: 2048 });
  });

  it('lifts a tiny board so the long side is at least 64', () => {
    expect(surfaceSize(1, 1, 4)).toEqual({ width: 64, height: 64 });
    expect(surfaceSize(2, 1, 4)).toEqual({ width: 64, height: 32 });
  });
});

describe('paintSurface', () => {
  it('redraws identically for the same seed and differs for another', () => {
    const log = (seed: number) => {
      const g = recordingContext();
      paintSurface(g, {
        kind: 'pcb',
        width: 80,
        depth: 40,
        seed,
        footprints: [{ name: 'U1', x: 0, z: 0, w: 8, d: 8 }],
      });
      return g.log;
    };
    expect(log(7)).toEqual(log(7));
    expect(log(7)).not.toEqual(log(8));
  });

  it('draws one silkscreen outline and one label per footprint', () => {
    const g = recordingContext();
    const size = paintSurface(g, {
      kind: 'pcb',
      width: 100,
      depth: 50,
      pxPerMm: 4,
      seed: 1,
      footprints: [
        { name: 'U1', x: 0, z: 0, w: 10, d: 10 },
        { name: 'C12', x: 20, z: -5, w: 4, d: 2 },
      ],
    });
    expect(size).toEqual({ width: 400, height: 200 });

    const texts = calls(g, 'fillText');
    const rects = calls(g, 'strokeRect');
    expect(texts.map((c) => c.args[0])).toEqual(['U1', 'C12']);
    expect(rects).toHaveLength(2);

    // 100 x 50 mm at 4 px/mm. The part at the origin is centred on the canvas.
    const [x, y, w, h] = rects[0].args as number[];
    expect(x + w / 2).toBeCloseTo(200);
    expect(y + h / 2).toBeCloseTo(100);
    const [x2, y2, w2, h2] = rects[1].args as number[];
    expect(x2 + w2 / 2).toBeCloseTo(200 + 20 * 4);
    expect(y2 + h2 / 2).toBeCloseTo(100 + -5 * 4);
  });

  it('grows pcb trace and via counts with area, then caps them', () => {
    const painted = (width: number, depth: number) => {
      const g = recordingContext();
      paintSurface(g, { kind: 'pcb', width, depth, seed: 4 });
      return g;
    };
    const traces = (width: number, depth: number) => calls(painted(width, depth), 'stroke').length;
    const vias = (width: number, depth: number) => calls(painted(width, depth), 'arc').length / 2;

    // The descent's 300 traces and 900 vias, on a 240 x 110 mm board.
    expect(traces(240, 110)).toBe(300);
    expect(vias(240, 110)).toBe(900);
    expect(traces(40, 40)).toBeLessThan(traces(120, 80));
    expect(traces(120, 80)).toBeLessThan(traces(240, 110));

    const linear = Math.round((300 * (8000 * 8000)) / (240 * 110));
    expect(traces(8000, 8000)).toBeLessThan(linear);
    expect(traces(8000, 8000)).toBe(traces(16000, 16000));
    expect(vias(8000, 8000)).toBe(vias(16000, 16000));
  });

  it('paints carbon and brushed metal without throwing', () => {
    for (const kind of ['carbon', 'brushed'] as SurfaceKind[]) {
      const g = recordingContext();
      expect(() => paintSurface(g, { kind, width: 80, depth: 40, seed: 1 })).not.toThrow();
      expect(calls(g, 'fillRect').length).toBeGreaterThan(0);
      expect(calls(g, 'stroke').length).toBeGreaterThan(0);
    }
  });
});
