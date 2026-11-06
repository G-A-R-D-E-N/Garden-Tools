export type CellEditKey = 'delete' | 'duplicate' | 'undo' | 'redo';

export function cellEditKey(e: Pick<KeyboardEvent, 'key' | 'ctrlKey' | 'altKey' | 'shiftKey' | 'repeat'>): CellEditKey | null {
  if (e.repeat) return null;
  const k = e.key.toLowerCase();
  if (k === 'delete' && !e.ctrlKey && !e.altKey && !e.shiftKey) return 'delete';
  if (k === 'd' && e.ctrlKey && !e.altKey && !e.shiftKey) return 'duplicate';
  if (k === 'z' && e.ctrlKey) return 'undo';
  if (k === 'y' && e.ctrlKey) return 'redo';
  return null;
}
