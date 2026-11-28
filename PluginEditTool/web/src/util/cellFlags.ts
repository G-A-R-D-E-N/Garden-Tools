export interface CellFlag {
  bit: number;
  label: string;
  locked?: boolean;
}

export const CELL_FLAGS: CellFlag[] = [
  { bit: 0x0001, label: 'Interior', locked: true },
  { bit: 0x0002, label: 'Has water' },
  { bit: 0x0004, label: "Can't travel from here" },
  { bit: 0x0008, label: 'No LOD water' },
  { bit: 0x0020, label: 'Public area' },
  { bit: 0x0040, label: 'Hand changed' },
  { bit: 0x0080, label: 'Show sky' },
  { bit: 0x0100, label: 'Use sky lighting' },
  { bit: 0x0400, label: 'Hidden from interior cell list' },
  { bit: 0x0800, label: 'Sunlight shadows' },
  { bit: 0x1000, label: 'Distant LOD only' },
  { bit: 0x2000, label: "Player followers can't travel here" },
];

export const FLAGS_MAX = 0xffff;
const KNOWN_MASK = CELL_FLAGS.reduce((mask, f) => mask | f.bit, 0);

export const hasFlag = (value: number, bit: number): boolean => (value & bit) !== 0;

export const setFlag = (value: number, bit: number, on: boolean): number =>
  (on ? value | bit : value & ~bit) & FLAGS_MAX;

export const unknownFlagBits = (value: number): number => value & ~KNOWN_MASK & FLAGS_MAX;

export const withUnknownBits = (value: number, unknown: number): number =>
  ((value & KNOWN_MASK) | (unknown & ~KNOWN_MASK)) & FLAGS_MAX;
