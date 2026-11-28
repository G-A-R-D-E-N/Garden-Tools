export const TILE_GAP = 8;

export interface RowWindow {
  first: number;
  last: number;
}

export function columnsFor(width: number, tileWidth: number, gap: number = TILE_GAP): number {
  if (!(width > 0) || !(tileWidth > 0)) return 1;
  return Math.max(1, Math.floor((width + gap) / (tileWidth + gap)));
}

export function rowsFor(count: number, columns: number): number {
  return columns > 0 ? Math.ceil(count / columns) : 0;
}

export function rowWindow(opts: {
  rowCount: number;
  rowHeight: number;
  scrollTop: number;
  viewportHeight: number;
  overscan: number;
}): RowWindow {
  const { rowCount, rowHeight, overscan } = opts;
  if (rowCount <= 0 || !(rowHeight > 0)) return { first: 0, last: 0 };
  const top = Math.max(0, opts.scrollTop);
  const first = Math.min(rowCount, Math.max(0, Math.floor(top / rowHeight) - overscan));
  const bottom = top + Math.max(0, opts.viewportHeight);
  const last = Math.min(rowCount, Math.max(first, Math.ceil(bottom / rowHeight) + overscan));
  return { first, last };
}

export function itemRange(window: RowWindow, columns: number, count: number): { start: number; end: number } {
  return {
    start: Math.min(count, window.first * columns),
    end: Math.min(count, window.last * columns),
  };
}
