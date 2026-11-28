import type { CellPlacedReference } from './backend';
import type { PlaceholderKind } from './cellPlaceholders';
import { primitiveRoleOf } from './cellPrimitive';
import type { CellLayerKind } from './util/cellLayer';

const EDITOR_MARKER = /marker/i;
const XMARKER_MODEL = /xmarker/i;

export function isEditorMarkerRef(ref: Pick<CellPlacedReference, 'baseEditorId' | 'modelPath'>): boolean {
  return EDITOR_MARKER.test(ref.baseEditorId ?? '') || XMARKER_MODEL.test(ref.modelPath ?? '');
}

export function placeholderKindOf(
  ref: CellPlacedReference,
  kind: CellLayerKind | null,
  resolved: boolean,
): PlaceholderKind | null {
  const role = primitiveRoleOf(ref);
  if (role) return role;
  if ((kind === 'room' || kind === 'trigger') && !resolved) return kind;
  if (kind === 'marker' && isEditorMarkerRef(ref)) return 'xmarker';
  if (kind === 'marker' && !resolved) return ref.modelPath ? 'unresolved' : 'marker';
  return null;
}