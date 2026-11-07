import { describe, expect, it } from 'vitest';
import { createTextureCache, glossToRoughnessInPlace, reconstructNormalZ } from './cellTextureCache';

type Fake = { id: string; bytes: number; disposed: boolean; dispose: () => void };

function fake(id: string, bytes = 100): Fake {
  const item: Fake = { id, bytes, disposed: false, dispose: () => { item.disposed = true; } };
  return item;
}

function options(overrides: Partial<Parameters<typeof createTextureCache<Fake>>[0]> = {}) {
  return {
    maxCount: 8,
    maxBytes: 10_000,
    concurrency: 4,
    sizeOf: (f: Fake) => f.bytes,
    load: async (key: string) => fake(key),
    ...overrides,
  };
}

describe('createTextureCache', () => {
  it('loads each key once when requested concurrently', async () => {
    let loads = 0;
    const cache = createTextureCache<Fake>(options({ load: async key => { loads++; return fake(key); } }));
    const [a, b] = await Promise.all([cache.acquire('plain|a'), cache.acquire('plain|a')]);
    expect(loads).toBe(1);
    expect(a).toBe(b);
    cache.dispose();
  });

  it('keeps a texture that a live material still holds, even past the count cap', async () => {
    const made = new Map<string, Fake>();
    const cache = createTextureCache<Fake>(options({
      maxCount: 1,
      load: async key => { const f = fake(key); made.set(key, f); return f; },
    }));
    await cache.acquire('a');
    await cache.acquire('b');
    expect(made.get('a')!.disposed).toBe(false);
    expect(made.get('b')!.disposed).toBe(false);
    cache.release('a');
    expect(made.get('a')!.disposed).toBe(true);
    expect(made.get('b')!.disposed).toBe(false);
    cache.dispose();
  });

  it('evicts unreferenced entries to stay within the byte budget', async () => {
    const made = new Map<string, Fake>();
    const cache = createTextureCache<Fake>(options({
      maxBytes: 250,
      load: async key => { const f = fake(key); made.set(key, f); return f; },
    }));
    for (const key of ['a', 'b', 'c']) {
      await cache.acquire(key);
      cache.release(key);
    }
    expect(made.get('a')!.disposed).toBe(true);
    expect(made.get('b')!.disposed).toBe(false);
    expect(made.get('c')!.disposed).toBe(false);
    expect(cache.bytes).toBe(200);
    cache.dispose();
  });

  it('disposes a load that finishes after the cache is disposed', async () => {
    let finish!: (f: Fake) => void;
    const cache = createTextureCache<Fake>(options({
      load: () => new Promise<Fake>(resolve => { finish = resolve; }),
    }));
    const pending = cache.acquire('late');
    await new Promise(resolve => setTimeout(resolve, 0));
    cache.dispose();
    const late = fake('late');
    finish(late);
    expect(await pending).toBeNull();
    expect(late.disposed).toBe(true);
    expect(cache.isDisposed).toBe(true);
  });

  it('disposes a load whose every holder released before it finished', async () => {
    let finish!: (f: Fake) => void;
    const cache = createTextureCache<Fake>(options({
      load: () => new Promise<Fake>(resolve => { finish = resolve; }),
    }));
    void cache.acquire('gone');
    await new Promise(resolve => setTimeout(resolve, 0));
    cache.release('gone');
    const late = fake('gone');
    finish(late);
    await new Promise(resolve => setTimeout(resolve, 0));
    expect(late.disposed).toBe(true);
    expect(cache.size).toBe(0);
    cache.dispose();
  });

  it('never runs more loads than the concurrency cap at once', async () => {
    let active = 0;
    let peak = 0;
    const cache = createTextureCache<Fake>(options({
      maxCount: 64,
      concurrency: 3,
      load: async key => {
        active++;
        peak = Math.max(peak, active);
        await new Promise(resolve => setTimeout(resolve, 5));
        active--;
        return fake(key);
      },
    }));
    await Promise.all(Array.from({ length: 12 }, (_, i) => cache.acquire(`k${i}`)));
    expect(peak).toBe(3);
    cache.dispose();
  });

  it('returns null for a failed load and does not throw', async () => {
    const cache = createTextureCache<Fake>(options({
      load: async () => { throw new Error('decode'); },
    }));
    await expect(cache.acquire('bad')).resolves.toBeNull();
    cache.dispose();
  });

  it('skips a queued load whose holders all released before its slot freed, and frees the slot', async () => {
    const started: string[] = [];
    const gates = new Map<string, (f: Fake) => void>();
    const cache = createTextureCache<Fake>(options({
      concurrency: 1,
      load: key => { started.push(key); return new Promise<Fake>(resolve => { gates.set(key, resolve); }); },
    }));
    void cache.acquire('a');
    const queued = cache.acquire('b');
    await new Promise(resolve => setTimeout(resolve, 0));
    cache.release('b');
    cache.release('a');
    gates.get('a')!(fake('a'));
    expect(await queued).toBeNull();
    expect(started).toEqual(['a']);
    const next = cache.acquire('c');
    await new Promise(resolve => setTimeout(resolve, 0));
    expect(started).toEqual(['a', 'c']);
    gates.get('c')!(fake('c'));
    expect(await next).not.toBeNull();
    cache.dispose();
  });
});

describe('reconstructNormalZ', () => {
  it('flips the DirectX green channel to OpenGL and rebuilds Z', () => {
    const rgba = new Uint8ClampedArray([128, 200, 0, 0]);
    reconstructNormalZ(rgba);
    expect(rgba[1]).toBe(55);
    const yOpenGL = -(200 / 255 * 2 - 1);
    const z = Math.sqrt(1 - yOpenGL * yOpenGL);
    expect(rgba[2]).toBe(Math.round((z * 0.5 + 0.5) * 255));
    expect(rgba[3]).toBe(255);
  });

  it('turns a low DirectX green value into a high OpenGL green value', () => {
    const rgba = new Uint8ClampedArray([128, 50, 0, 0]);
    reconstructNormalZ(rgba);
    expect(rgba[1]).toBe(205);
  });

  it('keeps a flat normal pointing straight out', () => {
    const rgba = new Uint8ClampedArray([128, 128, 0, 0]);
    reconstructNormalZ(rgba);
    expect(rgba[2]).toBe(255);
  });
});

describe('glossToRoughnessInPlace', () => {
  it('inverts the green channel and leaves red, blue and alpha alone', () => {
    const rgba = new Uint8ClampedArray([10, 200, 30, 255]);
    glossToRoughnessInPlace(rgba);
    expect([...rgba]).toEqual([10, 55, 30, 255]);
  });
});
