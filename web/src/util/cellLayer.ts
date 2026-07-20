// Shared layer-classification for the Cell Viewer, used by BOTH the panel's layer list and the
// viewport's per-mesh visibility tagging -- they MUST agree or hiding a layer desyncs from the list.
//
// Grouping priority:
//   - no renderable mesh (actor/trap, or the mesh failed to resolve) -> the markers group
//   - the mesh is effect-only (every renderable shape is a BSEffectShaderProperty: fog/mist/glow) ->
//     the FX/mist group, so atmospheric junk that covers the scene is one hideable layer
//   - otherwise the base object's record type (Static/Light/Furniture/...), which is what a modder
//     thinks in -- NOT the placed-record type (`recordType`), which is "PlacedObject" for ~every ref
import type { CellGeoMap } from '../CellViewport';
import type { CellPlacedReference } from '../backend';

export const FX_LAYER = '(FX / mist)';
export const MARKER_LAYER = '(markers)';

export function cellLayerOf(ref: CellPlacedReference, geo: CellGeoMap[string] | undefined): string {
  const shapes = geo && 'shapes' in geo ? geo.shapes : null;
  const renderable = shapes ? shapes.filter(s => s.verts.length > 0 && s.tris.length > 0) : [];
  if (renderable.length === 0) return MARKER_LAYER;
  if (renderable.every(s => s.effectShader)) return FX_LAYER;
  return ref.baseType || ref.recordType || '';
}
