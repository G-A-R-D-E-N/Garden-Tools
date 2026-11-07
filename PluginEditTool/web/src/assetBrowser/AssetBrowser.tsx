import { useCallback, useEffect, useMemo, useRef, useState, type KeyboardEvent as ReactKeyboardEvent, type MouseEvent as ReactMouseEvent, type DragEvent as ReactDragEvent } from 'react';
import { Search, X } from 'lucide-react';
import { getCell, type CellHost } from '../backend';
import { errorText } from '../errors';
import AssetBrowserMenu, { menuEntries, type MenuAction } from './AssetBrowserMenu';
import AssetBrowserTable from './AssetBrowserTable';
import {
  BASE_OBJECT_MATCHES, BASE_OBJECT_PAGE_SIZE, BASE_OBJECT_SCOPES, DEFAULT_BROWSER_QUERY,
  buildBaseObjectQuery, parseBaseObjectListing,
  type BaseObjectItem, type BaseObjectMatch, type BaseObjectScope, type BaseObjectSort,
  type BrowserQueryState,
} from './baseObjects';
import {
  loadColumnPrefs, loadSearchHistory, loadSnapSetting, nextSortState, pushSearchHistory,
  saveColumnPrefs, saveSearchHistory, saveSnapSetting, type BrowserColumn,
} from './browserPrefs';
import { BASE_OBJECT_MIME, encodeDragPayload, makeDragPayload, setActiveDragPayload } from './dragPayload';
import { SNAP_GRID_SIZES } from './placement';
import { PLACEABLE_TYPES } from './placeableTypes';
import { clickSelection, EMPTY_SELECTION, pruneSelection, rangeSelection, selectAll, selectOnly, type SelectionState } from './selection';
import './AssetBrowser.css';

export type PlaceMode = 'viewCentre' | 'origin';

export interface AssetBrowserProps {
  host?: Pick<CellHost, 'ListBaseObjects'>;
  activePlugin?: string;
  onPlace: (items: BaseObjectItem[], mode: PlaceMode) => void;
  onPreview: (item: BaseObjectItem) => void;
}

interface Listing {
  items: BaseObjectItem[];
  total: number;
  typeCounts: Record<string, number>;
}

const SEARCH_DEBOUNCE_MS = 250;

const SCOPE_LABELS: Record<BaseObjectScope, string> = {
  editorId: 'Editor ID',
  name: 'Name',
  hexId: 'Form ID',
  modelPath: 'Model path',
  plugin: 'Plugin',
};

const MATCH_LABELS: Record<BaseObjectMatch, string> = {
  contains: 'Contains',
  startsWith: 'Starts with',
  exact: 'Exact',
  regex: 'Regex',
};

