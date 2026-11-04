import { readdirSync, readFileSync, statSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { checkData, glbNodeNames } from '../../src/models/check';

const publicDir = join(dirname(fileURLToPath(import.meta.url)), '../../public');

function walk(dir: string): string[] {
  return readdirSync(dir).flatMap((name) => {
    const path = join(dir, name);
    return statSync(path).isDirectory() ? walk(path) : [relative(publicDir, path).split('\\').join('/')];
  });
}

/** An in-memory data folder: JSON values are written out, byte arrays kept as they are. */
function folder(files: Record<string, unknown>) {
  const bytes = new Map(
    Object.entries(files).map(([k, v]) => [
      k,
      v instanceof Uint8Array ? v : new TextEncoder().encode(typeof v === 'string' ? v : JSON.stringify(v)),
    ]),
  );
  return checkData([...bytes.keys()], (rel) => bytes.get(rel) ?? null);
}

/** A minimal GLB: header and a JSON chunk naming these nodes. */
function glb(names: string[]) {
  const json = new TextEncoder().encode(
    JSON.stringify({ asset: { version: '2.0' }, nodes: names.map((name) => ({ name })) }),
  );
  const out = new Uint8Array(20 + json.length);
  const view = new DataView(out.buffer);
  view.setUint32(0, 0x46546c67, true);
  view.setUint32(4, 2, true);
  view.setUint32(8, out.length, true);
  view.setUint32(12, json.length, true);
  view.setUint32(16, 0x4e4f534a, true);
  out.set(json, 20);
  return out;
}

const board = (extra: object = {}) => ({
  name: 'Board',
  footprint: [40, 40],
  layers: [
    {
      name: 'Top',
      thickness: 1,
      parts: [
        { name: 'Chip', shape: 'box', size: [4, 1, 4], at: [-8, 0] },
        {
          name: 'Pins',
          shape: 'box',
          size: [1, 1, 1],
          at: [8, 0],
          repeat: { count: 2, step: [2, 0], names: ['P1', 'P2'] },
        },
      ],
      links: [{ from: 'Chip', to: ['P1', 'Pins'] }],
    },
  ],
  ...extra,
});

describe('checkData', () => {
  it('the shipped devices, tours and lists all resolve', async () => {
    const files = walk(publicDir);
    expect(await checkData(files, (rel) => (files.includes(rel) ? readFileSync(join(publicDir, rel)) : null))).toEqual(
      [],
    );
  });

  it('accepts a sound folder', async () => {
    expect(
      await folder({
        'models/index.json': [{ name: 'Board', file: 'b/board.json' }],
        'models/b/board.json': board({
          controls: [{ id: 'on', label: 'On' }],
          wires: [{ name: 'W', from: 'Chip', to: 'P2' }],
        }),
        'models/b/tour.json': { name: 'T', levels: [{ file: 'board.json', into: 'Chip' }, { file: 'board.json' }] },
      }),
    ).toEqual([]);
  });

  it('reports bad JSON, missing files, escapes and list entries that are not there', async () => {
    expect(
      await folder({
        'models/index.json': [{ name: 'Gone', file: 'gone.json' }],
        'models/broken.json': '{ "name": ',
        'models/a/card.json': {
          name: 'Card',
          footprint: [10, 10],
          layers: [
            {
              name: 'L',
              thickness: 1,
              detail: '../../../outside.json',
              parts: [{ name: 'X', shape: 'box', detail: 'x.json' }],
            },
          ],
        },
      }),
    ).toEqual([
      { file: 'models/a/card.json', message: '“../../../outside.json” points outside the data folder' },
      { file: 'models/a/card.json', message: '“x.json” is missing (models/a/x.json)' },
      { file: 'models/broken.json', message: 'is not valid JSON' },
      { file: 'models/index.json', message: 'lists “gone.json”, which is not there' },
    ]);
  });

  it('reports links, wires, rules and tour stops that name nothing', async () => {
    const bad = board({
      wires: [{ name: 'W', from: 'Chip', to: 'Ghost' }],
      layers: [{ ...board().layers[0], links: [{ name: 'Bus', from: 'Chip', to: 'P3' }] }],
      readouts: [{ label: 'Speed', by: 'throttle', map: [[0, 0]] }],
    });
    expect(
      await folder({
        'models/b/board.json': bad,
        'models/b/tour.json': { name: 'T', levels: [{ file: 'board.json', into: 'Nowhere' }] },
      }),
    ).toEqual([
      { file: 'models/b/board.json', message: 'a rule follows “throttle”, which is not one of its controls' },
      { file: 'models/b/board.json', message: 'link “Bus” names “P3”, which is not on layer “Top”' },
      { file: 'models/b/board.json', message: 'a wire names “Ghost”, which is not a part of it' },
      { file: 'models/b/tour.json', message: 'level board.json: “into” names “Nowhere”, which is not a part of it' },
    ]);
  });

  it('checks project parts against the objects in their GLB', async () => {
    expect(
      await folder({
        'projects/m/project.json': {
          name: 'Motor',
          model: 'motor.glb',
          parts: [{ name: 'Bell', nodes: ['Bell_Top', 'Shaft', 'Stator'] }],
        },
        'projects/m/motor.glb': glb(['Bell Top', 'Shaft']),
      }),
    ).toEqual([{ file: 'projects/m/project.json', message: 'part “Bell” names “Stator”, which is not in motor.glb' }]);
  });
});

describe('glbNodeNames', () => {
  it('reads names the way the loader writes them, and nothing from other bytes', () => {
    expect([...glbNodeNames(glb(['Cylinder.008', 'Bell']))]).toEqual(['Cylinder.008', 'Cylinder008', 'Bell']);
    expect(glbNodeNames(new TextEncoder().encode('not a glb at all, really')).size).toBe(0);
  });
});
