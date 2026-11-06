














import type { CellGeoMap } from '../CellViewport';
import type { CellPlacedReference } from '../backend';

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
