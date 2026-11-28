export type CellEditKey = 'delete' | 'duplicate';

export function cellEditKey(e: Pick<KeyboardEvent, 'key' | 'ctrlKey' | 'altKey' | 'shiftKey'>): CellEditKey | null {
  const k = e.key.toLowerCase();
  if (k === 'delete' && !e.ctrlKey && !e.altKey && !e.shiftKey) return 'delete';
  if (k === 'd' && e.ctrlKey && !e.altKey && !e.shiftKey) return 'duplicate';
  return null;
}