export default function AssetBrowser(props: AssetBrowserProps) {
  const { host, activePlugin, onPlace, onPreview } = props;
  const [text, setText] = useState('');
  const [filters, setFilters] = useState<BrowserQueryState>(DEFAULT_BROWSER_QUERY);
  const [listing, setListing] = useState<Listing>({ items: [], total: 0, typeCounts: {} });
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [selection, setSelection] = useState<SelectionState>(EMPTY_SELECTION);
  const [menu, setMenu] = useState<{ x: number; y: number } | null>(null);
  const [columns, setColumns] = useState(() => loadColumnPrefs());
  const [snap, setSnap] = useState<number | null>(() => loadSnapSetting());
  const [history, setHistory] = useState<string[]>(() => loadSearchHistory());
  const [historyOpen, setHistoryOpen] = useState(false);
  const seq = useRef(0);

  const load = useCallback(async (query: BrowserQueryState, offset: number, append: boolean) => {
    const mine = ++seq.current;
    const cell: Pick<CellHost, 'ListBaseObjects'> | undefined = host ?? getCell();
    if (!cell) {
      setError('Cell bridge not available.');
      return;
    }
    setLoading(true);
    try {
      const reply = parseBaseObjectListing(await cell.ListBaseObjects(buildBaseObjectQuery(query, offset, BASE_OBJECT_PAGE_SIZE)));
      if (mine !== seq.current) return;
      if (!reply.ok) {
        setError(reply.error);
        return;
      }
      setError(null);
      const page = reply.listing;
      setListing(prev => ({
        items: append ? [...prev.items, ...page.items] : page.items,
        total: page.total,
        typeCounts: page.typeCounts,
      }));
      if (!append) setSelection(prev => pruneSelection(prev, page.items.map(item => item.formKey)));
    } catch (e) {
      if (mine === seq.current) setError(errorText(e));
    } finally {
      if (mine === seq.current) setLoading(false);
    }
  }, [host]);

  useEffect(() => {
    const timer = setTimeout(() => {
      const term = text.trim();
      setFilters(prev => (prev.text === term ? prev : { ...prev, text: term }));
    }, SEARCH_DEBOUNCE_MS);
    return () => clearTimeout(timer);
  }, [text]);

  useEffect(() => {
    void load(filters, 0, false);
  }, [filters, load]);

  useEffect(() => saveColumnPrefs(columns), [columns]);

  useEffect(() => {
    if (!menu) return;
    const close = (e: PointerEvent) => {
      if (e.target instanceof Element && e.target.closest('.ab-menu')) return;
      setMenu(null);
    };
    window.addEventListener('pointerdown', close);
    return () => window.removeEventListener('pointerdown', close);
  }, [menu]);

  const order = useMemo(() => listing.items.map(item => item.formKey), [listing.items]);
  const selectedKeys = useMemo(() => new Set(selection.keys), [selection.keys]);
  const selectedItems = useMemo(() => {
    const byKey = new Map(listing.items.map(item => [item.formKey, item] as const));
    return selection.keys.flatMap(key => {
      const item = byKey.get(key);
      return item ? [item] : [];
    });
  }, [listing.items, selection.keys]);

  const copy = (lines: string[]) => {
    if (lines.length === 0) return;
    navigator.clipboard?.writeText(lines.join('\n')).catch(e => setError(errorText(e)));
  };

  const loadMore = () => {
    if (!loading && listing.items.length < listing.total) void load(filters, listing.items.length, true);
  };

  const runMenu = (action: MenuAction) => {
    setMenu(null);
    if (selectedItems.length === 0) return;
    switch (action) {
      case 'placeViewCentre': onPlace(selectedItems, 'viewCentre'); break;
      case 'placeOrigin': onPlace(selectedItems, 'origin'); break;
      case 'copyHexId': copy(selectedItems.map(item => item.hexId)); break;
      case 'copyEditorId': copy(selectedItems.map(item => item.editorId)); break;
      case 'copyModelPath': copy(selectedItems.map(item => item.modelPath).filter(path => path !== '')); break;
      case 'copyFormKey': copy(selectedItems.map(item => item.formKey)); break;
      case 'preview': onPreview(selectedItems[0]); break;
    }
  };

  const onRowClick = (item: BaseObjectItem, e: ReactMouseEvent<HTMLDivElement>) => {
    setSelection(prev => clickSelection(prev, order, item.formKey, { ctrl: e.ctrlKey || e.metaKey, shift: e.shiftKey }));
  };

  const onRowContext = (item: BaseObjectItem, e: ReactMouseEvent<HTMLDivElement>) => {
    e.preventDefault();
    if (!selection.keys.includes(item.formKey)) setSelection(selectOnly(item.formKey));
    setMenu({ x: e.clientX, y: e.clientY });
  };

  const onRowDragStart = (item: BaseObjectItem, e: ReactDragEvent<HTMLDivElement>) => {
    const dragged = selection.keys.includes(item.formKey) ? selectedItems : [item];
    if (!selection.keys.includes(item.formKey)) setSelection(selectOnly(item.formKey));
    const payload = makeDragPayload(dragged);
    setActiveDragPayload(payload);
    e.dataTransfer.setData(BASE_OBJECT_MIME, encodeDragPayload(payload));
    e.dataTransfer.effectAllowed = 'copy';
  };

  const onRowDragEnd = () => setActiveDragPayload(null);

  const moveActive = (step: number, extend: boolean) => {
    if (order.length === 0) return;
    const current = selection.anchor === null ? -1 : order.indexOf(selection.anchor);
    const base = current < 0 ? (step > 0 ? -1 : order.length) : current;
    const next = order[Math.min(order.length - 1, Math.max(0, base + step))];
    setSelection(extend ? rangeSelection(selection, order, next) : selectOnly(next));
  };

  const onTableKey = (e: ReactKeyboardEvent<HTMLDivElement>) => {
    const modified = e.ctrlKey || e.metaKey;
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      moveActive(e.key === 'ArrowDown' ? 1 : -1, e.shiftKey);
    } else if (e.key === 'Enter') {
      if (selectedItems.length === 0) return;
      e.preventDefault();
      onPlace(selectedItems, 'viewCentre');
    } else if (e.key === 'Escape') {
      setSelection(EMPTY_SELECTION);
    } else if (e.key === 'ContextMenu' || (e.key === 'F10' && e.shiftKey)) {
      if (selectedItems.length === 0) return;
      e.preventDefault();
      setMenu(rowAnchor(e.currentTarget, selection.anchor));
    } else if (modified && e.key.toLowerCase() === 'a') {
      e.preventDefault();
      setSelection(selectAll(order));
    } else if (modified && e.key.toLowerCase() === 'c') {
      e.preventDefault();
      copy(selectedItems.map(item => item.hexId));
    }
  };

  const clearSearch = () => {
    setText('');
    setFilters(prev => ({ ...prev, text: '' }));
  };

  const rememberSearch = () => {
    const term = text.trim();
    setFilters(prev => ({ ...prev, text: term }));
    const next = pushSearchHistory(history, term);
    setHistory(next);
    saveSearchHistory(next);
  };

  const pickHistory = (term: string) => {
    setText(term);
    setFilters(prev => ({ ...prev, text: term }));
    setHistoryOpen(false);
  };

  const onSearchKey = (e: ReactKeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      rememberSearch();
    } else if (e.key === 'Escape') {
      e.preventDefault();
      clearSearch();
    }
  };

  const toggleScope = (scope: BaseObjectScope) => setFilters(prev => {
    const has = prev.scopes.includes(scope);
    if (has && prev.scopes.length === 1) return prev;
    return { ...prev, scopes: has ? prev.scopes.filter(s => s !== scope) : [...prev.scopes, scope] };
  });

  const changeSnap = (value: string) => {
    const grid = value === 'off' ? null : Number(value);
    setSnap(grid);
    saveSnapSetting(grid);
  };

  const onSort = (column: BaseObjectSort) => setFilters(prev => ({ ...prev, ...nextSortState(prev, column) }));

  const onResize = (column: BrowserColumn, width: number) => {
    setColumns(prev => ({ ...prev, [column]: { ...prev[column], width } }));
  };

  return (
    <div className="ab-root">
      <div className="ab-toolbar">
        <div className="ab-search">
          <Search size={13} className="ab-search-icon" />
          <input
            type="search"
            aria-label="Search objects"
            placeholder="Search objects"
            value={text}
            onChange={e => setText(e.target.value)}
            onFocus={() => setHistoryOpen(true)}
            onBlur={() => setHistoryOpen(false)}
            onKeyDown={onSearchKey}
          />
          {text !== '' && (
            <button type="button" className="ab-icon-btn" aria-label="Clear search" onMouseDown={e => e.preventDefault()} onClick={clearSearch}>
              <X size={12} />
            </button>
          )}
          {historyOpen && history.length > 0 && (
            <ul className="ab-history" role="listbox" aria-label="Search history">
              {history.map(term => (
                <li key={term} role="option" aria-selected={false}>
                  <button type="button" onMouseDown={e => e.preventDefault()} onClick={() => pickHistory(term)}>{term}</button>
                </li>
              ))}
            </ul>
          )}
        </div>

        <div className="ab-row-controls">
          <select aria-label="Match mode" value={filters.match} onChange={e => setFilters(prev => ({ ...prev, match: e.target.value as BaseObjectMatch }))}>
            {BASE_OBJECT_MATCHES.map(match => <option key={match} value={match}>{MATCH_LABELS[match]}</option>)}
          </select>
          <div className="ab-scopes" role="group" aria-label="Search fields">
            {BASE_OBJECT_SCOPES.map(scope => (
              <button
                key={scope}
                type="button"
                aria-pressed={filters.scopes.includes(scope)}
                className={`ab-chip${filters.scopes.includes(scope) ? ' active' : ''}`}
                onClick={() => toggleScope(scope)}
              >
                {SCOPE_LABELS[scope]}
              </button>
            ))}
          </div>
        </div>

        <div className="ab-toggles">
          <label className="ab-check">
            <input
              type="checkbox"
              checked={filters.activePlugin !== ''}
              disabled={!activePlugin}
              onChange={e => setFilters(prev => ({ ...prev, activePlugin: e.target.checked ? activePlugin ?? '' : '' }))}
            />
            Active plugin only
          </label>
          <label className="ab-check">
            <input
              type="checkbox"
              checked={filters.hideDeleted}
              onChange={e => setFilters(prev => ({ ...prev, hideDeleted: e.target.checked }))}
            />
            Hide deleted
          </label>
          <label className="ab-check">
            <input
              type="checkbox"
              checked={filters.placeableWithModel}
              onChange={e => setFilters(prev => ({ ...prev, placeableWithModel: e.target.checked }))}
            />
            Only with model
          </label>
          <span className="ab-field">
            <span>Snap grid</span>
            <select aria-label="Snap grid" value={snap === null ? 'off' : String(snap)} onChange={e => changeSnap(e.target.value)}>
              <option value="off">Off</option>
              {SNAP_GRID_SIZES.map(size => <option key={size} value={size}>{size}</option>)}
            </select>
          </span>
        </div>
      </div>

      <div className="ab-types" role="group" aria-label="Object types">
        <button
          type="button"
          aria-pressed={filters.type === ''}
          className={`ab-chip${filters.type === '' ? ' active' : ''}`}
          onClick={() => setFilters(prev => ({ ...prev, type: '' }))}
        >
          All ({listing.total})
        </button>
        {PLACEABLE_TYPES.map(type => (
          <button
            key={type.signature}
            type="button"
            aria-pressed={filters.type === type.signature}
            className={`ab-chip${filters.type === type.signature ? ' active' : ''}`}
            onClick={() => setFilters(prev => ({ ...prev, type: type.signature }))}
          >
            {type.name} ({listing.typeCounts[type.signature] ?? 0})
          </button>
        ))}
      </div>

      <AssetBrowserTable
        items={listing.items}
        total={listing.total}
        loading={loading}
        columns={columns}
        sortBy={filters.sortBy}
        sortDir={filters.sortDir}
        selectedKeys={selectedKeys}
        activeKey={selection.anchor}
        onSort={onSort}
        onResize={onResize}
        onRowClick={onRowClick}
        onRowContext={onRowContext}
        onRowDragStart={onRowDragStart}
        onRowDragEnd={onRowDragEnd}
        onNearEnd={loadMore}
        onKeyDown={onTableKey}
      />

      <div className="ab-footer">
        <span className="ab-status">{loading ? 'Loading…' : `${listing.items.length} of ${listing.total} objects`}</span>
        {listing.items.length < listing.total && (
          <button type="button" className="ab-more" disabled={loading} onClick={loadMore}>Load more</button>
        )}
      </div>

      {error && <div role="alert" className="ab-error">{error}</div>}

      {menu && selectedItems.length > 0 && (
        <AssetBrowserMenu
          x={menu.x}
          y={menu.y}
          entries={menuEntries(selectedItems.length, selectedItems.some(item => item.modelPath !== ''))}
          onAction={runMenu}
          onClose={() => setMenu(null)}
        />
      )}
    </div>
  );
}

function rowAnchor(grid: HTMLElement, key: string | null): { x: number; y: number } {
  const row = Array.from(grid.querySelectorAll<HTMLElement>('[data-key]')).find(el => el.dataset.key === key);
  const rect = (row ?? grid).getBoundingClientRect();
  return { x: rect.left + 16, y: rect.bottom };
}

