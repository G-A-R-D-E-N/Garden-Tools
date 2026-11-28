import { describe, expect, it } from 'vitest';
import type { CellPlacedReference } from '../backend';
import { DEFAULT_LAYER_VISIBILITY, type LayerVisibility } from '../cellLighting';
import type { CellGeoMap } from '../CellViewport';
import { applyCellVisibility, cellLayerKindOf, hasRenderableShapes, layerEnabled, toggleLayerOf } from './cellLayer';

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

describe('cellLayerKindOf', () => {
  it('leaves an ordinary modelled reference unclassified', () => {
    expect(cellLayerKindOf(reference({ modelPath: 'Clutter/Lamp.nif' }))).toBeNull();
  });

  it('never classifies a ground decal', () => {
    expect(cellLayerKindOf(reference({ decalDiffuse: 'Decals/Blood.dds', modelPath: null, isMarker: true }))).toBeNull();
  });

  it('classifies a light by its light record or its LIGH base', () => {
    expect(cellLayerKindOf(reference({
      light: { radius: 256, color: { r: 1, g: 1, b: 1 } }, modelPath: 'Lights/Lamp.nif',
    }))).toBe('light');
    expect(cellLayerKindOf(reference({ baseType: 'LIGH', modelPath: null }))).toBe('light');
  });

  it('classifies effect meshes by an effects directory in the path, either slash', () => {
    expect(cellLayerKindOf(reference({ modelPath: 'Effects\\Mist\\Fog.nif' }))).toBe('effect');
    expect(cellLayerKindOf(reference({ modelPath: 'effects/Mist/Fog.nif' }))).toBe('effect');
  });

  it('does not treat a name that merely starts with effects as an effects directory', () => {
    expect(cellLayerKindOf(reference({ modelPath: 'Clutter/effectsTable.nif' }))).toBeNull();
  });

  it('classifies room bounds and room markers as room', () => {
    expect(cellLayerKindOf(reference({ modelPath: 'Architecture/RoomMarker.nif' }))).toBe('room');
    expect(cellLayerKindOf(reference({ modelPath: 'Architecture/RoomBounds.nif' }))).toBe('room');
    expect(cellLayerKindOf(reference({ modelPath: 'Architecture/roombound.nif' }))).toBe('room');
  });

  it('classifies triggers and primitives as trigger, with PrimitiveBox included', () => {
    expect(cellLayerKindOf(reference({ modelPath: 'Triggers/TriggerBox.nif' }))).toBe('trigger');
    expect(cellLayerKindOf(reference({ modelPath: 'Primitives/PrimitiveBox.nif' }))).toBe('trigger');
  });

  it('classifies a backend-flagged marker whatever its model name', () => {
    expect(cellLayerKindOf(reference({ isMarker: true, modelPath: 'Clutter/Beacon.nif' }))).toBe('marker');
  });

  it('classifies a reference with no model and no scol parts as a marker', () => {
    expect(cellLayerKindOf(reference({ modelPath: '', scolParts: null }))).toBe('marker');
    expect(cellLayerKindOf(reference({ modelPath: null, scolParts: [] }))).toBe('marker');
  });

  it('does not classify a scol-only reference with no model path as a marker', () => {
    expect(cellLayerKindOf(reference({
      modelPath: '',
      scolParts: [{ modelPath: 'Scol/Wall.nif', placements: [] }],
    }))).toBeNull();
  });

  it('classifies XMarker paths and marker base types as marker', () => {
    expect(cellLayerKindOf(reference({ modelPath: 'Markers/XMarker.nif' }))).toBe('marker');
    for (const baseType of ['SOUN', 'IDLM', 'NAVM', 'XMRK']) {
      expect(cellLayerKindOf(reference({ baseType, modelPath: 'Clutter/Thing.nif' }))).toBe('marker');
    }
  });

  it('classifies cell-of-cell markers by base type, model path or editor id', () => {
    expect(cellLayerKindOf(reference({ baseType: 'COCM', modelPath: 'Clutter/Thing.nif' }))).toBe('marker');
    expect(cellLayerKindOf(reference({ modelPath: 'Markers/COCMarker.nif' }))).toBe('marker');
    expect(cellLayerKindOf(reference({ editorId: 'COC_Start', modelPath: 'Clutter/Thing.nif' }))).toBe('marker');
    expect(cellLayerKindOf(reference({ baseEditorId: 'COCLocation', modelPath: 'Clutter/Thing.nif' }))).toBe('marker');
  });

  it('classifies sound, map and player start markers as marker', () => {
    expect(cellLayerKindOf(reference({ modelPath: 'Markers/SoundMarker.nif' }))).toBe('marker');
    expect(cellLayerKindOf(reference({ modelPath: 'Markers/MapMarker.nif' }))).toBe('marker');
    expect(cellLayerKindOf(reference({ modelPath: 'Markers/PlayerStart.nif' }))).toBe('marker');
    expect(cellLayerKindOf(reference({ baseType: 'MRKR', modelPath: 'Clutter/Thing.nif' }))).toBe('marker');
  });

  it('gives the room and trigger paths precedence over the marker flag', () => {
    expect(cellLayerKindOf(reference({ isMarker: true, modelPath: 'Architecture/RoomMarker.nif' }))).toBe('room');
  });

  it('gives the light base precedence over the effect path', () => {
    expect(cellLayerKindOf(reference({ baseType: 'LIGH', modelPath: 'Effects/Glow.nif' }))).toBe('light');
  });
});

