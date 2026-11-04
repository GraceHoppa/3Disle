import { describe, expect, it } from 'vitest';
import { refsOf, resolveRef, walkDeps, type Loaded } from '../../src/models/pack';

describe('refsOf', () => {
  it('finds a project model, tour levels, details and file parts, however deep', () => {
    const refs = refsOf({
      model: 'motor.glb',
      levels: [{ file: 'card.json' }, { file: 'pcb.json' }],
      layers: [
        {
          detail: 'layer.json',
          parts: [
            { name: 'Die', detail: 'die.json' },
            { name: 'Fan', shape: 'file', file: 'fan.stl' },
            { name: 'Box', shape: 'box', file: 'not-a-file-part.json' },
          ],
        },
      ],
    });
    expect(refs.sort()).toEqual(['card.json', 'die.json', 'fan.stl', 'layer.json', 'motor.glb', 'pcb.json']);
  });

  it('finds nothing in plain values', () => {
    expect(refsOf(null)).toEqual([]);
    expect(refsOf('detail.json')).toEqual([]);
    expect(refsOf({ name: 'x', info: 'see detail.json' })).toEqual([]);
  });
});

describe('resolveRef', () => {
  it('resolves next to the file, and up and across folders', () => {
    expect(resolveRef('models/rtx/card.json', 'pcb.json')).toBe('models/rtx/pcb.json');
    expect(resolveRef('models/rtx/card.json', './pcb.json')).toBe('models/rtx/pcb.json');
    expect(resolveRef('projects/motor/project.json', '../../models/drone/fc.json')).toBe('models/drone/fc.json');
    expect(resolveRef('models/a/b.json', 'c.json?v=2#top')).toBe('models/a/c.json');
  });

  it('refuses web links, absolute paths and climbing out of the data folder', () => {
    expect(resolveRef('models/a/card.json', 'https://example.com/x.glb')).toBeNull();
    expect(resolveRef('models/a/card.json', 'data:application/json,{}')).toBeNull();
    expect(resolveRef('models/a/card.json', '/etc/passwd')).toBeNull();
    expect(resolveRef('models/a/card.json', '../../../etc/passwd')).toBeNull();
  });
});

describe('walkDeps', () => {
  const files = new Map<string, Loaded>([
    ['models/t/tour.json', { json: { levels: [{ file: 'card.json' }, { file: 'die.json' }] } }],
    ['models/t/card.json', { json: { layers: [{ parts: [{ detail: 'die.json' }, { detail: 'gone.json' }] }] } }],
    ['models/t/die.json', { json: { name: 'die', layers: [{ parts: [{ detail: 'card.json' }] }] } }],
    ['models/t/fan.glb', 'bin'],
    ['models/t/fan.json', { json: { model: 'fan.glb' } }],
  ]);
  const load = (rel: string) => files.get(rel) ?? 'missing';

  it('follows JSON once each, survives loops and reports what is missing', () => {
    const { files: got, missing } = walkDeps('models/t/tour.json', load);
    expect(got).toEqual(['models/t/tour.json', 'models/t/card.json', 'models/t/die.json']);
    expect(missing).toEqual(['models/t/gone.json']);
  });

  it('keeps a binary and does not look inside it', () => {
    expect(walkDeps('models/t/fan.json', load)).toEqual({
      files: ['models/t/fan.json', 'models/t/fan.glb'],
      missing: [],
    });
  });
});
