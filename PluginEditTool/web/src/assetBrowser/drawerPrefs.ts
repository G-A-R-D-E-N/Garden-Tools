export const DRAWER_MIN_HEIGHT = 160;
export const DRAWER_MAX_FRACTION = 0.7;
export const DRAWER_DEFAULT_HEIGHT = 300;
export const DRAWER_KEY_STEP = 16;
export const DRAWER_PREFS_KEY = 'fo4ide.assetBrowser.drawer';
export const TILE_SIZE_MIN = 96;
export const TILE_SIZE_MAX = 240;
export const TILE_SIZE_DEFAULT = 144;

export type BrowserView = 'tiles' | 'list';

export interface DrawerPrefs {
  height: number;
  pinned: boolean;
  tileSize: number;
  view: BrowserView;
  treeOpen: boolean;
  detailOpen: boolean;
}

export const DEFAULT_DRAWER_PREFS: DrawerPrefs = {
  height: DRAWER_DEFAULT_HEIGHT,
  pinned: false,
  tileSize: TILE_SIZE_DEFAULT,
  view: 'tiles',
  treeOpen: true,
  detailOpen: true,
};

type ReadStorage = Pick<Storage, 'getItem'> | null;
type WriteStorage = Pick<Storage, 'setItem'> | null;

export function maxDrawerHeight(panelHeight: number): number {
  const panel = Number.isFinite(panelHeight) ? panelHeight : 0;
  return Math.max(DRAWER_MIN_HEIGHT, Math.floor(panel * DRAWER_MAX_FRACTION));
}

export function clampDrawerHeight(height: number, panelHeight: number): number {
  const wanted = Number.isFinite(height) ? Math.round(height) : DRAWER_DEFAULT_HEIGHT;
  return Math.min(maxDrawerHeight(panelHeight), Math.max(DRAWER_MIN_HEIGHT, wanted));
}

export function resizedDrawerHeight(startHeight: number, startY: number, currentY: number, panelHeight: number): number {
  return clampDrawerHeight(startHeight + (startY - currentY), panelHeight);
}

export function clampTileSize(size: number): number {
  if (!Number.isFinite(size)) return TILE_SIZE_DEFAULT;
  return Math.min(TILE_SIZE_MAX, Math.max(TILE_SIZE_MIN, Math.round(size)));
}

export function loadDrawerPrefs(storage: ReadStorage = safeStorage()): DrawerPrefs {
  const raw = readItem(storage, DRAWER_PREFS_KEY);
  if (raw === null) return { ...DEFAULT_DRAWER_PREFS };
  try {
    const parsed = JSON.parse(raw) as Partial<Record<keyof DrawerPrefs, unknown>>;
    return {
      height: typeof parsed.height === 'number' && Number.isFinite(parsed.height)
        ? Math.max(DRAWER_MIN_HEIGHT, Math.round(parsed.height))
        : DEFAULT_DRAWER_PREFS.height,
      pinned: parsed.pinned === true,
      tileSize: clampTileSize(typeof parsed.tileSize === 'number' ? parsed.tileSize : Number.NaN),
      view: parsed.view === 'list' ? 'list' : 'tiles',
      treeOpen: parsed.treeOpen !== false,
      detailOpen: parsed.detailOpen !== false,
    };
  } catch {
    return { ...DEFAULT_DRAWER_PREFS };
  }
}

export function saveDrawerPrefs(prefs: DrawerPrefs, storage: WriteStorage = safeStorage()): void {
  writeItem(storage, DRAWER_PREFS_KEY, JSON.stringify(prefs));
}

export interface KeyChord {
  key: string;
  ctrlKey: boolean;
  metaKey: boolean;
  altKey: boolean;
  shiftKey: boolean;
  editable: boolean;
}

export function drawerHotkeyAction(chord: KeyChord): 'toggle' | 'search' | null {
  const mod = chord.ctrlKey || chord.metaKey;
  if (mod && !chord.altKey && chord.key === ' ') return 'toggle';
  if (mod && !chord.altKey && !chord.shiftKey && chord.key.toLowerCase() === 'f') return 'search';
  if (chord.editable || mod || chord.altKey || chord.shiftKey) return null;
  return chord.key === '`' ? 'toggle' : null;
}

export function isEditableTarget(tagName: string, contentEditable: boolean): boolean {
  return contentEditable || tagName === 'INPUT' || tagName === 'TEXTAREA' || tagName === 'SELECT';
}

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

function safeStorage(): Storage | null {
  try {
    return typeof localStorage === 'undefined' ? null : localStorage;
  } catch {
    return null;
  }
}
