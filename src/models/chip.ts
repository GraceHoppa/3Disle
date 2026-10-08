import type { AssemblySpec } from './assembly';

/**
 * A generic look inside a packaged chip, for chips that have no detail model of their own:
 * contacts underneath (a ball grid for bigger chips, edge pads for small ones), a substrate,
 * the silicon die on micro-bumps, and the mould compound on top. It shows how chips like this
 * are built in general, and says so: only the chip's own name and specs are carried over.
 */
export function genericPackage(chip: {
  name: string;
  size: [number, number, number];
  specs?: Record<string, string>;
  info?: string;
}): AssemblySpec {
  const [w, h, d] = chip.size;
  const note = 'Generic package: shows how chips like this are built, not this chip’s actual layout.';
  const big = Math.min(w, d) >= 8;
  const pitch = big ? Math.max(0.5, Math.min(w, d) / 24) : 0;
  const cols = big ? Math.max(2, Math.floor((w - pitch) / pitch)) : 0;
  const rows = big ? Math.max(2, Math.floor((d - pitch) / pitch)) : 0;
  const dieW = w * 0.5;
  const dieD = d * 0.5;
  const bump = Math.max(0.08, Math.min(dieW, dieD) / 30);
  return {
    name: `Inside ${chip.name}`,
    footprint: [w, d],
    layers: [
      {
        name: big ? 'Ball grid' : 'Edge pads',
        thickness: 0,
        category: 'Package · contacts',
        info: big
          ? 'Solder balls under the package connect it to the board. ' + note
          : 'Flat pads around the edge connect the package to the board. ' + note,
        parts: big
          ? [
              {
                name: 'Solder balls',
                shape: 'sphere',
                radius: pitch * 0.3,
                at: [-(cols - 1) * pitch * 0.5, -(rows - 1) * pitch * 0.5],
                repeat: { count: 0, grid: [cols, rows], step: [pitch, pitch] },
                material: 'steel',
                color: '#c8ccd2',
              },
            ]
          : [
              {
                name: 'Pads (front)',
                shape: 'box',
                size: [w * 0.12, 0.05, d * 0.12],
                at: [-w * 0.3, -d * 0.42],
                repeat: { count: 4, step: [w * 0.2, 0] },
                material: 'gold',
              },
              {
                name: 'Pads (back)',
                shape: 'box',
                size: [w * 0.12, 0.05, d * 0.12],
                at: [-w * 0.3, d * 0.42],
                repeat: { count: 4, step: [w * 0.2, 0] },
                material: 'gold',
              },
            ],
      },
      {
        name: 'Substrate',
        thickness: Math.max(0.1, h * 0.25),
        surface: 'traces',
        color: '#2a1d15',
        category: 'Package · substrate',
        info: 'Thin layers of copper wiring fan the die’s connections out to the contacts. ' + note,
      },
      {
        name: 'Die',
        thickness: 0,
        category: 'Silicon',
        info: (chip.info ? chip.info + ' ' : '') + note,
        specs: chip.specs,
        parts: [
          {
            name: 'Micro-bumps',
            shape: 'sphere',
            radius: bump,
            at: [-dieW / 2 + bump * 2, -dieD / 2 + bump * 2],
            repeat: {
              count: 0,
              grid: [Math.max(2, Math.floor(dieW / (bump * 4))), Math.max(2, Math.floor(dieD / (bump * 4)))],
              step: [bump * 4, bump * 4],
            },
            material: 'gold',
          },
          {
            name: `${chip.name} die`,
            shape: 'box',
            size: [dieW, Math.max(0.05, h * 0.15), dieD],
            lift: bump * 2,
            material: 'chip',
            color: '#4a5263',
            marking: 'silicon die',
            specs: chip.specs,
            info: 'The silicon itself, mounted face-down on micro-bumps (flip-chip). ' + note,
          },
        ],
      },
      {
        name: 'Mould compound',
        thickness: Math.max(0.1, h * 0.45),
        material: 'resin',
        category: 'Package · body',
        info: 'Epoxy that seals and protects the die; drawn see-through here. ' + note,
      },
    ],
  };
}
