import { useState } from 'react';
import { pushHistory, redoHistory, undoHistory, type UndoHistory } from './util/undoHistory';

type VisSnapshot = { types: Set<string>; refs: Set<string> };
const EMPTY: UndoHistory<VisSnapshot> = { undo: [], redo: [] };

export default function useCellVisibility() {
  const [hiddenTypes, setHiddenTypes] = useState<Set<string>>(new Set());
  const [hiddenRefs, setHiddenRefs] = useState<Set<string>>(new Set());
  const [history, setHistory] = useState<UndoHistory<VisSnapshot>>(EMPTY);

  const snapshot = (): VisSnapshot => ({ types: new Set(hiddenTypes), refs: new Set(hiddenRefs) });
  const pushUndo = () => {
    const snap = snapshot();
    setHistory(h => pushHistory(h, snap));
  };
  const apply = (s: VisSnapshot) => { setHiddenTypes(s.types); setHiddenRefs(s.refs); };
  const undo = () => {
    const prev = history.undo.at(-1);
    if (!prev) return;
    const cur = snapshot();
    setHistory(h => undoHistory(h, cur));
    apply(prev);
  };
  const redo = () => {
    const next = history.redo.at(-1);
    if (!next) return;
    const cur = snapshot();
    setHistory(h => redoHistory(h, cur));
    apply(next);
  };
  const toggleType = (t: string) => {
    pushUndo();
    setHiddenTypes(prev => {
      const next = new Set(prev);
      if (next.has(t)) next.delete(t); else next.add(t);
      return next;
    });
  };
  const showAllTypes = () => { pushUndo(); setHiddenTypes(new Set()); };
  const hideRef = (key: string) => {
    pushUndo();
    setHiddenRefs(prev => new Set(prev).add(key));
  };
  const unhideAll = () => { pushUndo(); setHiddenRefs(new Set()); };
  const reset = () => {
    setHiddenTypes(new Set());
    setHiddenRefs(new Set());
    setHistory(EMPTY);
  };

  return { hiddenTypes, hiddenRefs, history, undo, redo, toggleType, showAllTypes, hideRef, unhideAll, reset };
}
