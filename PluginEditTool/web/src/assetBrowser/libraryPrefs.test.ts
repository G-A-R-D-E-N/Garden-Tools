import { describe, expect, it } from 'vitest';
import type { BaseObjectItem } from './baseObjects';
import {
  FAVOURITES_KEY, RECENT_KEY, RECENT_LIMIT, isFavourite, loadLibrary, pushRecent, saveLibrary, toggleFavourite,
} from './libraryPrefs';

function item(formKey: string, editorId = formKey): BaseObjectItem {
  return {
    formKey,
    hexId: '00000001',
    editorId,
    name: '',
    signature: 'STAT',
    typeName: 'Static',
    modelPath: '',
    winningPlugin: 'Fallout4.esm',
    deleted: false,
    hasModel: false,
  };
}

function memoryStorage(initial: Record<string, string> = {}): Storage {
  const map = new Map<string, string>(Object.entries(initial));
  return {
    getItem: (key: string) => map.get(key) ?? null,
    setItem: (key: string, value: string) => { map.set(key, value); },
  } as unknown as Storage;
}

describe('favourites', () => {
  it('adds a favourite to the front and removes it on the second toggle', () => {
    const added = toggleFavourite([item('a')], item('b'));
    expect(added.map(entry => entry.formKey)).toEqual(['b', 'a']);
    expect(isFavourite(added, 'b')).toBe(true);
    const removed = toggleFavourite(added, item('b'));
    expect(removed.map(entry => entry.formKey)).toEqual(['a']);
    expect(isFavourite(removed, 'b')).toBe(false);
  });
});

describe('recently used', () => {
  it('moves reopened items to the front without duplicates', () => {
    const recent = pushRecent([item('a'), item('b')], [item('b'), item('c')]);
    expect(recent.map(entry => entry.formKey)).toEqual(['b', 'c', 'a']);
  });

  it('keeps at most RECENT_LIMIT entries', () => {
    const many = Array.from({ length: RECENT_LIMIT + 5 }, (_, i) => item(`k${i}`));
    expect(pushRecent([], many)).toHaveLength(RECENT_LIMIT);
  });
});

describe('library persistence', () => {
  it('round-trips favourites and recent items', () => {
    const storage = memoryStorage();
    const state = { favourites: [item('a', 'Alpha')], recent: [item('b', 'Beta')] };
    saveLibrary(state, storage);
    expect(loadLibrary(storage)).toEqual(state);
  });

  it('drops malformed stored entries and tolerates corrupt JSON', () => {
    const stored = JSON.stringify([{ formKey: 'ok', editorId: 'Good', deleted: true }, { editorId: 'no key' }, 7]);
    const loaded = loadLibrary(memoryStorage({ [FAVOURITES_KEY]: stored, [RECENT_KEY]: '{oops' }));
    expect(loaded.favourites).toHaveLength(1);
    expect(loaded.favourites[0]).toMatchObject({ formKey: 'ok', editorId: 'Good', deleted: true, name: '' });
    expect(loaded.recent).toEqual([]);
  });

  it('returns empty lists when nothing is stored', () => {
    expect(loadLibrary(memoryStorage())).toEqual({ favourites: [], recent: [] });
    expect(loadLibrary(null)).toEqual({ favourites: [], recent: [] });
  });
});
