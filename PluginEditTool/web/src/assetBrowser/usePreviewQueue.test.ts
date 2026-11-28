import { describe, expect, it, vi } from 'vitest';
import { PreviewCache, type PreviewStore } from './previewCache';
import { PreviewQueue } from './usePreviewQueue';

interface Deferred {
  path: string;
  resolve: (url: string | null) => void;
}

function harness(opts: { store?: PreviewStore | null } = {}) {
  const started: Deferred[] = [];
  let inFlight = 0;
  let maxInFlight = 0;
  const render = vi.fn((modelPath: string) => new Promise<string | null>(done => {
    inFlight++;
    maxInFlight = Math.max(maxInFlight, inFlight);
    started.push({
      path: modelPath,
      resolve: url => {
        inFlight--;
        done(url);
      },
    });
  }));
  const cache = new PreviewCache(10, opts.store ?? null);
  const queue = new PreviewQueue({ concurrency: 2, cache, render });
  return { queue, cache, render, started, maxInFlight: () => maxInFlight };
}

async function flush(): Promise<void> {
  await new Promise(done => setTimeout(done, 0));
}

describe('preview queue', () => {
  it('renders at most two models at a time', async () => {
    const h = harness();
    const requests = ['a', 'b', 'c', 'd'].map(p => h.queue.request(`${p}.nif`));
    await flush();
    expect(h.render).toHaveBeenCalledTimes(2);
    expect(h.queue.active).toBe(2);
    expect(h.queue.queued).toBe(2);

    h.started[0].resolve(`blob:${h.started[0].path}`);
    await flush();
    expect(h.render).toHaveBeenCalledTimes(3);

    for (let i = 0; i < 3; i++) {
      await flush();
      h.started[i + 1]?.resolve(`blob:${h.started[i + 1].path}`);
    }
    await flush();
    expect(h.started).toHaveLength(4);
    expect(await Promise.all(requests)).toEqual(['blob:a.nif', 'blob:b.nif', 'blob:c.nif', 'blob:d.nif']);
    expect(h.maxInFlight()).toBe(2);
  });

  it('shares one job between requests for the same model', async () => {
    const h = harness();
    const first = h.queue.request('shared.nif');
    const second = h.queue.request('SHARED.NIF');
    await flush();
    expect(h.render).toHaveBeenCalledTimes(1);
    h.started[0].resolve('blob:shared');
    expect(await first).toBe('blob:shared');
    expect(await second).toBe('blob:shared');
  });

  it('drops a queued model that is cancelled before it starts', async () => {
    const h = harness();
    h.queue.request('a.nif');
    h.queue.request('b.nif');
    await flush();
    const third = h.queue.request('c.nif');
    h.queue.cancel('c.nif');
    expect(await third).toBeNull();
    expect(h.queue.queued).toBe(0);
    for (const s of h.started) s.resolve(null);
    await flush();
    expect(h.render.mock.calls.map(c => c[0])).toEqual(['a.nif', 'b.nif']);
  });

  it('keeps a running model until every holder has cancelled', async () => {
    const h = harness();
    const first = h.queue.request('a.nif');
    h.queue.request('a.nif');
    await flush();
    h.queue.cancel('a.nif');
    expect(h.queue.active).toBe(1);
    h.queue.cancel('a.nif');
    expect(await first).toBeNull();
    h.started[0].resolve('blob:late');
    await flush();
    expect(h.cache.peek('a.nif')?.url).toBe('blob:late');
  });

  it('answers a cache hit without rendering', async () => {
    const h = harness();
    const first = h.queue.request('cached.nif');
    await flush();
    h.started[0].resolve('blob:cached');
    expect(await first).toBe('blob:cached');
    expect(h.queue.cached('cached.nif')).toBe('blob:cached');
    expect(await h.queue.request('cached.nif')).toBe('blob:cached');
    expect(h.render).toHaveBeenCalledTimes(1);
  });

  it('uses the persistent store before rendering', async () => {
    const store: PreviewStore = {
      get: vi.fn(async () => ({ url: 'data:stored', size: null })),
      put: vi.fn(async () => undefined),
    };
    const h = harness({ store });
    expect(await h.queue.request('stored.nif')).toBe('data:stored');
    expect(h.render).not.toHaveBeenCalled();
  });

  it('does not retry a model that rendered nothing, but does retry after a thrown error', async () => {
    const h = harness();
    const missing = h.queue.request('missing.nif');
    await flush();
    h.started[0].resolve(null);
    expect(await missing).toBeNull();
    expect(await h.queue.request('missing.nif')).toBeNull();
    expect(h.render).toHaveBeenCalledTimes(1);

    const throwing = new PreviewQueue({
      concurrency: 1,
      cache: new PreviewCache(10, null),
      render: vi.fn(async () => { throw new Error('gl lost'); }),
    });
    expect(await throwing.request('boom.nif')).toBeNull();
  });
});
