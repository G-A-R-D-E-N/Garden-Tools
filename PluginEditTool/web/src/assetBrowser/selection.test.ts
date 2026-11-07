import { describe, expect, it } from 'vitest';
import {
  clickSelection, pruneSelection, rangeSelection, selectAll, selectOnly, toggleSelection, EMPTY_SELECTION,
} from './selection';

const order = ['a', 'b', 'c', 'd', 'e'];
const none = { ctrl: false, shift: false };

describe('selection math', () => {
  it('starts empty and a single select anchors on that row', () => {
    expect(EMPTY_SELECTION).toEqual({ keys: [], anchor: null });
    expect(selectOnly('c')).toEqual({ keys: ['c'], anchor: 'c' });
  });

  it('a plain click replaces the whole selection', () => {
    const state = { keys: ['a', 'b'], anchor: 'b' };
    expect(clickSelection(state, order, 'd', none)).toEqual({ keys: ['d'], anchor: 'd' });
  });

  it('ctrl click adds a row and then removes it again', () => {
    const added = clickSelection({ keys: ['a'], anchor: 'a' }, order, 'c', { ctrl: true, shift: false });
    expect(added).toEqual({ keys: ['a', 'c'], anchor: 'c' });
    const removed = toggleSelection(added, 'a');
    expect(removed).toEqual({ keys: ['c'], anchor: 'a' });
  });

  it('shift click selects the inclusive range from the anchor forwards', () => {
    const state = { keys: ['b'], anchor: 'b' };
    expect(clickSelection(state, order, 'd', { ctrl: false, shift: true })).toEqual({
      keys: ['b', 'c', 'd'],
      anchor: 'b',
    });
  });

  it('shift click selects the inclusive range when the target is above the anchor', () => {
    const state = { keys: ['d'], anchor: 'd' };
    expect(rangeSelection(state, order, 'b')).toEqual({ keys: ['b', 'c', 'd'], anchor: 'd' });
  });

  it('shift click with no anchor falls back to a single select', () => {
    expect(rangeSelection(EMPTY_SELECTION, order, 'c')).toEqual({ keys: ['c'], anchor: 'c' });
  });

  it('shift click onto a row that is not listed falls back to a single select', () => {
    expect(rangeSelection({ keys: ['a'], anchor: 'a' }, order, 'zz')).toEqual({ keys: ['zz'], anchor: 'zz' });
  });

  it('select all takes every loaded row and anchors on the first', () => {
    expect(selectAll(order)).toEqual({ keys: order, anchor: 'a' });
    expect(selectAll([])).toEqual({ keys: [], anchor: null });
  });

  it('pruning drops rows that are no longer loaded and clears a stale anchor', () => {
    expect(pruneSelection({ keys: ['a', 'z'], anchor: 'z' }, order)).toEqual({ keys: ['a'], anchor: null });
    expect(pruneSelection({ keys: ['a', 'b'], anchor: 'b' }, order)).toEqual({ keys: ['a', 'b'], anchor: 'b' });
  });
});
