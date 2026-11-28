import { describe, expect, it } from 'vitest';
import {
  DEFAULT_DRAWER_PREFS, DRAWER_MIN_HEIGHT, DRAWER_PREFS_KEY, clampDrawerHeight, clampTileSize, drawerHotkeyAction,
  isEditableTarget, loadDrawerPrefs, maxDrawerHeight, resizedDrawerHeight, saveDrawerPrefs,
} from './drawerPrefs';

function memoryStorage(initial: Record<string, string> = {}): Storage {
  const map = new Map<string, string>(Object.entries(initial));
  return {
    getItem: (key: string) => map.get(key) ?? null,
    setItem: (key: string, value: string) => { map.set(key, value); },
  } as unknown as Storage;
}

const chord = {
  key: '',
  ctrlKey: false,
  metaKey: false,
  altKey: false,
  shiftKey: false,
  editable: false,
};

describe('drawer height', () => {
  it('caps the drawer at 70 percent of the panel and never below the minimum', () => {
    expect(maxDrawerHeight(1000)).toBe(700);
    expect(maxDrawerHeight(100)).toBe(DRAWER_MIN_HEIGHT);
  });

  it('clamps requested heights into the allowed range', () => {
    expect(clampDrawerHeight(50, 1000)).toBe(160);
    expect(clampDrawerHeight(900, 1000)).toBe(700);
    expect(clampDrawerHeight(300.4, 1000)).toBe(300);
    expect(clampDrawerHeight(Number.NaN, 1000)).toBe(300);
  });

  it('resizes from the handle: dragging up grows the drawer', () => {
    expect(resizedDrawerHeight(300, 500, 400, 1000)).toBe(400);
    expect(resizedDrawerHeight(300, 500, 900, 1000)).toBe(160);
  });
});

describe('drawer and browser preferences', () => {
  it('clamps tile sizes into range', () => {
    expect(clampTileSize(10)).toBe(96);
    expect(clampTileSize(500)).toBe(240);
    expect(clampTileSize(150.4)).toBe(150);
    expect(clampTileSize(Number.NaN)).toBe(144);
  });

  it('returns defaults when nothing is stored or the stored text is corrupt', () => {
    expect(loadDrawerPrefs(memoryStorage())).toEqual(DEFAULT_DRAWER_PREFS);
    expect(loadDrawerPrefs(memoryStorage({ [DRAWER_PREFS_KEY]: '{not json' }))).toEqual(DEFAULT_DRAWER_PREFS);
    expect(loadDrawerPrefs(null)).toEqual(DEFAULT_DRAWER_PREFS);
  });

  it('sanitises stored values field by field', () => {
    const stored = JSON.stringify({ height: 50, pinned: true, tileSize: 500, view: 'list', treeOpen: false });
    expect(loadDrawerPrefs(memoryStorage({ [DRAWER_PREFS_KEY]: stored }))).toEqual({
      height: 160,
      pinned: true,
      tileSize: 240,
      view: 'list',
      treeOpen: false,
      detailOpen: true,
    });
    const bad = JSON.stringify({ height: 'tall', pinned: 'yes', view: 'grid' });
    expect(loadDrawerPrefs(memoryStorage({ [DRAWER_PREFS_KEY]: bad }))).toEqual(DEFAULT_DRAWER_PREFS);
  });

  it('round-trips saved preferences', () => {
    const storage = memoryStorage();
    const prefs = { ...DEFAULT_DRAWER_PREFS, height: 420, pinned: true, detailOpen: false };
    saveDrawerPrefs(prefs, storage);
    expect(loadDrawerPrefs(storage)).toEqual(prefs);
  });

  it('ignores storage that throws', () => {
    const broken = {
      getItem: () => { throw new Error('denied'); },
      setItem: () => { throw new Error('full'); },
    } as unknown as Storage;
    expect(loadDrawerPrefs(broken)).toEqual(DEFAULT_DRAWER_PREFS);
    expect(() => saveDrawerPrefs(DEFAULT_DRAWER_PREFS, broken)).not.toThrow();
  });
});

describe('drawer hotkeys', () => {
  it('toggles on Ctrl+Space and on a bare backtick outside text fields', () => {
    expect(drawerHotkeyAction({ ...chord, key: ' ', ctrlKey: true })).toBe('toggle');
    expect(drawerHotkeyAction({ ...chord, key: '`' })).toBe('toggle');
    expect(drawerHotkeyAction({ ...chord, key: '`', editable: true })).toBeNull();
    expect(drawerHotkeyAction({ ...chord, key: '`', shiftKey: true })).toBeNull();
  });

  it('focuses search on Ctrl+F and Cmd+F only', () => {
    expect(drawerHotkeyAction({ ...chord, key: 'f', ctrlKey: true })).toBe('search');
    expect(drawerHotkeyAction({ ...chord, key: 'F', metaKey: true })).toBe('search');
    expect(drawerHotkeyAction({ ...chord, key: 'f' })).toBeNull();
    expect(drawerHotkeyAction({ ...chord, key: 'f', ctrlKey: true, shiftKey: true })).toBeNull();
  });

  it('recognises editable targets', () => {
    expect(isEditableTarget('INPUT', false)).toBe(true);
    expect(isEditableTarget('TEXTAREA', false)).toBe(true);
    expect(isEditableTarget('DIV', true)).toBe(true);
    expect(isEditableTarget('DIV', false)).toBe(false);
  });
});
