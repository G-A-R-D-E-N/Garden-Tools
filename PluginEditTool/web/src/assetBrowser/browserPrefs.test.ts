import { describe, expect, it } from 'vitest';
import {
  BROWSER_COLUMNS, COLUMN_PREFS_KEY, DEFAULT_COLUMN_PREFS, SEARCH_HISTORY_KEY, SEARCH_HISTORY_LIMIT, SNAP_KEY,
  clampWidth, loadColumnPrefs, loadSearchHistory, loadSnapSetting, nextSortState, pushSearchHistory,
  saveColumnPrefs, saveSearchHistory, saveSnapSetting,
} from './browserPrefs';

function memoryStorage(): Storage {
  const map = new Map<string, string>();
  return {
    getItem: (key: string) => map.get(key) ?? null,
    setItem: (key: string, value: string) => { map.set(key, value); },
    removeItem: (key: string) => { map.delete(key); },
    clear: () => map.clear(),
    key: () => null,
    get length() { return map.size; },
  } as Storage;
}

describe('column preferences', () => {
  it('lists the Form ID column under its hex id key', () => {
    expect(BROWSER_COLUMNS).toEqual(['editorId', 'hexId', 'name', 'type', 'modelPath', 'plugin']);
    expect(Object.keys(DEFAULT_COLUMN_PREFS)).toEqual(BROWSER_COLUMNS);
  });

  it('returns the defaults when nothing has been saved or storage is missing', () => {
    expect(loadColumnPrefs(memoryStorage())).toEqual(DEFAULT_COLUMN_PREFS);
    expect(loadColumnPrefs(null)).toEqual(DEFAULT_COLUMN_PREFS);
  });

  it('round-trips a hidden column and a resized width', () => {
    const storage = memoryStorage();
    const prefs = loadColumnPrefs(storage);
    prefs.plugin.visible = false;
    prefs.name.width = 300;
    saveColumnPrefs(prefs, storage);
    const back = loadColumnPrefs(storage);
    expect(back.plugin.visible).toBe(false);
    expect(back.name.width).toBe(300);
    expect(back.editorId).toEqual(DEFAULT_COLUMN_PREFS.editorId);
  });

  it('clamps stored widths into the allowed range', () => {
    const storage = memoryStorage();
    storage.setItem(COLUMN_PREFS_KEY, JSON.stringify({ name: { width: 5000 }, type: { width: 1 } }));
    const back = loadColumnPrefs(storage);
    expect(back.name.width).toBe(800);
    expect(back.type.width).toBe(40);
    expect(clampWidth(12.6)).toBe(40);
  });

  it('falls back to the defaults when the stored JSON is corrupt', () => {
    const storage = memoryStorage();
    storage.setItem(COLUMN_PREFS_KEY, '{not json');
    expect(loadColumnPrefs(storage)).toEqual(DEFAULT_COLUMN_PREFS);
  });
});

describe('search history', () => {
  it('puts the newest term first and removes its older copy', () => {
    expect(pushSearchHistory(['b', 'a'], ' a ')).toEqual(['a', 'b']);
  });

  it('ignores blank terms', () => {
    const history = ['b', 'a'];
    expect(pushSearchHistory(history, '   ')).toBe(history);
  });

  it('keeps only the last ten terms', () => {
    let history: string[] = [];
    for (let i = 0; i < SEARCH_HISTORY_LIMIT + 3; i++) history = pushSearchHistory(history, `term${i}`);
    expect(history).toHaveLength(SEARCH_HISTORY_LIMIT);
    expect(history[0]).toBe(`term${SEARCH_HISTORY_LIMIT + 2}`);
  });

  it('reads back only string entries and tolerates a non-array value', () => {
    const storage = memoryStorage();
    storage.setItem(SEARCH_HISTORY_KEY, JSON.stringify(['ok', 3, null, 'two']));
    expect(loadSearchHistory(storage)).toEqual(['ok', 'two']);
    storage.setItem(SEARCH_HISTORY_KEY, JSON.stringify({ nope: true }));
    expect(loadSearchHistory(storage)).toEqual([]);
  });

  it('saves at most the limit and loads nothing when storage is missing', () => {
    const storage = memoryStorage();
    saveSearchHistory(Array.from({ length: 14 }, (_, i) => `t${i}`), storage);
    expect(loadSearchHistory(storage)).toHaveLength(SEARCH_HISTORY_LIMIT);
    expect(loadSearchHistory(null)).toEqual([]);
  });
});

describe('snap preference', () => {
  it('defaults to a 64 unit grid and remembers off', () => {
    const storage = memoryStorage();
    expect(loadSnapSetting(storage)).toBe(64);
    saveSnapSetting(null, storage);
    expect(storage.getItem(SNAP_KEY)).toBe('off');
    expect(loadSnapSetting(storage)).toBeNull();
  });

  it('remembers a chosen grid size', () => {
    const storage = memoryStorage();
    saveSnapSetting(8, storage);
    expect(loadSnapSetting(storage)).toBe(8);
    expect(loadSnapSetting(null)).toBe(64);
  });
});

describe('sort toggle', () => {
  it('flips direction when the same column is chosen again', () => {
    expect(nextSortState({ sortBy: 'name', sortDir: 'asc' }, 'name')).toEqual({ sortBy: 'name', sortDir: 'desc' });
    expect(nextSortState({ sortBy: 'name', sortDir: 'desc' }, 'name')).toEqual({ sortBy: 'name', sortDir: 'asc' });
  });

  it('starts ascending on a newly chosen column', () => {
    expect(nextSortState({ sortBy: 'name', sortDir: 'desc' }, 'hexId')).toEqual({ sortBy: 'hexId', sortDir: 'asc' });
  });
});
