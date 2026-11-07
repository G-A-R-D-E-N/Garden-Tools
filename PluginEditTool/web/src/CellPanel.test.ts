import { describe, expect, it } from 'vitest';
import type { CellPlacedReference } from './backend';
import type { CellGeoMap } from './CellViewport';
import { parseCellGridCoordinate } from './util/cellGrid';
import { cellLayerOf, FX_LAYER, MARKER_LAYER } from './util/cellLayer';

const accepted: Array<[string, number]> = [
  ['0', 0],
  ['-33', -33],
  ['+32', 32],
  ['2147483647', 2147483647],
  ['-2147483648', -2147483648],
];

const rejected = [
  '',
  '12abc',
  '1.5',
  '1e3',
  ' 5',
  '5 ',
  '9007199254740992',
  '2147483648',
  '-2147483649',
  '4294967297',
];

describe('parseCellGridCoordinate', () => {
  it.each(accepted)('accepts complete integer token %s', (text, expected) => {
    expect(parseCellGridCoordinate(text)).toBe(expected);
  });

  it.each(rejected)('rejects non-integer token %s', text => {
    expect(parseCellGridCoordinate(text)).toBeNull();
  });
});

describe('cellLayerOf', () => {
  const reference = (overrides: Partial<CellPlacedReference>): CellPlacedReference => ({
    formKey: '000001:Fallout4.esm',
    recordType: 'PlacedObject',
    baseType: 'STAT',
    baseFormKey: '000002:Fallout4.esm',
    modelPath: 'Clutter/Lamp.nif',
    position: { x: 0, y: 0, z: 0 },
    rotation: { x: 0, y: 0, z: 0 },
    scale: 1,
    ...overrides,
  });
  const solid = { shapes: [{ verts: [0, 0, 0], tris: [0, 1, 2] }] } as unknown as CellGeoMap[string];
  const effectOnly = { shapes: [{ verts: [0, 0, 0], tris: [0, 1, 2], effectShader: true }] } as unknown as CellGeoMap[string];

  it('puts a backend-flagged marker in the marker layer whatever its model name', () => {
    expect(cellLayerOf(reference({ isMarker: true, modelPath: 'Clutter/Beacon.nif' }), solid)).toBe(MARKER_LAYER);
  });

  it('does not treat a marker-like model name as a marker when the backend does not flag it', () => {
    expect(cellLayerOf(reference({ isMarker: false, modelPath: 'Markers/Lamp.nif' }), solid)).toBe('STAT');
  });

  it('keeps effect-only geometry in the FX layer', () => {
    expect(cellLayerOf(reference({ isMarker: false }), effectOnly)).toBe(FX_LAYER);
  });
});
