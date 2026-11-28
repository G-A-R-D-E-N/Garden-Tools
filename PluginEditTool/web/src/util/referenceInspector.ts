import type { ReferenceDetails } from '../referenceDetails';

export const INSPECTOR_MIN = 320;
export const INSPECTOR_DEFAULT = 420;
export const INSPECTOR_MAX_FRACTION = 0.6;
export const INSPECTOR_KEY_STEP = 16;

const WIDTH_KEY = 'cell.inspectorWidth';

export type InspectorTab = 'reference' | 'scripts' | 'plugins' | 'edit';

export const maxInspectorWidth = (windowWidth: number): number =>
  Math.max(INSPECTOR_MIN, Math.floor(windowWidth * INSPECTOR_MAX_FRACTION));

export function clampInspectorWidth(width: number, windowWidth: number): number {
  if (!Number.isFinite(width)) return INSPECTOR_DEFAULT;
  return Math.min(maxInspectorWidth(windowWidth), Math.max(INSPECTOR_MIN, Math.round(width)));
}

const hasScriptSet = (set: unknown): boolean => {
  if (typeof set !== 'object' || set === null) return false;
  const { scripts, fragmentBytes } = set as { scripts?: unknown; fragmentBytes?: unknown };
  return (Array.isArray(scripts) && scripts.length > 0) || (typeof fragmentBytes === 'number' && fragmentBytes > 0);
};

export const hasReferenceScripts = (details: ReferenceDetails | null): boolean =>
  details !== null && (hasScriptSet(details.scripts) || hasScriptSet(details.baseScripts));

export function loadInspectorWidth(windowWidth = window.innerWidth): number {
  const stored = localStorage.getItem(WIDTH_KEY);
  return clampInspectorWidth(stored === null ? INSPECTOR_DEFAULT : Number(stored), windowWidth);
}

export function saveInspectorWidth(width: number): void {
  localStorage.setItem(WIDTH_KEY, String(Math.round(width)));
}
