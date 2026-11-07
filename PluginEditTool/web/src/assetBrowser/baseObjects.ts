export type BaseObjectSort = 'editorId' | 'hexId' | 'name' | 'type' | 'modelPath' | 'plugin';
export type BaseObjectMatch = 'contains' | 'startsWith' | 'exact' | 'regex';
export type BaseObjectScope = 'editorId' | 'name' | 'hexId' | 'modelPath' | 'plugin';
export type SortDir = 'asc' | 'desc';

export interface BaseObjectQuery {
  type?: string;
  query?: string;
  scopes?: BaseObjectScope[];
  match?: BaseObjectMatch;
  activePlugin?: string;
  hideDeleted?: boolean;
  placeableWithModel?: boolean;
  sortBy?: BaseObjectSort;
  sortDir?: SortDir;
  offset?: number;
  limit?: number;
}

export interface BrowserQueryState {
  text: string;
  type: string;
  scopes: BaseObjectScope[];
  match: BaseObjectMatch;
  activePlugin: string;
  hideDeleted: boolean;
  placeableWithModel: boolean;
  sortBy: BaseObjectSort;
  sortDir: SortDir;
}

export interface BaseObjectItem {
  formKey: string;
  hexId: string;
  editorId: string;
  name: string;
  signature: string;
  typeName: string;
  modelPath: string;
  winningPlugin: string;
  deleted: boolean;
  hasModel: boolean;
}

export interface BaseObjectListing {
  total: number;
  offset: number;
  limit: number;
  sortBy: BaseObjectSort;
  sortDir: SortDir;
  typeCounts: Record<string, number>;
  items: BaseObjectItem[];
}

export interface PlacementTransform {
  x: number;
  y: number;
  z: number;
  rx: number;
  ry: number;
  rz: number;
}

export interface PlacementItem {
  baseFormKey: string;
  transform: PlacementTransform;
  editorId?: string;
}

export interface PlaceOptions {
  persistent?: boolean;
  initiallyDisabled?: boolean;
  scale?: number;
  editorId?: string;
}

export interface PlaceReply {
  plugin: string;
  cellFormKey: string;
  created: string[];
  references: unknown[];
}

export type ListingReply = { ok: true; listing: BaseObjectListing } | { ok: false; error: string };
export type PlaceResultReply = { ok: true; reply: PlaceReply } | { ok: false; error: string };

export const BASE_OBJECT_PAGE_SIZE = 100;
export const BASE_OBJECT_MAX_LIMIT = 200;
export const BASE_OBJECT_SORTS: BaseObjectSort[] = ['editorId', 'hexId', 'name', 'type', 'modelPath', 'plugin'];
export const BASE_OBJECT_SCOPES: BaseObjectScope[] = ['editorId', 'name', 'hexId', 'modelPath', 'plugin'];
export const BASE_OBJECT_MATCHES: BaseObjectMatch[] = ['contains', 'startsWith', 'exact', 'regex'];

export const DEFAULT_BROWSER_QUERY: BrowserQueryState = {
  text: '',
  type: '',
  scopes: ['editorId', 'name', 'hexId'],
  match: 'contains',
  activePlugin: '',
  hideDeleted: false,
  placeableWithModel: false,
  sortBy: 'editorId',
  sortDir: 'asc',
};

export function buildBaseObjectQuery(state: BrowserQueryState, offset: number, limit: number): BaseObjectQuery {
  const query: BaseObjectQuery = {
    scopes: state.scopes.length > 0 ? [...state.scopes] : [...DEFAULT_BROWSER_QUERY.scopes],
    match: state.match,
    sortBy: state.sortBy,
    sortDir: state.sortDir,
    offset: Math.max(0, Math.trunc(offset)),
    limit: Math.min(BASE_OBJECT_MAX_LIMIT, Math.max(1, Math.trunc(limit))),
  };
  const text = state.text.trim();
  if (text) query.query = text;
  if (state.type) query.type = state.type;
  if (state.activePlugin) query.activePlugin = state.activePlugin;
  if (state.hideDeleted) query.hideDeleted = true;
  if (state.placeableWithModel) query.placeableWithModel = true;
  return query;
}

export function parseBaseObjectListing(text: string): ListingReply {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return { ok: false, error: 'Object list reply was not JSON.' };
  }
  if (!isRecord(raw)) return { ok: false, error: 'Object list reply was not an object.' };
  if (typeof raw.error === 'string') return { ok: false, error: raw.error };
  const items = Array.isArray(raw.items) ? raw.items.filter(isRecord).map(toItem) : [];
  return {
    ok: true,
    listing: {
      total: numberOr(raw.total, items.length),
      offset: numberOr(raw.offset, 0),
      limit: numberOr(raw.limit, BASE_OBJECT_PAGE_SIZE),
      sortBy: isSort(raw.sortBy) ? raw.sortBy : 'editorId',
      sortDir: raw.sortDir === 'desc' ? 'desc' : 'asc',
      typeCounts: toCounts(raw.typeCounts),
      items,
    },
  };
}

export function parsePlaceResult(text: string): PlaceResultReply {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return { ok: false, error: 'Place reply was not JSON.' };
  }
  if (!isRecord(raw)) return { ok: false, error: 'Place reply was not an object.' };
  if (typeof raw.error === 'string') return { ok: false, error: raw.error };
  const created = Array.isArray(raw.created) ? raw.created.filter((k): k is string => typeof k === 'string') : [];
  return {
    ok: true,
    reply: {
      plugin: strOf(raw.plugin),
      cellFormKey: strOf(raw.cellFormKey),
      created,
      references: Array.isArray(raw.references) ? raw.references : [],
    },
  };
}

export function placementsJson(items: PlacementItem[]): string {
  return JSON.stringify(items);
}

export function zeroTransform(): PlacementTransform {
  return { x: 0, y: 0, z: 0, rx: 0, ry: 0, rz: 0 };
}

export function isSort(value: unknown): value is BaseObjectSort {
  return typeof value === 'string' && (BASE_OBJECT_SORTS as string[]).includes(value);
}

function toItem(row: Record<string, unknown>): BaseObjectItem {
  return {
    formKey: strOf(row.formKey),
    hexId: strOf(row.hexId),
    editorId: strOf(row.editorId),
    name: strOf(row.name),
    signature: strOf(row.signature),
    typeName: strOf(row.typeName),
    modelPath: strOf(row.modelPath),
    winningPlugin: strOf(row.winningPlugin),
    deleted: row.deleted === true,
    hasModel: row.hasModel === true,
  };
}

function toCounts(value: unknown): Record<string, number> {
  if (!isRecord(value)) return {};
  const counts: Record<string, number> = {};
  for (const [signature, count] of Object.entries(value)) {
    if (typeof count === 'number') counts[signature] = count;
  }
  return counts;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function strOf(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function numberOr(value: unknown, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}
