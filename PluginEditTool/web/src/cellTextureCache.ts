
export interface Disposable { dispose(): void }

export interface TextureCache<T extends Disposable> {
  acquire(key: string): Promise<T | null>;
  release(key: string): void;
  dispose(): void;
  readonly isDisposed: boolean;
  readonly size: number;
  readonly bytes: number;
}

export interface TextureCacheOptions<T extends Disposable> {
  maxCount: number;
  maxBytes: number;
  concurrency: number;
  load: (key: string) => Promise<T | null>;
  sizeOf: (value: T) => number;
}

export const CELL_TEXTURE_CACHE_MAX = 128;
export const CELL_TEXTURE_CACHE_MAX_BYTES = 512 * 1024 * 1024;
export const CELL_TEXTURE_CONCURRENCY = 4;
export const CELL_TEXTURE_MAX_DIMENSION = 2048;

interface Entry<T> { value: T | null; bytes: number; refs: number }
interface Flight<T> { promise: Promise<T | null>; refs: number }

export function createTextureCache<T extends Disposable>(options: TextureCacheOptions<T>): TextureCache<T> {
  const maxCount = Math.max(1, Math.floor(options.maxCount));
  const maxBytes = Math.max(0, options.maxBytes);
  const concurrency = Math.max(1, Math.floor(options.concurrency));
  const entries = new Map<string, Entry<T>>();
  const flights = new Map<string, Flight<T>>();
  const waiting: (() => void)[] = [];
  let active = 0;
  let totalBytes = 0;
  let disposed = false;

  const takeSlot = () => new Promise<void>(resolve => {
    if (active < concurrency) {
      active++;
      resolve();
    } else {
      waiting.push(() => { active++; resolve(); });
    }
  });
  const freeSlot = () => {
    active--;
    waiting.shift()?.();
  };

  const evict = () => {
    for (const [key, entry] of entries) {
      if (entries.size <= maxCount && totalBytes <= maxBytes) break;
      if (entry.refs > 0) continue;
      entries.delete(key);
      totalBytes -= entry.bytes;
      entry.value?.dispose();
    }
  };

  const run = async (key: string): Promise<T | null> => {
    await takeSlot();
    if (disposed || !flights.get(key)?.refs) {
      freeSlot();
      flights.delete(key);
      return null;
    }
    let value: T | null = null;
    try {
      value = await options.load(key);
    } catch {
      value = null;
    } finally {
      freeSlot();
    }
    const flight = flights.get(key);
    flights.delete(key);
    if (disposed || !flight?.refs) {
      value?.dispose();
      return null;
    }
    const bytes = value ? Math.max(0, options.sizeOf(value)) : 0;
    entries.set(key, { value, bytes, refs: flight.refs });
    totalBytes += bytes;
    evict();
    return value;
  };

  return {
    get isDisposed() { return disposed; },
    get size() { return entries.size; },
    get bytes() { return totalBytes; },
    acquire(key: string): Promise<T | null> {
      if (disposed) return Promise.resolve(null);
      const hit = entries.get(key);
      if (hit) {
        hit.refs++;
        entries.delete(key);
        entries.set(key, hit);
        return Promise.resolve(hit.value);
      }
      let flight = flights.get(key);
      if (!flight) {
        flight = { promise: Promise.resolve(null), refs: 0 };
        flights.set(key, flight);
        flight.promise = run(key);
      }
      flight.refs++;
      return flight.promise;
    },
    release(key: string): void {
      const hit = entries.get(key);
      if (hit) {
        hit.refs = Math.max(0, hit.refs - 1);
        evict();
        return;
      }
      const flight = flights.get(key);
      if (flight) flight.refs = Math.max(0, flight.refs - 1);
    },
    dispose(): void {
      disposed = true;
      for (const entry of entries.values()) entry.value?.dispose();
      entries.clear();
      totalBytes = 0;
    },
  };
}


export function reconstructNormalZ(rgba: Uint8ClampedArray): void {
  for (let i = 0; i + 3 < rgba.length; i += 4) {
    const x = rgba[i] / 255 * 2 - 1;
    const yDirectX = rgba[i + 1] / 255 * 2 - 1;
    const yOpenGL = -yDirectX;
    const z = Math.sqrt(Math.max(0, 1 - x * x - yOpenGL * yOpenGL));
    rgba[i + 1] = 255 - rgba[i + 1];
    rgba[i + 2] = Math.round((z * 0.5 + 0.5) * 255);
    rgba[i + 3] = 255;
  }
}

export function glossToRoughnessInPlace(rgba: Uint8ClampedArray): void {
  for (let i = 0; i + 3 < rgba.length; i += 4) {
    rgba[i + 1] = 255 - rgba[i + 1];
  }
}

export function estimateTextureBytes(width: number, height: number): number {
  return width * height * 4 * (4 / 3);
}
