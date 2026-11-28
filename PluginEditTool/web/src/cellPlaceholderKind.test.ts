import { describe, expect, it } from 'vitest';
import type { CellPlacedReference } from './backend';
import { isEditorMarkerRef, placeholderKindOf } from './cellPlaceholderKind';

const reference = (overrides: Partial<CellPlacedReference> = {}): CellPlacedReference => ({
  formKey: 'a',
  recordType: 'PlacedObject',
  baseType: 'STAT',
  baseFormKey: '000002:Fallout4.esm',
  modelPath: null,
  position: { x: 0, y: 0, z: 0 },
  rotation: { x: 0, y: 0, z: 0 },
  scale: 1,
  ...overrides,
});

describe('placeholderKindOf', () => {
  it('draws a primitive room as a room box even when its model resolved', () => {
    const ref = reference({
      baseEditorId: 'RoomMarker', modelPath: 'Architecture/RoomMarker.nif', primitive: { type: 1, bounds: { x: 1, y: 1, z: 1 } },
    });
    expect(placeholderKindOf(ref, 'room', true)).toBe('room');
  });

  it('draws a primitive trigger as a trigger box even when its model resolved', () => {
    const ref = reference({ baseEditorId: 'TriggerBox', primitive: { type: 1, bounds: { x: 1, y: 1, z: 1 } } });
    expect(placeholderKindOf(ref, 'trigger', true)).toBe('trigger');
  });

  it('keeps an unresolved room or trigger reference as its own kind', () => {
    expect(placeholderKindOf(reference({ modelPath: 'Architecture/RoomBounds.nif' }), 'room', false)).toBe('room');
    expect(placeholderKindOf(reference({ modelPath: 'Triggers/TriggerBox.nif' }), 'trigger', false)).toBe('trigger');
  });

  it('draws a resolved non-primitive room or trigger with its model', () => {
    expect(placeholderKindOf(reference({ modelPath: 'Architecture/RoomBounds.nif' }), 'room', true)).toBeNull();
    expect(placeholderKindOf(reference({ modelPath: 'Triggers/TriggerBox.nif' }), 'trigger', true)).toBeNull();
  });

  it('draws an editor marker as an xmarker, even when its marker model resolved', () => {
    expect(placeholderKindOf(reference({ baseEditorId: 'XMarkerHeading' }), 'marker', false)).toBe('xmarker');
    expect(placeholderKindOf(reference({ baseEditorId: 'XMarkerHeading', modelPath: 'Markers/XMarker.nif' }), 'marker', true)).toBe('xmarker');
    expect(placeholderKindOf(reference({ modelPath: 'Markers/XMarker.nif' }), 'marker', true)).toBe('xmarker');
    expect(placeholderKindOf(reference({ baseEditorId: 'COCLocation' }), 'marker', false)).toBe('marker');
  });

  it('labels an unresolved non-editor marker by whether it names a model', () => {
    expect(placeholderKindOf(reference({ modelPath: 'Clutter/Beacon.nif' }), 'marker', false)).toBe('unresolved');
    expect(placeholderKindOf(reference({ modelPath: null }), 'marker', false)).toBe('marker');
  });

  it('leaves a resolved non-editor marker and a missing kind to the model path', () => {
    expect(placeholderKindOf(reference({ modelPath: 'Clutter/Beacon.nif' }), 'marker', true)).toBeNull();
    expect(placeholderKindOf(reference(), null, false)).toBeNull();
  });
});

describe('isEditorMarkerRef', () => {
  it('matches marker base ids and XMarker model paths', () => {
    expect(isEditorMarkerRef({ baseEditorId: 'XMarkerHeading', modelPath: null })).toBe(true);
    expect(isEditorMarkerRef({ modelPath: 'Markers/XMarker.nif' })).toBe(true);
    expect(isEditorMarkerRef({ baseEditorId: 'Chair', modelPath: 'Clutter/Chair.nif' })).toBe(false);
  });
});
