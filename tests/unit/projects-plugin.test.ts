import { afterEach, describe, expect, it } from 'vitest';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { saveProjectFile } from '../../scripts/projects-plugin';

const dirs: string[] = [];
const temp = () => {
  const d = mkdtempSync(join(tmpdir(), 'projects-'));
  dirs.push(d);
  return d;
};
afterEach(() => dirs.splice(0).forEach((d) => rmSync(d, { recursive: true, force: true })));
const json = (o: object) => Buffer.from(JSON.stringify(o));

describe('saveProjectFile', () => {
  it('writes the project and lists it in the index', () => {
    const dir = temp();
    saveProjectFile(dir, 'drone', 'project.json', json({ name: 'My drone', model: 'drone.glb', parts: [] }));
    expect(JSON.parse(readFileSync(join(dir, 'drone/project.json'), 'utf8')).name).toBe('My drone');
    expect(JSON.parse(readFileSync(join(dir, 'index.json'), 'utf8'))).toEqual([
      { name: 'My drone', file: 'drone/project.json', note: 'Project' },
    ]);
  });

  it('saving again updates the index entry instead of adding another', () => {
    const dir = temp();
    saveProjectFile(dir, 'drone', 'project.json', json({ name: 'A', model: 'm.glb', parts: [] }));
    saveProjectFile(dir, 'drone', 'project.json', json({ name: 'B', model: 'm.glb', parts: [] }));
    const index = JSON.parse(readFileSync(join(dir, 'index.json'), 'utf8'));
    expect(index).toHaveLength(1);
    expect(index[0].name).toBe('B');
  });

  it('writes model files without touching the index', () => {
    const dir = temp();
    saveProjectFile(dir, 'drone', 'drone.glb', Buffer.from([1, 2, 3]));
    expect(readFileSync(join(dir, 'drone/drone.glb'))).toEqual(Buffer.from([1, 2, 3]));
    expect(existsSync(join(dir, 'index.json'))).toBe(false);
  });

  it('refuses anything that could escape the folder or is not a project file', () => {
    const dir = temp();
    const bad = (slug: string, file: string, data = json({ name: 'x', model: 'm.glb' })) =>
      expect(() => saveProjectFile(dir, slug, file, data)).toThrow();
    bad('..', 'project.json');
    bad('drone/../x', 'project.json');
    bad('Drone', 'project.json');
    bad('drone', '../index.json');
    bad('drone', 'evil.sh');
    bad('drone', '.hidden.json');
    bad('drone', 'project.json', json({ nope: true }));
    bad('drone', 'project.json', Buffer.from('not json'));
  });
});
