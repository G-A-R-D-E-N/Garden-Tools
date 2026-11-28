import { useEffect, useMemo, useState, type Dispatch, type SetStateAction } from 'react';
import type { CellHost } from '../backend';
import type { BaseObjectItem, BrowserQueryState } from './baseObjects';
import { buildCategoryTree, matchesCategory, type CategoryNode, type CategorySelection } from './categoryTree';
import { loadLibrary, saveLibrary, type LibraryState } from './libraryPrefs';
import { useListingScan, type ListingScan } from './useListingScan';

export interface BrowserDataOptions {
  host?: Pick<CellHost, 'ListBaseObjects'>;
  filters: BrowserQueryState;
  category: CategorySelection;
  enabled: boolean;
}

export interface BrowserData {
  scan: ListingScan;
  catalogTotal: number;
  typeCounts: Record<string, number>;
  items: BaseObjectItem[];
  tree: CategoryNode[];
  library: LibraryState;
  setLibrary: Dispatch<SetStateAction<LibraryState>>;
  catalogError: string | null;
}

export function useBrowserData(opts: BrowserDataOptions): BrowserData {
  const { host, filters, category, enabled } = opts;
  const [library, setLibrary] = useState<LibraryState>(() => loadLibrary());
  const countQuery = useMemo(() => ({ ...filters, type: '' }), [filters]);
  const scan = useListingScan({ host, query: filters, enabled });
  const catalog = useListingScan({ host, query: countQuery, enabled: enabled && filters.type !== '' });

  useEffect(() => { saveLibrary(library); }, [library]);

  const counts = filters.type === '' ? scan : catalog;
  const scanned = scan.items;

  const items = useMemo(() => {
    if (category.kind === 'favourites') return library.favourites;
    if (category.kind === 'recent') return library.recent;
    return scanned.filter(item => matchesCategory(item, category));
  }, [category, library.favourites, library.recent, scanned]);

  const tree = useMemo(() => buildCategoryTree({
    total: counts.total,
    typeCounts: counts.typeCounts,
    scanItems: scanned,
    activeType: filters.type,
    favourites: library.favourites.length,
    recent: library.recent.length,
  }), [counts.total, counts.typeCounts, scanned, filters.type, library.favourites.length, library.recent.length]);

  return {
    scan,
    catalogTotal: counts.total,
    typeCounts: counts.typeCounts,
    items,
    tree,
    library,
    setLibrary,
    catalogError: catalog.error,
  };
}
