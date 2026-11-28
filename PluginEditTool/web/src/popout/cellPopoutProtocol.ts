import type { CellLightingData, CellPlacedReference } from '../backend';
import type { BaseObjectDragPayload } from '../assetBrowser/dragPayload';
import type { Point3 } from '../assetBrowser/placement';

export const CELL_POPOUT_CHANNEL = 'fo4ide.cellViewport';
export const CELL_POPOUT_ROUTE = 'cell-viewport';
export const CELL_POPOUT_HASH = `#/popout/${CELL_POPOUT_ROUTE}`;
export const CELL_POPOUT_WINDOW_NAME = 'fo4ide-cell-viewport';

export type CellPopoutIntent =
  | { kind: 'hello' }
  | { kind: 'bye' }
  | { kind: 'select'; formKey: string | null }
  | { kind: 'hide'; formKey: string }
  | { kind: 'unhideAll' }
  | { kind: 'toggleMarkers' }
  | { kind: 'delete'; formKey: string }
  | { kind: 'duplicate'; formKey: string }
  | { kind: 'details'; formKey: string }
  | { kind: 'move'; reference: CellPlacedReference }
  | { kind: 'drop'; payload: BaseObjectDragPayload; point: Point3 }
  | { kind: 'log'; line: string };

export type CellMainMessage =
  | { kind: 'references'; references: CellPlacedReference[]; lighting: CellLightingData | null }
  | { kind: 'sync'; hiddenTypes: string[]; hiddenRefs: string[]; selectedKey: string | null }
  | { kind: 'close' };

export type PopoutWindowEvent = { Type: 'PopoutWindow'; Route: string; Open: boolean };

const INTENT_KINDS = new Set<string>([
  'hello', 'bye', 'select', 'hide', 'unhideAll', 'toggleMarkers', 'delete', 'duplicate', 'details', 'move', 'drop', 'log',
]);

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);

const isPoint = (value: unknown): value is Point3 =>
  isRecord(value) && typeof value.x === 'number' && typeof value.y === 'number' && typeof value.z === 'number';

const isReference = (value: unknown): value is CellPlacedReference =>
  isRecord(value) && typeof value.formKey === 'string' && isPoint(value.position) && isRecord(value.rotation);

const isStringArray = (value: unknown): value is string[] =>
  Array.isArray(value) && value.every(item => typeof item === 'string');

export function isCellPopoutIntent(value: unknown): value is CellPopoutIntent {
  if (!isRecord(value) || typeof value.kind !== 'string' || !INTENT_KINDS.has(value.kind)) return false;
  switch (value.kind) {
    case 'hello':
    case 'bye':
    case 'unhideAll':
    case 'toggleMarkers':
      return true;
    case 'select':
      return value.formKey === null || typeof value.formKey === 'string';
    case 'hide':
    case 'delete':
    case 'duplicate':
    case 'details':
      return typeof value.formKey === 'string';
    case 'move':
      return isReference(value.reference);
    case 'drop':
      return isRecord(value.payload) && Array.isArray(value.payload.items) && isPoint(value.point);
    case 'log':
      return typeof value.line === 'string';
    default:
      return false;
  }
}

export function isCellMainMessage(value: unknown): value is CellMainMessage {
  if (!isRecord(value)) return false;
  switch (value.kind) {
    case 'references':
      return Array.isArray(value.references) && value.references.every(isReference);
    case 'sync':
      return isStringArray(value.hiddenTypes) && isStringArray(value.hiddenRefs)
        && (value.selectedKey === null || typeof value.selectedKey === 'string');
    case 'close':
      return true;
    default:
      return false;
  }
}

export function parsePopoutWindowEvent(value: unknown): PopoutWindowEvent | null {
  if (!isRecord(value) || value.Type !== 'PopoutWindow') return null;
  if (typeof value.Route !== 'string' || typeof value.Open !== 'boolean') return null;
  return { Type: 'PopoutWindow', Route: value.Route, Open: value.Open };
}

export function buildCellPopoutUrl(location: { origin: string; pathname: string }, token: string): string {
  const query = token ? `?t=${encodeURIComponent(token)}` : '';
  return `${location.origin}${location.pathname}${CELL_POPOUT_HASH}${query}`;
}

export function isCellPopoutRoute(hash: string): boolean {
  const route = hash.replace(/^#/, '').split('?')[0];
  return route === `/popout/${CELL_POPOUT_ROUTE}`;
}

type PopoutWindowListener = (event: PopoutWindowEvent) => void;

const popoutWindowListeners = new Set<PopoutWindowListener>();

export function subscribePopoutWindow(listener: PopoutWindowListener): () => void {
  popoutWindowListeners.add(listener);
  return () => {
    popoutWindowListeners.delete(listener);
  };
}

export function emitPopoutWindow(event: PopoutWindowEvent): void {
  for (const listener of [...popoutWindowListeners]) listener(event);
}
