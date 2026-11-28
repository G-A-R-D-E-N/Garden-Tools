import { useEffect, useState, type RefObject } from 'react';
import * as THREE from 'three';
import { getCell } from '../backend';
import { loadPlainTexture } from '../cellTextureLoad';
import { fetchGhostGeometry } from '../util/ghostMesh';
import { PreviewCache, createIndexedDbStore, previewKey, PREVIEW_MEMORY_CAPACITY } from './previewCache';
import { createPreviewRenderer, THUMB_VIEW, type PreviewRenderer, type PreviewSources } from './previewRenderer';

export const PREVIEW_CONCURRENCY = 2;

export interface PreviewQueueOptions {
  concurrency: number;
  cache: PreviewCache;
  render: (modelPath: string) => Promise<string | null>;
}

interface PreviewJob {
  key: string;
  modelPath: string;
  refs: number;
  resolve: (url: string | null) => void;
  promise: Promise<string | null>;
}

export class PreviewQueue {
  private readonly concurrency: number;
  private readonly cache: PreviewCache;
  private readonly renderOne: (modelPath: string) => Promise<string | null>;
  private readonly jobs = new Map<string, PreviewJob>();
  private readonly waiting: PreviewJob[] = [];
  private readonly failed = new Set<string>();
  private running = 0;

  constructor(opts: PreviewQueueOptions) {
    this.concurrency = Math.max(1, Math.floor(opts.concurrency));
    this.cache = opts.cache;
    this.renderOne = opts.render;
  }

  get active(): number {
    return this.running;
  }

  get queued(): number {
    return this.waiting.length;
  }

  cached(modelPath: string): string | null {
    return this.cache.peek(modelPath)?.url ?? null;
  }

  request(modelPath: string): Promise<string | null> {
    const hit = this.cache.peek(modelPath);
    if (hit) return Promise.resolve(hit.url);
    const key = previewKey(modelPath);
    if (this.failed.has(key)) return Promise.resolve(null);
    const existing = this.jobs.get(key);
    if (existing) {
      existing.refs++;
      return existing.promise;
    }
    let resolve: (url: string | null) => void = () => undefined;
    const promise = new Promise<string | null>(done => { resolve = done; });
    const job: PreviewJob = { key, modelPath, refs: 1, resolve, promise };
    this.jobs.set(key, job);
    this.waiting.push(job);
    this.pump();
    return promise;
  }

  cancel(modelPath: string): void {
    const job = this.jobs.get(previewKey(modelPath));
    if (!job) return;
    job.refs--;
    if (job.refs > 0) return;
    const at = this.waiting.indexOf(job);
    if (at >= 0) this.waiting.splice(at, 1);
    this.finish(job, null);
  }

  private pump(): void {
    while (this.running < this.concurrency && this.waiting.length > 0) {
      const job = this.waiting.shift();
      if (!job) break;
      this.running++;
      void this.run(job).finally(() => {
        this.running--;
        this.pump();
      });
    }
  }

  private async run(job: PreviewJob): Promise<void> {
    let url: string | null;
    try {
      url = (await this.cache.load(job.modelPath))?.url ?? null;
      if (!url) {
        url = await this.renderOne(job.modelPath);
        if (url) this.cache.remember(job.modelPath, { url, size: null });
        else this.failed.add(job.key);
      }
    } catch {
      url = null;
    }
    this.finish(job, url);
  }

  private finish(job: PreviewJob, url: string | null): void {
    if (this.jobs.get(job.key) === job) this.jobs.delete(job.key);
    job.resolve(url);
  }
}

let shared: { queue: PreviewQueue; renderer: PreviewRenderer } | null = null;

function defaultSources(): PreviewSources {
  return {
    fetchGeometry: fetchGhostGeometry,
    loadTexture: async (modelPath, relPath) => getCell()?.GetTexture(modelPath, relPath) ?? '',
    decodeTexture: url => loadPlainTexture(url, THREE.SRGBColorSpace),
  };
}

function sharedParts(): { queue: PreviewQueue; renderer: PreviewRenderer } {
  if (!shared) {
    const renderer = createPreviewRenderer(defaultSources());
    const cache = new PreviewCache(PREVIEW_MEMORY_CAPACITY, createIndexedDbStore());
    const queue = new PreviewQueue({
      concurrency: PREVIEW_CONCURRENCY,
      cache,
      render: modelPath => renderer.render(modelPath, THUMB_VIEW),
    });
    shared = { queue, renderer };
  }
  return shared;
}

export function getPreviewQueue(): PreviewQueue {
  return sharedParts().queue;
}

export function getPreviewRenderer(): PreviewRenderer {
  return sharedParts().renderer;
}

export function usePreviewUrl(modelPath: string, enabled: boolean): string | null {
  const queue = getPreviewQueue();
  const [settled, setSettled] = useState<{ path: string; url: string | null }>({ path: '', url: null });

  useEffect(() => {
    if (!enabled || modelPath === '') return;
    let alive = true;
    void queue.request(modelPath).then(url => {
      if (alive) setSettled({ path: modelPath, url });
    });
    return () => {
      alive = false;
      queue.cancel(modelPath);
    };
  }, [queue, modelPath, enabled]);

  if (modelPath === '') return null;
  return queue.cached(modelPath) ?? (settled.path === modelPath ? settled.url : null);
}

export function useInView<T extends Element>(ref: RefObject<T | null>): boolean {
  const [visible, setVisible] = useState(() => typeof IntersectionObserver === 'undefined');
  useEffect(() => {
    const el = ref.current;
    if (!el || typeof IntersectionObserver === 'undefined') return;
    const observer = new IntersectionObserver(entries => {
      setVisible(entries.some(entry => entry.isIntersecting));
    });
    observer.observe(el);
    return () => observer.disconnect();
  }, [ref]);
  return visible;
}
