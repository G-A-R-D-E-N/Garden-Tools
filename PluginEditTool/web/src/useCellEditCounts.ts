import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { getBackend, getCell } from './backend';
import { errorText } from './errors';
import { isPatchLoaded } from './cellPatchTarget';

export type EditCounts = { undo: number; redo: number };

export function useCellEditCounts(cellKey: string | undefined, patchRef: { current: string | null }, onError: (message: string) => void, onForget: () => void) {
  const [editCounts, setEditCounts] = useState<EditCounts>({ undo: 0, redo: 0 });
  const handlersRef = useRef({ onError, onForget });
  useLayoutEffect(() => { handlersRef.current = { onError, onForget }; });

  const refreshEditCounts = useCallback(async (patch: string) => {
    const c = getCell();
    if (!c) return;
    const h = JSON.parse(await c.GetCellEditHistory(patch)) as { undoCount: number; redoCount: number };
    setEditCounts({ undo: h.undoCount, redo: h.redoCount });
  }, []);

  useEffect(() => {
    patchRef.current ??= localStorage.getItem('cell.gizmoPatch') || null;
    const patch = patchRef.current;
    if (!patch) return;
    void (async () => {
      if (await isPatchLoaded(getBackend(), patch) === false && patchRef.current === patch) {
        localStorage.removeItem('cell.gizmoPatch');
        patchRef.current = null;
        setEditCounts({ undo: 0, redo: 0 });
        handlersRef.current.onForget();
        handlersRef.current.onError(`• saved patch ${patch} is not loaded any more; choose a patch plugin again.`);
        return;
      }
      await refreshEditCounts(patch).catch(e => handlersRef.current.onError('✗ edit counts: ' + errorText(e)));
    })();
  }, [cellKey, patchRef, refreshEditCounts]);

  return { editCounts, refreshEditCounts };
}
