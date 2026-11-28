import { describe, expect, it, vi } from 'vitest';
import type { BaseObjectQuery } from './baseObjects';

async function loadPrewarm() {
  vi.resetModules();
  return import('./prewarmBaseObjects');
}

const settle = () => new Promise(resolve => setTimeout(resolve, 0));

describe('prewarmBaseObjects', () => {
  it('sends one limit-1 query with hideDeleted explicit and ignores later calls', async () => {
    const { prewarmBaseObjects } = await loadPrewarm();
    const ListBaseObjects = vi.fn<(query: BaseObjectQuery) => Promise<string>>(async () => '{}');
    prewarmBaseObjects({ ListBaseObjects });
    prewarmBaseObjects({ ListBaseObjects });
    await settle();
    expect(ListBaseObjects).toHaveBeenCalledTimes(1);
    expect(ListBaseObjects).toHaveBeenCalledWith(expect.objectContaining({ offset: 0, limit: 1, hideDeleted: false }));
  });

  it('swallows a rejected prewarm and still counts it as started', async () => {
    const { prewarmBaseObjects } = await loadPrewarm();
    const ListBaseObjects = vi.fn<(query: BaseObjectQuery) => Promise<string>>(async () => {
      throw new Error('bridge down');
    });
    expect(() => prewarmBaseObjects({ ListBaseObjects })).not.toThrow();
    await settle();
    prewarmBaseObjects({ ListBaseObjects });
    await settle();
    expect(ListBaseObjects).toHaveBeenCalledTimes(1);
  });

  it('waits for a bridge before marking the prewarm as started', async () => {
    const { prewarmBaseObjects } = await loadPrewarm();
    prewarmBaseObjects();
    const ListBaseObjects = vi.fn<(query: BaseObjectQuery) => Promise<string>>(async () => '{}');
    prewarmBaseObjects({ ListBaseObjects });
    await settle();
    expect(ListBaseObjects).toHaveBeenCalledTimes(1);
  });
});
