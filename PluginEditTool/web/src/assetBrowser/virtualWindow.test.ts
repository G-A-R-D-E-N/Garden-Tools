import { describe, expect, it } from 'vitest';
import { columnsFor, itemRange, rowWindow, rowsFor } from './virtualWindow';

describe('virtual window math', () => {
  it('fits as many tile columns as the width allows, never fewer than one', () => {
    expect(columnsFor(800, 144)).toBe(5);
    expect(columnsFor(152, 144)).toBe(1);
    expect(columnsFor(100, 144)).toBe(1);
    expect(columnsFor(0, 144)).toBe(1);
    expect(columnsFor(800, 0)).toBe(1);
  });

  it('counts rows from the item count and column count', () => {
    expect(rowsFor(10, 4)).toBe(3);
    expect(rowsFor(8, 4)).toBe(2);
    expect(rowsFor(0, 4)).toBe(0);
    expect(rowsFor(5, 0)).toBe(0);
  });

  it('windows the visible rows with overscan on both sides', () => {
    expect(rowWindow({ rowCount: 100, rowHeight: 50, scrollTop: 500, viewportHeight: 200, overscan: 1 }))
      .toEqual({ first: 9, last: 15 });
  });

  it('clamps the window at the top and the bottom of the list', () => {
    expect(rowWindow({ rowCount: 100, rowHeight: 50, scrollTop: 0, viewportHeight: 120, overscan: 2 }))
      .toEqual({ first: 0, last: 5 });
    expect(rowWindow({ rowCount: 10, rowHeight: 50, scrollTop: 10000, viewportHeight: 200, overscan: 1 }))
      .toEqual({ first: 10, last: 10 });
    expect(rowWindow({ rowCount: 0, rowHeight: 50, scrollTop: 0, viewportHeight: 200, overscan: 1 }))
      .toEqual({ first: 0, last: 0 });
  });

  it('ignores a negative scroll position and a missing row height', () => {
    expect(rowWindow({ rowCount: 20, rowHeight: 50, scrollTop: -40, viewportHeight: 100, overscan: 0 }))
      .toEqual({ first: 0, last: 2 });
    expect(rowWindow({ rowCount: 20, rowHeight: 0, scrollTop: 0, viewportHeight: 100, overscan: 0 }))
      .toEqual({ first: 0, last: 0 });
  });

  it('maps a row window to item indices and never exceeds the count', () => {
    expect(itemRange({ first: 9, last: 15 }, 5, 100)).toEqual({ start: 45, end: 75 });
    expect(itemRange({ first: 9, last: 15 }, 5, 70)).toEqual({ start: 45, end: 70 });
  });
});
