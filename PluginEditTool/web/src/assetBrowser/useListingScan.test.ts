import { describe, expect, it, vi } from 'vitest';
import { scanAll, scanKey, type ScanProgress } from './useListingScan';
import { DEFAULT_BROWSER_QUERY } from './baseObjects';

function pageReply(offset: number, limit: number, total: number, typeCounts: Record<string, number> = { STAT: total }) {
  const count = Math.max(0, Math.min(limit, total - offset));
  const items = Array.from({ length: count }, (_, i) => ({
    formKey: `F${offset + i}`,
    hexId: '00000001',
    editorId: `Obj${offset + i}`,
    name: '',
    signature: 'STAT',
    typeName: 'Static',
    modelPath: '',
    winningPlugin: 'Fallout4.esm',
    deleted: false,
    hasModel: false,
  }));
  return JSON.stringify({ total, offset, limit, sortBy: 'editorId', sortDir: 'asc', typeCounts, items });
}

async function run(total: number, opts: { pageSize?: number; cap?: number; alive?: () => boolean } = {}) {
  const fetchPage = vi.fn(async (offset: number, limit: number) => pageReply(offset, limit, total));
  const progress: ScanProgress[] = [];
  await scanAll(fetchPage, {
    pageSize: opts.pageSize ?? 200,
    cap: opts.cap ?? 20000,
    alive: opts.alive ?? (() => true),
    onProgress: p => progress.push(p),
  });
  return { fetchPage, progress, last: progress.at(-1) };
}

describe('listing scan paging', () => {
  it('walks every page at 200 rows until the total is reached', async () => {
    const { fetchPage, last, progress } = await run(450);
    expect(fetchPage.mock.calls).toEqual([[0, 200], [200, 200], [400, 200]]);
    expect(last?.items).toHaveLength(450);
    expect(last?.done).toBe(true);
    expect(last?.capped).toBe(false);
    expect(progress.map(p => p.items.length)).toEqual([200, 400, 450]);
    expect(progress[0].done).toBe(false);
  });

  it('keeps the type counts from the first page', async () => {
    const fetchPage = vi.fn(async (offset: number, limit: number) => pageReply(offset, limit, 3, { DOOR: 3 }));
    let last: ScanProgress | undefined;
    await scanAll(fetchPage, { pageSize: 200, cap: 20000, alive: () => true, onProgress: p => { last = p; } });
    expect(last?.typeCounts).toEqual({ DOOR: 3 });
  });

  it('stops at the cap and reports that the list was capped', async () => {
    const { fetchPage, last } = await run(10, { pageSize: 4, cap: 6 });
    expect(fetchPage.mock.calls).toEqual([[0, 4], [4, 4]]);
    expect(last?.items).toHaveLength(6);
    expect(last?.done).toBe(true);
    expect(last?.capped).toBe(true);
  });

  it('stops quietly when the scan is no longer wanted', async () => {
    let live = true;
    const fetchPage = vi.fn(async (offset: number, limit: number) => {
      live = false;
      return pageReply(offset, limit, 900);
    });
    const onProgress = vi.fn();
    await scanAll(fetchPage, { pageSize: 200, cap: 20000, alive: () => live, onProgress });
    expect(fetchPage).toHaveBeenCalledTimes(1);
    expect(onProgress).not.toHaveBeenCalled();
  });

  it('reports a bridge error and an empty result as finished', async () => {
    const failing = await run(0);
    expect(failing.last?.items).toEqual([]);
    expect(failing.last?.done).toBe(true);

    const errors: ScanProgress[] = [];
    await scanAll(async () => JSON.stringify({ error: 'bad query' }), {
      pageSize: 200,
      cap: 20000,
      alive: () => true,
      onProgress: p => errors.push(p),
    });
    expect(errors[0].error).toBe('bad query');
    expect(errors[0].done).toBe(true);
  });

  it('builds a stable key from the whole filter state', () => {
    expect(scanKey(DEFAULT_BROWSER_QUERY)).toBe(scanKey({ ...DEFAULT_BROWSER_QUERY }));
    expect(scanKey(DEFAULT_BROWSER_QUERY)).not.toBe(scanKey({ ...DEFAULT_BROWSER_QUERY, type: 'DOOR' }));
  });
});
