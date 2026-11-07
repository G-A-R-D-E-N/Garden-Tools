export type SelectMode = 'plain' | 'toggle' | 'range' | 'add-range';
export type ConflictSelection = Record<string, boolean>;

export function modeOf(e: { ctrlKey: boolean; metaKey: boolean; shiftKey: boolean }): SelectMode {
  const additive = e.ctrlKey || e.metaKey;
  if (e.shiftKey) return additive ? 'add-range' : 'range';
  return additive ? 'toggle' : 'plain';
}

export function selectRow(
  order: string[], sel: ConflictSelection, anchor: string | null, key: string, mode: SelectMode,
): { sel: ConflictSelection; anchor: string | null } {
  if (mode === 'toggle') return { sel: { ...sel, [key]: !sel[key] }, anchor: key };
  const base: ConflictSelection = mode === 'add-range' ? sel : {};
  const from = anchor === null ? -1 : order.indexOf(anchor);
  const to = order.indexOf(key);
  if (mode === 'plain' || from < 0 || to < 0) return { sel: { ...base, [key]: true }, anchor: key };
  const [lo, hi] = from <= to ? [from, to] : [to, from];
  return { sel: { ...base, ...Object.fromEntries(order.slice(lo, hi + 1).map(k => [k, true])) }, anchor };
}

export function pruneSelection(sel: ConflictSelection, keys: string[]): ConflictSelection {
  const live = new Set(keys);
  return Object.fromEntries(Object.entries(sel).filter(([k, on]) => on && live.has(k)));
}
