import { describe, expect, it, vi } from 'vitest';
import { PreviewCache, previewKey, type PreviewStore } from './previewCache';

function fakeStore(initial: Record<string, { url: string; size: null }> = {}): PreviewStore & {
  get: ReturnType<typeof vi.fn>;
  put: ReturnType<typeof vi.fn>;
} {
  const map = new Map(Object.entries(initial));
  return {
    get: vi.fn(async (key: string) => map.get(key)),
    put: vi.fn(async (key: string, entry: { url: string; size: null }) => { map.set(key, entry); }),
  } as unknown as PreviewStore & { get: ReturnType<typeof vi.fn>; put: ReturnType<typeof vi.fn> };
}

describe('preview key', () => {
  it('is versioned, slash-normalised and case-insensitive', () => {
    expect(previewKey('Meshes/Props/A.nif')).toBe('v1|meshes\\props\\a.nif');
    expect(previewKey('  meshes\\PROPS\\a.nif ')).toBe(previewKey('Meshes/Props/A.NIF'));
  });
});

describe('preview memory cache', () => {
  it('evicts the least recently used entry beyond capacity', () => {
    const cache = new PreviewCache(2, null);
    cache.remember('a.nif', { url: 'blob:a', size: null });
    cache.remember('b.nif', { url: 'blob:b', size: null });
    expect(cache.peek('a.nif')?.url).toBe('blob:a');
    cache.remember('c.nif', { url: 'blob:c', size: null });
    expect(cache.size).toBe(2);
    expect(cache.peek('b.nif')).toBeUndefined();
    expect(cache.peek('a.nif')?.url).toBe('blob:a');
    expect(cache.peek('c.nif')?.url).toBe('blob:c');
  });

  it('keeps capacity at least one', () => {
    const cache = new PreviewCache(0, null);
    cache.remember('a.nif', { url: 'blob:a', size: null });
    cache.remember('b.nif', { url: 'blob:b', size: null });
    expect(cache.size).toBe(1);
  });

  it('answers a memory hit without touching the store', async () => {
    const store = fakeStore();
    const cache = new PreviewCache(4, store);
    cache.remember('a.nif', { url: 'blob:a', size: null });
    await expect(cache.load('A.nif')).resolves.toEqual({ url: 'blob:a', size: null });
    expect(store.get).not.toHaveBeenCalled();
  });

  it('loads from the store on a miss and keeps the result in memory', async () => {
    const store = fakeStore({ [previewKey('b.nif')]: { url: 'data:b', size: null } });
    const cache = new PreviewCache(4, store);
    await expect(cache.load('b.nif')).resolves.toEqual({ url: 'data:b', size: null });
    expect(cache.peek('b.nif')?.url).toBe('data:b');
    await cache.load('b.nif');
    expect(store.get).toHaveBeenCalledTimes(1);
  });

  it('persists new entries to the store under the same key', () => {
    const store = fakeStore();
    const cache = new PreviewCache(4, store);
    cache.remember('c.nif', { url: 'data:c', size: [1, 2, 3] });
    expect(store.put).toHaveBeenCalledWith(previewKey('c.nif'), { url: 'data:c', size: [1, 2, 3] });
  });

  it('treats a failing store as a miss', async () => {
    const store = {
      get: vi.fn(async () => { throw new Error('idb closed'); }),
      put: vi.fn(async () => { throw new Error('quota'); }),
    } as unknown as PreviewStore;
    const cache = new PreviewCache(4, store);
    await expect(cache.load('d.nif')).resolves.toBeUndefined();
    expect(() => cache.remember('d.nif', { url: 'blob:d', size: null })).not.toThrow();
    await Promise.resolve();
    expect(cache.peek('d.nif')?.url).toBe('blob:d');
  });
});
