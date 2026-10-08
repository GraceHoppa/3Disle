import type { LayerSpec, LinkSpec, PartSpec } from './assembly';

/**
 * Standard flows, drawn from what parts *are* instead of their names: a layer that says
 * `"flows": "gpu-board"` gets the power path and the data paths of any graphics card, as long
 * as its parts carry roles. The text describes how every such device works, not one product;
 * product numbers belong in explicit `links`, which win over a preset flow of the same name.
 */
interface Rule {
  name: string;
  from: string;
  to: string;
  color: string;
  category: string;
  info: string;
}

export const PRESETS: Record<string, Rule[]> = {
  'gpu-board': [
    {
      name: '12 V input',
      from: 'power-in',
      to: 'vrm',
      color: '#ffb347',
      category: 'Power',
      info: 'Power from the supply enters at the connector and feeds the voltage regulator.',
    },
    {
      name: 'GPU core supply',
      from: 'vrm',
      to: 'gpu',
      color: '#ffb347',
      category: 'Power',
      info: 'The regulator’s phases step 12 V down to the roughly 1 V the GPU core runs on.',
    },
    {
      name: 'Memory bus',
      from: 'gpu',
      to: 'memory',
      color: '#76b900',
      category: 'Data · memory',
      info: 'Each memory chip has its own channel to the GPU; textures, frames and model data stream over them.',
    },
    {
      name: 'PCIe link',
      from: 'gpu',
      to: 'pcie',
      color: '#5aa9ff',
      category: 'Data · host',
      info: 'Commands and data from the CPU arrive over the PCIe lanes of the slot.',
    },
    {
      name: 'Display outputs',
      from: 'gpu',
      to: 'display',
      color: '#e8edf2',
      category: 'Data · display',
      info: 'Finished frames leave through the display outputs.',
    },
  ],
  'fpv-quad': [
    {
      name: 'Battery power',
      from: 'battery',
      to: 'esc',
      color: '#ffb347',
      category: 'Power',
      info: 'The LiPo pack feeds the 4-in-1 ESC through the XT60 lead; the ESC also powers the flight controller.',
    },
    {
      name: 'Three-phase drive',
      from: 'esc',
      to: 'motor',
      color: '#ffb347',
      category: 'Power',
      info: 'The ESC switches battery power across each motor’s three windings in turn, which is what makes a brushless motor spin.',
    },
    {
      name: 'Motor commands',
      from: 'fc',
      to: 'esc',
      color: '#76b900',
      category: 'Data · control',
      info: 'The flight controller sends each motor a throttle value thousands of times a second (DShot); bidirectional DShot returns the motor’s RPM.',
    },
    {
      name: 'Radio link',
      from: 'rx',
      to: 'fc',
      color: '#5aa9ff',
      category: 'Data · radio',
      info: 'The receiver turns the pilot’s radio packets into stick positions for the flight controller (CRSF over a UART).',
    },
    {
      name: 'Video',
      from: 'camera',
      to: 'vtx',
      color: '#e8edf2',
      category: 'Data · video',
      info: 'The camera feeds the video transmitter, which sends the picture to the pilot’s goggles.',
    },
    {
      name: 'Telemetry and OSD',
      from: 'fc',
      to: 'vtx',
      color: '#5aa9ff',
      category: 'Data · telemetry',
      info: 'The flight controller sends battery voltage, flight mode and warnings to the video system to show in the goggles (MSP).',
    },
  ],
  'gpu-die': [
    {
      name: 'Memory traffic',
      from: 'memory-controller',
      to: 'l2',
      color: '#76b900',
      category: 'Data · memory',
      info: 'Memory controllers on the edge of the die move data between the memory chips and the shared L2 cache.',
    },
    {
      name: 'Work to the clusters',
      from: 'l2',
      to: 'gpc',
      color: '#76b900',
      category: 'Data · compute',
      info: 'The L2 cache feeds every graphics processing cluster; results flow back the same way.',
    },
    {
      name: 'Host and display',
      from: 'io',
      to: 'l2',
      color: '#5aa9ff',
      category: 'Data · host',
      info: 'PCIe, display and media engines reach the rest of the chip through the L2.',
    },
  ],
};

/** The names a part answers to on the board: each named copy, else the part (or its whole row). */
const namesOf = (p: PartSpec) => p.repeat?.names ?? [p.name];

/**
 * Links for a layer's preset: one per source part, to every part with the target role. Rules
 * whose roles are missing are skipped; explicit links with the same name replace the preset's.
 */
export function presetLinks(layer: LayerSpec, scale: number): LinkSpec[] {
  const rules = layer.flows ? PRESETS[layer.flows] : undefined;
  if (!rules) return [];
  const byRole = (role: string) => (layer.parts ?? []).filter((p) => p.role === role);
  const taken = new Set((layer.links ?? []).map((l) => l.name));
  const out: LinkSpec[] = [];
  for (const r of rules) {
    if (taken.has(r.name)) continue;
    const to = byRole(r.to).flatMap(namesOf);
    if (!to.length) continue;
    for (const src of byRole(r.from).flatMap(namesOf)) {
      out.push({
        name: r.name,
        from: src,
        to,
        lanes: 4,
        width: Math.max(0.03, scale * 0.0012),
        color: r.color,
        category: r.category,
        info: r.info,
      });
    }
  }
  return out;
}