describe('toggleLayerOf', () => {
  it('maps a classified kind to its toggle and leaves lights and plain references ungated', () => {
    expect(toggleLayerOf('marker')).toBe('marker');
    expect(toggleLayerOf('effect')).toBe('effect');
    expect(toggleLayerOf('light')).toBeUndefined();
    expect(toggleLayerOf(null)).toBeUndefined();
  });
});

describe('hasRenderableShapes', () => {
  const shape = (verts: number[], tris: number[]) => ({ verts, tris });

  it('is false without a loaded mesh or without a triangle', () => {
    expect(hasRenderableShapes(undefined)).toBe(false);
    expect(hasRenderableShapes({ error: 'missing' })).toBe(false);
    expect(hasRenderableShapes({ shapes: [shape([0, 0, 0], [])] } as unknown as CellGeoMap[string])).toBe(false);
  });

  it('is true when one shape has vertices and triangles', () => {
    expect(hasRenderableShapes({ shapes: [shape([], []), shape([0, 0, 0], [0, 1, 2])] } as unknown as CellGeoMap[string])).toBe(true);
  });
});

describe('layerEnabled', () => {
  it('shows unclassified references whatever the layer flags say', () => {
    const allOff: LayerVisibility = { room: false, trigger: false, marker: false, effect: false };
    expect(layerEnabled(undefined, allOff)).toBe(true);
  });

  it('follows the flag of a classified layer', () => {
    expect(layerEnabled('room', DEFAULT_LAYER_VISIBILITY)).toBe(true);
    expect(layerEnabled('room', { ...DEFAULT_LAYER_VISIBILITY, room: false })).toBe(false);
  });
});

describe('applyCellVisibility', () => {
  const entry = (recordType: string, layer?: 'room' | 'trigger' | 'marker' | 'effect') => ({
    mesh: { visible: true }, recordType, layer,
  });

  it('hides a record type that is in the hidden set', () => {
    const stat = entry('STAT');
    const door = entry('DOOR');
    applyCellVisibility([stat, door], new Set(['STAT']), DEFAULT_LAYER_VISIBILITY);
    expect(stat.mesh.visible).toBe(false);
    expect(door.mesh.visible).toBe(true);
  });

  it('hides a classified entry when its layer is off, even if its record type is not hidden', () => {
    const room = entry('STAT', 'room');
    const trigger = entry('STAT', 'trigger');
    applyCellVisibility([room, trigger], undefined, { ...DEFAULT_LAYER_VISIBILITY, room: false });
    expect(room.mesh.visible).toBe(false);
    expect(trigger.mesh.visible).toBe(true);
  });

  it('keeps unclassified entries visible when every layer is off', () => {
    const plain = entry('STAT');
    const effect = entry('STAT', 'effect');
    const allOff: LayerVisibility = { room: false, trigger: false, marker: false, effect: false };
    applyCellVisibility([plain, effect], undefined, allOff);
    expect(plain.mesh.visible).toBe(true);
    expect(effect.mesh.visible).toBe(false);
  });

  it('re-shows an entry when its layer is turned back on', () => {
    const marker = entry('(markers)', 'marker');
    applyCellVisibility([marker], undefined, { ...DEFAULT_LAYER_VISIBILITY, marker: false });
    expect(marker.mesh.visible).toBe(false);
    applyCellVisibility([marker], undefined, DEFAULT_LAYER_VISIBILITY);
    expect(marker.mesh.visible).toBe(true);
  });

  it('hides a marker model and its placeholder together when the marker layer is off', () => {
    const model = entry('(markers)', 'marker');
    const placeholder = entry('(markers)', 'marker');
    applyCellVisibility([model, placeholder], undefined, { ...DEFAULT_LAYER_VISIBILITY, marker: false });
    expect(model.mesh.visible).toBe(false);
    expect(placeholder.mesh.visible).toBe(false);
  });
});
