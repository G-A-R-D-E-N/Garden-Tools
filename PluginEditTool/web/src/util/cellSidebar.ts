export const SIDEBAR_DEFAULT = 400;
export const SIDEBAR_MIN = 300;
export const SIDEBAR_MAX_FRACTION = 0.6;
export const SIDEBAR_KEY_STEP = 16;

const WIDTH_KEY = 'cell.sidebarWidth';

export const maxSidebarWidth = (windowWidth: number): number =>
  Math.max(SIDEBAR_MIN, Math.floor(windowWidth * SIDEBAR_MAX_FRACTION));

export function clampSidebarWidth(width: number, windowWidth: number): number {
  if (!Number.isFinite(width)) return SIDEBAR_DEFAULT;
  return Math.min(maxSidebarWidth(windowWidth), Math.max(SIDEBAR_MIN, Math.round(width)));
}

export function loadSidebarWidth(windowWidth: number = window.innerWidth): number {
  const stored = localStorage.getItem(WIDTH_KEY);
  return clampSidebarWidth(stored === null ? SIDEBAR_DEFAULT : Number(stored), windowWidth);
}

export function saveSidebarWidth(width: number): void {
  localStorage.setItem(WIDTH_KEY, String(Math.round(width)));
}

export function loadSectionOpen(key: string, fallback = true): boolean {
  const stored = localStorage.getItem(key);
  return stored === null ? fallback : stored === '1';
}

export function saveSectionOpen(key: string, open: boolean): void {
  localStorage.setItem(key, open ? '1' : '0');
}
