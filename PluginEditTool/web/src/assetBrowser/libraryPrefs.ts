import type { BaseObjectItem } from './baseObjects';

export const FAVOURITES_KEY = 'fo4ide.assetBrowser.favourites';
export const RECENT_KEY = 'fo4ide.assetBrowser.recent';
export const RECENT_LIMIT = 40;

export interface LibraryState {
  favourites: BaseObjectItem[];
  recent: BaseObjectItem[];
}

type ReadStorage = Pick<Storage, 'getItem'> | null;
type WriteStorage = Pick<Storage, 'setItem'> | null;

export function loadLibrary(storage: ReadStorage = safeStorage()): LibraryState {
  return {
    favourites: readItems(storage, FAVOURITES_KEY),
    recent: readItems(storage, RECENT_KEY).slice(0, RECENT_LIMIT),
  };
}

export function saveLibrary(state: LibraryState, storage: WriteStorage = safeStorage()): void {
  writeItem(storage, FAVOURITES_KEY, JSON.stringify(state.favourites));
  writeItem(storage, RECENT_KEY, JSON.stringify(state.recent));
}

export function isFavourite(favourites: readonly BaseObjectItem[], formKey: string): boolean {
  return favourites.some(item => item.formKey === formKey);
}

export function toggleFavourite(favourites: readonly BaseObjectItem[], item: BaseObjectItem): BaseObjectItem[] {
  if (isFavourite(favourites, item.formKey)) return favourites.filter(entry => entry.formKey !== item.formKey);
  return [{ ...item }, ...favourites];
}

export function pushRecent(recent: readonly BaseObjectItem[], items: readonly BaseObjectItem[]): BaseObjectItem[] {
  const fresh = items.map(item => ({ ...item }));
  const keys = new Set(fresh.map(item => item.formKey));
  return [...fresh, ...recent.filter(item => !keys.has(item.formKey))].slice(0, RECENT_LIMIT);
}

function readItems(storage: ReadStorage, key: string): BaseObjectItem[] {
  try {
    const parsed = JSON.parse(storage?.getItem(key) ?? '[]') as unknown;
    if (!Array.isArray(parsed)) return [];
    return parsed.flatMap(value => {
      const item = toItem(value);
      return item ? [item] : [];
    });
  } catch {
    return [];
  }
}

function toItem(value: unknown): BaseObjectItem | null {
  if (typeof value !== 'object' || value === null) return null;
  const record = value as Record<string, unknown>;
  if (typeof record.formKey !== 'string' || record.formKey === '') return null;
  const text = (field: string): string => {
    const raw = record[field];
    return typeof raw === 'string' ? raw : '';
  };
  return {
    formKey: record.formKey,
    hexId: text('hexId'),
    editorId: text('editorId'),
    name: text('name'),
    signature: text('signature'),
    typeName: text('typeName'),
    modelPath: text('modelPath'),
    winningPlugin: text('winningPlugin'),
    deleted: record.deleted === true,
    hasModel: record.hasModel === true,
  };
}

function writeItem(storage: WriteStorage, key: string, value: string): void {
  try {
    storage?.setItem(key, value);
  } catch {
    return;
  }
}

function safeStorage(): Storage | null {
  try {
    return typeof localStorage === 'undefined' ? null : localStorage;
  } catch {
    return null;
  }
}
