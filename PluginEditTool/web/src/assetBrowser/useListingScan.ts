import { useEffect, useState } from 'react';
import { getCell, type CellHost } from '../backend';
import { errorText } from '../errors';
import {
  BASE_OBJECT_MAX_LIMIT, buildBaseObjectQuery, parseBaseObjectListing,
  type BaseObjectItem, type BrowserQueryState,
} from './baseObjects';

export const SCAN_PAGE_SIZE = BASE_OBJECT_MAX_LIMIT;
export const SCAN_CAP = 20000;

export interface ScanProgress {
  items: BaseObjectItem[];
  total: number;
  typeCounts: Record<string, number>;
  done: boolean;
  capped: boolean;
  error: string | null;
}

export type PageFetch = (offset: number, limit: number) => Promise<string | null>;

export interface ScanOptions {
  pageSize: number;
  cap: number;
  alive: () => boolean;
  onProgress: (progress: ScanProgress) => void;
}

const EMPTY_PROGRESS: ScanProgress = { items: [], total: 0, typeCounts: {}, done: true, capped: false, error: null };

export async function scanAll(fetchPage: PageFetch, opts: ScanOptions): Promise<void> {
  const items: BaseObjectItem[] = [];
  let total = 0;
  let typeCounts: Record<string, number> = {};
  let offset = 0;
  for (;;) {
    const raw = await fetchPage(offset, opts.pageSize);
    if (!opts.alive()) return;
    if (raw === null) {
      opts.onProgress({ ...EMPTY_PROGRESS, items: [...items], total, typeCounts });
      return;
    }
    const reply = parseBaseObjectListing(raw);
    if (!reply.ok) {
      opts.onProgress({ ...EMPTY_PROGRESS, items: [...items], total, typeCounts, error: reply.error });
      return;
    }
    const page = reply.listing;
    if (offset === 0) typeCounts = page.typeCounts;
    total = page.total;
    items.push(...page.items);
    const finished = page.items.length === 0 || items.length >= total;
    const hitCap = items.length >= opts.cap;
    if (items.length > opts.cap) items.length = opts.cap;
    opts.onProgress({
      items: [...items],
      total,
      typeCounts,
      done: finished || hitCap,
      capped: hitCap && !finished,
      error: null,
    });
    if (finished || hitCap) return;
    offset += page.items.length;
  }
}

export function scanKey(query: BrowserQueryState): string {
  return JSON.stringify(query);
}

export interface ListingScan {
  items: BaseObjectItem[];
  total: number;
  typeCounts: Record<string, number>;
  loading: boolean;
  capped: boolean;
  error: string | null;
  retry: () => void;
}

export function useListingScan(opts: {
  host?: Pick<CellHost, 'ListBaseObjects'>;
  query: BrowserQueryState;
  enabled: boolean;
  pageSize?: number;
  cap?: number;
}): ListingScan {
  const { host, query, enabled } = opts;
  const pageSize = opts.pageSize ?? SCAN_PAGE_SIZE;
  const cap = opts.cap ?? SCAN_CAP;
  const key = scanKey(query);
  const typelessKey = scanKey({ ...query, type: '' });
  const [scan, setScan] = useState<ScanProgress & { key: string }>({ ...EMPTY_PROGRESS, key: '', done: false });
  const [catalog, setCatalog] = useState<{ key: string; counts: Record<string, number> } | null>(null);
  const [attempt, setAttempt] = useState(0);

  useEffect(() => {
    if (!enabled) return;
    const cell: Pick<CellHost, 'ListBaseObjects'> | undefined = host ?? getCell();
    if (!cell) return;
    let alive = true;
    const fetchPage: PageFetch = (offset, limit) => cell.ListBaseObjects(buildBaseObjectQuery(query, offset, limit));
    const onProgress = (progress: ScanProgress) => {
      if (!alive) return;
      setScan({ ...progress, key });
      if (query.type === '') setCatalog({ key: typelessKey, counts: progress.typeCounts });
    };
    scanAll(fetchPage, { pageSize, cap, alive: () => alive, onProgress }).catch(e => {
      if (alive) setScan({ ...EMPTY_PROGRESS, key, error: errorText(e) });
    });
    return () => { alive = false; };
  }, [query, enabled, host, pageSize, cap, key, typelessKey, attempt]);

  const current = scan.key === key;
  const counts = catalog && catalog.key === typelessKey ? catalog.counts : scan.typeCounts;
  const retry = () => {
    setScan({ ...EMPTY_PROGRESS, key, done: false });
    setAttempt(n => n + 1);
  };
  return {
    items: current ? scan.items : [],
    total: current ? scan.total : 0,
    typeCounts: counts,
    loading: !current || !scan.done,
    capped: current && scan.capped,
    error: current ? scan.error : null,
    retry,
  };
}
