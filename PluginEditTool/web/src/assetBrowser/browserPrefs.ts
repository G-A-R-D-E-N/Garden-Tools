import type { BaseObjectSort, SortDir } from './baseObjects';
import { DEFAULT_SNAP_GRID, parseSnapSetting } from './placement';

export type BrowserColumn = 'editorId' | 'hexId' | 'name' | 'type' | 'modelPath' | 'plugin';

export interface ColumnPref {
  visible: boolean;
  width: number;
}

export type ColumnPrefs = Record<BrowserColumn, ColumnPref>;

export const BROWSER_COLUMNS: BrowserColumn[] = ['editorId', 'hexId', 'name', 'type', 'modelPath', 'plugin'];

export const DEFAULT_COLUMN_PREFS: ColumnPrefs = {
  editorId: { visible: true, width: 180 },
  hexId: { visible: true, width: 90 },
  name: { visible: true, width: 160 },
  type: { visible: true, width: 110 },
  modelPath: { visible: true, width: 220 },
  plugin: { visible: true, width: 150 },
};

export const COLUMN_PREFS_KEY = 'fo4ide.assetBrowser.columns';
export const SEARCH_HISTORY_KEY = 'fo4ide.assetBrowser.searchHistory';
export const SNAP_KEY = 'fo4ide.assetBrowser.snapGrid';
export const SEARCH_HISTORY_LIMIT = 10;
const MIN_WIDTH = 40;
const MAX_WIDTH = 800;

type ReadStorage = Pick<Storage, 'getItem'> | null;
type WriteStorage = Pick<Storage, 'setItem'> | null;

function readItem(storage: ReadStorage, key: string): string | null {
  try {
    return storage?.getItem(key) ?? null;
  } catch {
    return null;
  }
}

function writeItem(storage: WriteStorage, key: string, value: string): void {
  try {
    storage?.setItem(key, value);
  } catch {
    return;
  }
}

export function loadColumnPrefs(storage: ReadStorage = safeStorage()): ColumnPrefs {
  const raw = readItem(storage, COLUMN_PREFS_KEY);
  if (!raw) return structuredClone(DEFAULT_COLUMN_PREFS);
  try {
    const parsed = JSON.parse(raw) as Partial<Record<BrowserColumn, Partial<ColumnPref>>>;
    const merged = structuredClone(DEFAULT_COLUMN_PREFS);
    for (const column of BROWSER_COLUMNS) {
      const pref = parsed[column];
      if (!pref) continue;
      if (typeof pref.visible === 'boolean') merged[column].visible = pref.visible;
      if (typeof pref.width === 'number') merged[column].width = clampWidth(pref.width);
    }
    return merged;
  } catch {
    return structuredClone(DEFAULT_COLUMN_PREFS);
  }
}

export function saveColumnPrefs(prefs: ColumnPrefs, storage: WriteStorage = safeStorage()): void {
  writeItem(storage, COLUMN_PREFS_KEY, JSON.stringify(prefs));
}

export function clampWidth(width: number): number {
  return Math.min(MAX_WIDTH, Math.max(MIN_WIDTH, Math.round(width)));
}

export function pushSearchHistory(history: string[], term: string): string[] {
  const trimmed = term.trim();
  if (!trimmed) return history;
  return [trimmed, ...history.filter(entry => entry !== trimmed)].slice(0, SEARCH_HISTORY_LIMIT);
}

export function loadSearchHistory(storage: ReadStorage = safeStorage()): string[] {
  try {
    const parsed = JSON.parse(readItem(storage, SEARCH_HISTORY_KEY) ?? '[]') as unknown;
    return Array.isArray(parsed)
      ? parsed.filter((e): e is string => typeof e === 'string').slice(0, SEARCH_HISTORY_LIMIT)
      : [];
  } catch {
    return [];
  }
}

export function saveSearchHistory(history: string[], storage: WriteStorage = safeStorage()): void {
  writeItem(storage, SEARCH_HISTORY_KEY, JSON.stringify(history.slice(0, SEARCH_HISTORY_LIMIT)));
}

export function loadSnapSetting(storage: ReadStorage = safeStorage()): number | null {
  const raw = readItem(storage, SNAP_KEY);
  return raw === null ? DEFAULT_SNAP_GRID : parseSnapSetting(raw);
}

export function saveSnapSetting(grid: number | null, storage: WriteStorage = safeStorage()): void {
  writeItem(storage, SNAP_KEY, grid === null ? 'off' : String(grid));
}

export function nextSortState(current: { sortBy: BaseObjectSort; sortDir: SortDir },
  column: BaseObjectSort): { sortBy: BaseObjectSort; sortDir: SortDir } {
  if (current.sortBy === column) return { sortBy: column, sortDir: current.sortDir === 'asc' ? 'desc' : 'asc' };
  return { sortBy: column, sortDir: 'asc' };
}

function safeStorage(): Storage | null {
  try {
    return typeof localStorage === 'undefined' ? null : localStorage;
  } catch {
    return null;
  }
}
