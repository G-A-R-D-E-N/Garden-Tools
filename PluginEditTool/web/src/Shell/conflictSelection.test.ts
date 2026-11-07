import { describe, expect, it } from 'vitest';
import { pruneSelection, selectRow } from './conflictSelection';

const order = ['A', 'B', 'C', 'D'];

describe('selectRow', () => {
  it('plain click selects only the clicked row and anchors on it', () => {
    expect(selectRow(order, { A: true, B: true }, 'A', 'C', 'plain')).toEqual({ sel: { C: true }, anchor: 'C' });
  });

  it('toggle flips one row and moves the anchor', () => {
    const first = selectRow(order, { A: true }, 'A', 'C', 'toggle');
    expect(first).toEqual({ sel: { A: true, C: true }, anchor: 'C' });
    expect(selectRow(order, first.sel, first.anchor, 'A', 'toggle')).toEqual({ sel: { A: false, C: true }, anchor: 'A' });
  });

  it('range selects the inclusive span from the anchor in either direction and keeps the anchor', () => {
    expect(selectRow(order, {}, 'B', 'D', 'range')).toEqual({ sel: { B: true, C: true, D: true }, anchor: 'B' });
    expect(selectRow(order, {}, 'C', 'A', 'range')).toEqual({ sel: { A: true, B: true, C: true }, anchor: 'C' });
  });

  it('range without an anchor selects just the clicked row', () => {
    expect(selectRow(order, {}, null, 'B', 'range')).toEqual({ sel: { B: true }, anchor: 'B' });
  });

  it('add-range adds the span from the anchor to the existing selection and keeps the anchor', () => {
    expect(selectRow(order, { A: true }, 'A', 'C', 'add-range')).toEqual({ sel: { A: true, B: true, C: true }, anchor: 'A' });
    expect(selectRow(order, { D: true }, null, 'B', 'add-range')).toEqual({ sel: { D: true, B: true }, anchor: 'B' });
  });
});

describe('pruneSelection', () => {
  it('keeps selected keys that are still listed and drops the rest', () => {
    expect(pruneSelection({ A: true, B: true, C: false }, ['A', 'C'])).toEqual({ A: true });
  });
});
