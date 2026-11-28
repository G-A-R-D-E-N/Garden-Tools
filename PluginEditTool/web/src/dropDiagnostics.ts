import type { Point3 } from './assetBrowser/placement';

export const DRAG_OVER_LINE = 'drag over viewport';
export const DROP_REJECTED_LINE = 'drop payload rejected -- the drag data holds no base objects';
export const POINT_MISSING_LINE = 'pointAt returned no point -- the cursor is off the cell and off the ground plane';
export const DROP_IGNORED_BUSY_LINE = 'drop ignored: busy -- another edit is running';
export const DROP_IGNORED_NO_CELL_LINE = 'drop ignored: no cell loaded';

export function dragStartLine(count: number): string {
  return `drag start: ${count} object(s)`;
}

export function dropTypesLine(types: readonly string[]): string {
  return `drop received (types=${types.join(', ') || 'none'})`;
}

export function formatPoint(p: Point3): string {
  return `${p.x.toFixed(1)},${p.y.toFixed(1)},${p.z.toFixed(1)}`;
}

export function dropPointLine(p: Point3, snapped: Point3): string {
  return `drop point ${formatPoint(p)} (snapped ${formatPoint(snapped)})`;
}

export function placeRequestLine(count: number): string {
  return `PlaceReferences request: ${count} placement(s)`;
}

export function placeOkLine(created: readonly string[]): string {
  return created.length > 0
    ? `PlaceReferences ok: created ${created.join(', ')}`
    : 'PlaceReferences ok: created none';
}

export function placeErrorLine(message: string): string {
  return `✗ PlaceReferences error: ${message}`;
}
