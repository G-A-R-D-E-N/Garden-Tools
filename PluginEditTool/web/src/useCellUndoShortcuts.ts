import { useEffect, useLayoutEffect, useRef } from 'react';

export type UndoRedoKey = 'undo' | 'redo';

export function isEditableTarget(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false;
  if (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.tagName === 'SELECT') return true;
  const editable = target.closest('[contenteditable]');
  return editable !== null && editable.getAttribute('contenteditable') !== 'false';
}

export function undoRedoKey(e: Pick<KeyboardEvent, 'key' | 'ctrlKey' | 'metaKey' | 'altKey' | 'shiftKey' | 'repeat'>): UndoRedoKey | null {
  if (e.altKey || e.repeat || !(e.ctrlKey || e.metaKey)) return null;
  const k = e.key.toLowerCase();
  if (k === 'z') return e.shiftKey ? 'redo' : 'undo';
  if (k === 'y' && !e.shiftKey) return 'redo';
  return null;
}

export function useCellUndoShortcuts(onUndo: () => void, onRedo: () => void) {
  const undoRef = useRef(onUndo);
  const redoRef = useRef(onRedo);
  useLayoutEffect(() => {
    undoRef.current = onUndo;
    redoRef.current = onRedo;
  });
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (isEditableTarget(e.target)) return;
      const kind = undoRedoKey(e);
      if (!kind) return;
      e.preventDefault();
      if (kind === 'undo') undoRef.current();
      else redoRef.current();
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);
}
