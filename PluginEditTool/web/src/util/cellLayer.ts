














import type { CellGeoMap } from '../CellViewport';
import type { CellPlacedReference } from '../backend';
import type { LayerVisibility, ToggleableLayer } from '../cellLighting';
import { primitiveRoleOf } from '../cellPrimitive';

export const FX_LAYER = '(FX / mist)';
export const MARKER_LAYER = '(markers)';
export const DECAL_LAYER = '(ground decals)';







export function cellLayerOf(ref: CellPlacedReference, geo: CellGeoMap[string] | undefined): string {
  if (ref.decalDiffuse) return DECAL_LAYER;
  const shapes = geo && 'shapes' in geo ? geo.shapes : null;
  const renderable = shapes ? shapes.filter(s => s.verts.length > 0 && s.tris.length > 0) : [];
  if (renderable.length === 0) return MARKER_LAYER;
  if (ref.isMarker) return MARKER_LAYER;
  if (renderable.every(s => s.effectShader)) return FX_LAYER;
  return ref.baseType || ref.recordType || '';
}

export type CellLayerKind = ToggleableLayer | 'light';

const EFFECT_PATH = /(^|[\\/])effects[\\/]/i;
const ROOM_PATH = /room(marker|bounds?)/i;
const TRIGGER_PATH = /trigger|primitive/i;
const MARKER_PATH = /xmarker|cocmarker|soundmarker|mapmarker|playerstart/i;
const MARKER_BASE_TYPES = new Set(['SOUN', 'IDLM', 'NAVM', 'XMRK', 'COCM', 'MRKR']);
const COC_EDITOR_ID = /^COC/;

export function cellLayerKindOf(ref: CellPlacedReference): CellLayerKind | null {
  if (ref.decalDiffuse) return null;
  if (ref.light || ref.baseType === 'LIGH') return 'light';
  const primitive = primitiveRoleOf(ref);
  if (primitive === 'room') return 'room';
  const path = ref.modelPath ?? '';
  if (EFFECT_PATH.test(path)) return 'effect';
  if (ROOM_PATH.test(path)) return 'room';
  if (primitive === 'trigger' && !path) return 'trigger';
  if (TRIGGER_PATH.test(path)) return 'trigger';
  const unmodelled = !path && !(ref.scolParts && ref.scolParts.length > 0);
  const cocEditor = COC_EDITOR_ID.test(ref.editorId ?? '') || COC_EDITOR_ID.test(ref.baseEditorId ?? '');
  if (ref.isMarker || unmodelled || MARKER_PATH.test(path) || MARKER_BASE_TYPES.has(ref.baseType ?? '') || cocEditor) {
    return 'marker';
  }
  return null;
}

export function toggleLayerOf(kind: CellLayerKind | null): ToggleableLayer | undefined {
  return kind === null || kind === 'light' ? undefined : kind;
}

export function hasRenderableShapes(geo: CellGeoMap[string] | undefined): boolean {
  return !!geo && 'shapes' in geo && geo.shapes.some(s => s.verts.length > 0 && s.tris.length > 0);
}

export function layerEnabled(layer: ToggleableLayer | undefined, layers: LayerVisibility): boolean {
  return layer === undefined || layers[layer];
}

export function applyCellVisibility(
  entries: { mesh: { visible: boolean }; recordType: string; layer?: ToggleableLayer }[],
  hiddenTypes: ReadonlySet<string> | undefined,
  layers: LayerVisibility,
): void {
  for (const e of entries) {
    e.mesh.visible = (!hiddenTypes || !hiddenTypes.has(e.recordType)) && layerEnabled(e.layer, layers);
  }
}
