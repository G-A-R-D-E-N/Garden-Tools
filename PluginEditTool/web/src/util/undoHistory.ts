export type UndoHistory<T> = { undo: T[]; redo: T[] };
export const HISTORY_CAP = 50;

export const pushHistory = <T>(h: UndoHistory<T>, snap: T): UndoHistory<T> =>
  ({ undo: [...h.undo, snap].slice(-HISTORY_CAP), redo: [] });
export const undoHistory = <T>(h: UndoHistory<T>, cur: T): UndoHistory<T> =>
  ({ undo: h.undo.slice(0, -1), redo: [...h.redo, cur] });
export const redoHistory = <T>(h: UndoHistory<T>, cur: T): UndoHistory<T> =>
  ({ undo: [...h.undo, cur], redo: h.redo.slice(0, -1) });
