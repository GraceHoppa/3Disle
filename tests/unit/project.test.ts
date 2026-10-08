import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { applyProject, isProject, listNodes, parseSpecs, type ProjectSpec } from '../../src/models/project';
import { creditOf } from '../../src/models/load';

/** A flat model like a Blender export: named meshes, one of them inside a named group. */
function model() {
  const root = new THREE.Group();
  const mesh = (name: string, x: number) => {
    const m = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1));
    m.name = name;
    m.position.x = x;
    return m;
  };
  const arms = new THREE.Group();
  arms.name = 'Arms';
  arms.position.y = 5;
  arms.add(mesh('Arm_L', -3), mesh('Arm_R', 3));
  root.add(mesh('Plate', 0), mesh('Motor', 10), arms);
  return root;
}
const worldX = (o: THREE.Object3D) => o.getWorldPosition(new THREE.Vector3()).x;
const worldY = (o: THREE.Object3D) => o.getWorldPosition(new THREE.Vector3()).y;

const spec: ProjectSpec = {
  name: 'Drone',
  model: 'drone.glb',
  parts: [
    {
      name: 'Frame',
      nodes: ['Plate', 'Arm_L', 'Arm_R'],
      category: 'Frame',
      info: 'Carbon',
      specs: { Weight: '120 g' },
    },
    { name: 'Motor FL', nodes: ['Motor'], detail: '../motor/project.json' },
  ],
};

describe('isProject', () => {
  it('tells projects from assemblies', () => {
    expect(isProject(spec)).toBe(true);
    expect(isProject({ name: 'x', footprint: [1, 1], layers: [] })).toBe(false);
    expect(isProject(null)).toBe(false);
  });
});

describe('listNodes', () => {
  it('lists every named object, nested ones too', () => {
    expect(listNodes(model()).map((o) => o.name)).toEqual(['Plate', 'Motor', 'Arms', 'Arm_L', 'Arm_R']);
  });
});

describe('applyProject', () => {
  it('makes one direct child per part, then the unclaimed rest', () => {
    const { root } = applyProject(model(), spec);
    expect(root.name).toBe('Drone');
    expect(root.children.map((c) => c.name)).toEqual(['Frame', 'Motor FL', 'Arms']);
  });

  it('moves objects into parts without moving them in the world', () => {
    const m = model();
    m.updateWorldMatrix(true, true);
    const before = ['Plate', 'Arm_L', 'Motor'].map((n) => [
      worldX(m.getObjectByName(n)!),
      worldY(m.getObjectByName(n)!),
    ]);
    const { root } = applyProject(m, spec);
    root.updateWorldMatrix(true, true);
    const after = ['Plate', 'Arm_L', 'Motor'].map((n) => [
      worldX(root.getObjectByName(n)!),
      worldY(root.getObjectByName(n)!),
    ]);
    expect(after).toEqual(before);
    expect(root.getObjectByName('Arm_L')!.parent!.name).toBe('Frame');
  });

  it('carries the description, specs and zoom branch for the inspector', () => {
    const { root } = applyProject(model(), spec);
    const frame = root.getObjectByName('Frame')!;
    expect(frame.userData).toMatchObject({ info: 'Carbon', category: 'Frame', specs: { Weight: '120 g' } });
    expect(root.getObjectByName('Motor FL')!.userData.detail).toBe('../motor/project.json');
  });

  it('reports names the model does not have and claims each object once', () => {
    const { root, missing } = applyProject(model(), {
      name: 'x',
      model: 'm.glb',
      parts: [
        { name: 'A', nodes: ['Plate', 'Ghost'] },
        { name: 'B', nodes: ['Plate'] },
      ],
    });
    expect(missing).toEqual(['Ghost']);
    expect(root.getObjectByName('B')!.children).toHaveLength(0);
  });
});

describe('parseSpecs', () => {
  it('reads Key: value lines, keeping colons inside values', () => {
    expect(parseSpecs('Voltage: 12 V\n\nnot a spec\nRatio: 16:9\n  Current :  55 A ')).toEqual({
      Voltage: '12 V',
      Ratio: '16:9',
      Current: '55 A',
    });
  });
});

describe('credits', () => {
  const sketchfab = {
    extras: {
      author: 'chiwei (https://sketchfab.com/chiwei2333)',
      license: 'CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/)',
      source: 'https://sketchfab.com/3d-models/x',
      title: 'Brushless Motor',
    },
  };

  it('reads author, license, title and source from a glTF asset, without the URLs in brackets', () => {
    expect(creditOf(sketchfab)).toEqual({
      author: 'chiwei',
      license: 'CC-BY-4.0',
      source: 'https://sketchfab.com/3d-models/x',
      title: 'Brushless Motor',
    });
  });

  it('has no credit without both an author and a license', () => {
    expect(creditOf(undefined)).toBeNull();
    expect(creditOf({ extras: { author: 'someone' } })).toBeNull();
    expect(creditOf({ extras: { author: '  ', license: 'MIT' } })).toBeNull();
  });

  it('a project keeps the file’s credit unless it names its own', () => {
    const model = new THREE.Group();
    model.userData.credit = creditOf(sketchfab);
    const spec: ProjectSpec = { name: 'm', model: 'm.glb', parts: [] };
    expect(applyProject(model, spec).root.userData.credit.author).toBe('chiwei');
    const own = { author: 'me', license: 'CC0' };
    expect(applyProject(new THREE.Group(), { ...spec, credit: own }).root.userData.credit).toEqual(own);
  });
});
