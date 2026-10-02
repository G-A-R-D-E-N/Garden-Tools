import { describe, expect, it } from 'vitest';
import { HISTORY_CAP, pushHistory, redoHistory, undoHistory, type UndoHistory } from './undoHistory';

const empty: UndoHistory<string> = { undo: [], redo: [] };

describe('visibility undo history', () => {
  it('toggle, undo, redo round-trips the snapshots', () => {
    const toggled = pushHistory(empty, 'shown');
    expect(toggled).toEqual({ undo: ['shown'], redo: [] });
    const undone = undoHistory(toggled, 'hidden');
    expect(undone).toEqual({ undo: [], redo: ['hidden'] });
    expect(redoHistory(undone, 'shown')).toEqual({ undo: ['shown'], redo: [] });
  });

  it('caps the undo stack, dropping the oldest snapshot', () => {
    let h = empty;
    for (let i = 0; i <= HISTORY_CAP; i++) h = pushHistory(h, `s${i}`);
    expect(h.undo).toHaveLength(HISTORY_CAP);
    expect(h.undo[0]).toBe('s1');
    expect(h.undo.at(-1)).toBe(`s${HISTORY_CAP}`);
  });

  it('a new toggle after undo clears redo', () => {
    const undone = undoHistory(pushHistory(pushHistory(empty, 'a'), 'b'), 'c');
    expect(undone.redo).toEqual(['c']);
    expect(pushHistory(undone, 'd')).toEqual({ undo: ['a', 'd'], redo: [] });
  });
});
