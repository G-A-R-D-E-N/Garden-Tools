export interface SelectionState {
  keys: string[];
  anchor: string | null;
}

export const EMPTY_SELECTION: SelectionState = { keys: [], anchor: null };

export function selectOnly(key: string): SelectionState {
  return { keys: [key], anchor: key };
}

export function toggleSelection(state: SelectionState, key: string): SelectionState {
  const has = state.keys.includes(key);
  return {
    keys: has ? state.keys.filter(k => k !== key) : [...state.keys, key],
    anchor: key,
  };
}

export function rangeSelection(state: SelectionState, order: string[], key: string): SelectionState {
  const anchorIndex = state.anchor === null ? -1 : order.indexOf(state.anchor);
  const targetIndex = order.indexOf(key);
  if (anchorIndex < 0 || targetIndex < 0) return selectOnly(key);
  const [from, to] = anchorIndex <= targetIndex ? [anchorIndex, targetIndex] : [targetIndex, anchorIndex];
  return { keys: order.slice(from, to + 1), anchor: state.anchor };
}

export function selectAll(order: string[]): SelectionState {
  return { keys: [...order], anchor: order[0] ?? null };
}

export function clickSelection(state: SelectionState, order: string[], key: string,
  modifiers: { ctrl: boolean; shift: boolean }): SelectionState {
  if (modifiers.shift) return rangeSelection(state, order, key);
  if (modifiers.ctrl) return toggleSelection(state, key);
  return selectOnly(key);
}

export function pruneSelection(state: SelectionState, order: string[]): SelectionState {
  const visible = new Set(order);
  const keys = state.keys.filter(k => visible.has(k));
  return { keys, anchor: state.anchor !== null && visible.has(state.anchor) ? state.anchor : null };
}
