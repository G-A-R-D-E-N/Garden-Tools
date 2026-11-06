import { useCallback, useEffect, useRef, useState, type KeyboardEvent } from 'react';
import { pushHistory, redoHistory, undoHistory, type UndoHistory } from '../util/undoHistory';

export interface UndoEntry {
  label: string;
  undo: () => void;
  redo: () => void;
}

const isTypingTarget = (target: EventTarget) =>
  target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target instanceof HTMLSelectElement ||
  (target instanceof HTMLElement && target.closest('[contenteditable]:not([contenteditable="false"])') !== null);

export function useUndoStack() {
  const [history, setHistory] = useState<UndoHistory<UndoEntry>>({ undo: [], redo: [] });
  const scopeRef = useRef<HTMLElement>(null);
  useEffect(() => { scopeRef.current?.focus(); }, []);

  const push = useCallback((entry: UndoEntry) => setHistory(h => pushHistory(h, entry)), []);

  const undo = useCallback(() => {
    const entry = history.undo.at(-1);
    if (!entry) return;
    entry.undo();
    setHistory(h => undoHistory(h, entry));
  }, [history]);

  const redo = useCallback(() => {
    const entry = history.redo.at(-1);
    if (!entry) return;
    entry.redo();
    setHistory(h => redoHistory(h, entry));
  }, [history]);

  const onKeyDown = (e: KeyboardEvent<HTMLElement>) => {
    if (!(e.ctrlKey || e.metaKey) || e.altKey || isTypingTarget(e.target)) return;
    const key = e.key.toLowerCase();
    if (key === 'z' && !e.shiftKey) {
      e.preventDefault();
      undo();
    } else if (key === 'y' || key === 'z') {
      e.preventDefault();
      redo();
    }
  };

  return {
    push,
    undo,
    redo,
    canUndo: history.undo.length > 0,
    canRedo: history.redo.length > 0,
    scopeRef,
    onKeyDown,
  };
}
