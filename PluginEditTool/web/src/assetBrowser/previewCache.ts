export const PREVIEW_SCHEMA_VERSION = 1;
export const PREVIEW_MEMORY_CAPACITY = 600;

const DB_NAME = 'fo4ide-asset-previews';
const DB_STORE = 'previews';

export interface PreviewEntry {
  url: string;
  size: [number, number, number] | null;
}

export interface PreviewStore {
  get(key: string): Promise<PreviewEntry | undefined>;
  put(key: string, entry: PreviewEntry): Promise<void>;
}

export function previewKey(modelPath: string): string {
  return `v${PREVIEW_SCHEMA_VERSION}|${modelPath.trim().split('/').join('\\').toLowerCase()}`;
}

export class PreviewCache {
  private readonly entries = new Map<string, PreviewEntry>();
  private readonly capacity: number;
  private readonly store: PreviewStore | null;

  constructor(capacity: number, store: PreviewStore | null) {
    this.capacity = Math.max(1, Math.floor(capacity));
    this.store = store;
  }

  get size(): number {
    return this.entries.size;
  }

  peek(modelPath: string): PreviewEntry | undefined {
    const key = previewKey(modelPath);
    const hit = this.entries.get(key);
    if (hit) this.touch(key, hit);
    return hit;
  }

  async load(modelPath: string): Promise<PreviewEntry | undefined> {
    const hit = this.peek(modelPath);
    if (hit || !this.store) return hit;
    const key = previewKey(modelPath);
    try {
      const stored = await this.store.get(key);
      if (stored) this.keep(key, stored);
      return stored;
    } catch {
      return undefined;
    }
  }

  remember(modelPath: string, entry: PreviewEntry): void {
    const key = previewKey(modelPath);
    this.keep(key, entry);
    if (this.store) void this.store.put(key, entry).catch(() => undefined);
  }

  private touch(key: string, entry: PreviewEntry): void {
    this.entries.delete(key);
    this.entries.set(key, entry);
  }

  private keep(key: string, entry: PreviewEntry): void {
    this.touch(key, entry);
    while (this.entries.size > this.capacity) {
      const oldest = this.entries.keys().next().value;
      if (oldest === undefined) break;
      this.entries.delete(oldest);
    }
  }
}

export function createIndexedDbStore(): PreviewStore | null {
  if (typeof indexedDB === 'undefined') return null;
  let connection: Promise<IDBDatabase> | null = null;
  const open = (): Promise<IDBDatabase> => {
    connection ??= new Promise<IDBDatabase>((resolve, reject) => {
      const request = indexedDB.open(DB_NAME, 1);
      request.onupgradeneeded = () => request.result.createObjectStore(DB_STORE);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
    return connection;
  };
  const run = async <T>(mode: IDBTransactionMode, work: (store: IDBObjectStore) => IDBRequest<T>): Promise<T> => {
    const db = await open();
    return new Promise<T>((resolve, reject) => {
      const request = work(db.transaction(DB_STORE, mode).objectStore(DB_STORE));
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
  };
  return {
    async get(key) {
      const value: unknown = await run('readonly', store => store.get(key));
      return isEntry(value) ? value : undefined;
    },
    async put(key, entry) {
      await run('readwrite', store => store.put(entry, key));
    },
  };
}

function isEntry(value: unknown): value is PreviewEntry {
  return typeof value === 'object' && value !== null && typeof (value as PreviewEntry).url === 'string';
}
