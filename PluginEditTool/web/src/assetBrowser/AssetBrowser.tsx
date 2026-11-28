import { useEffect, useMemo, useRef, useState, type KeyboardEvent as ReactKeyboardEvent, type MouseEvent as ReactMouseEvent, type DragEvent as ReactDragEvent } from 'react';
import { getCell, type CellHost } from '../backend';
import { errorText } from '../errors';
import AssetBrowserMenu from './AssetBrowserMenu';
import AssetBrowserTable from './AssetBrowserTable';
import CategoryTree from './CategoryTreeView';
import DetailPane from './DetailPane';
import DrawerToolbar from './DrawerToolbar';
import TileGrid from './TileGrid';
import { menuEntries, type MenuAction } from './menuModel';
import { DEFAULT_BROWSER_QUERY, type BaseObjectItem, type BrowserQueryState, type BaseObjectSort } from './baseObjects';
import {
  loadColumnPrefs, loadSearchHistory, loadSnapSetting, nextSortState, pushSearchHistory,
  saveColumnPrefs, saveSearchHistory, saveSnapSetting, type BrowserColumn,
} from './browserPrefs';
import { type CategorySelection } from './categoryTree';
import { BASE_OBJECT_MIME, encodeDragPayload, makeDragPayload, setActiveDragPayload } from './dragPayload';
import { isFavourite, pushRecent, toggleFavourite } from './libraryPrefs';
import { clickSelection, EMPTY_SELECTION, pruneSelection, rangeSelection, selectAll, selectOnly, type SelectionState } from './selection';
import type { DrawerController } from './useDrawerState';
import { useBrowserData } from './useBrowserData';
import { SCAN_CAP } from './useListingScan';
import { INDEXING_MESSAGE } from './tableModel';
import './AssetBrowser.css';

export type PlaceMode = 'viewCentre' | 'origin';

export interface AssetBrowserProps {
  host?: Pick<CellHost, 'ListBaseObjects'>;
  activePlugin?: string;
  drawer: DrawerController;
  onPlace: (items: BaseObjectItem[], mode: PlaceMode) => void;
  onPreview: (item: BaseObjectItem) => void;
  onDragStart?: (count: number) => void;
}

const SEARCH_DEBOUNCE_MS = 250;

export default function AssetBrowser(props: AssetBrowserProps) {
  const { host, activePlugin, drawer, onPlace, onPreview, onDragStart } = props;
  const { prefs } = drawer;
  const [text, setText] = useState('');
  const [filters, setFilters] = useState<BrowserQueryState>(DEFAULT_BROWSER_QUERY);
  const [category, setCategory] = useState<CategorySelection>({ kind: 'all' });
  const [rawSelection, setSelection] = useState<SelectionState>(EMPTY_SELECTION);
  const [menu, setMenu] = useState<{ x: number; y: number } | null>(null);
  const [columns, setColumns] = useState(() => loadColumnPrefs());
  const [snap, setSnap] = useState<number | null>(() => loadSnapSetting());
  const [history, setHistory] = useState<string[]>(() => loadSearchHistory());
  const [actionError, setActionError] = useState<string | null>(null);
  const dragging = useRef<BaseObjectItem[]>([]);

  const data = useBrowserData({ host, filters, category, enabled: drawer.open });
  const { scan, items, tree, library, setLibrary, catalogError } = data;
  const bridgeMissing = !(host ?? getCell());
  const isLibrary = category.kind === 'favourites' || category.kind === 'recent';
  const loading = !bridgeMissing && !isLibrary && scan.loading;
  const shownError = bridgeMissing ? 'Cell bridge not available.' : actionError ?? scan.error ?? catalogError;
  const failed = !bridgeMissing && scan.error !== null;
  const retryable = failed && !actionError;
  const total = isLibrary ? items.length : scan.total;

  useEffect(() => {
    const timer = setTimeout(() => {
      const term = text.trim();
      setFilters(prev => (prev.text === term ? prev : { ...prev, text: term }));
    }, SEARCH_DEBOUNCE_MS);
    return () => clearTimeout(timer);
  }, [text]);

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

  const order = useMemo(() => items.map(item => item.formKey), [items]);
  const selection = useMemo(() => pruneSelection(rawSelection, order), [rawSelection, order]);
  const selectedKeys = useMemo(() => new Set(selection.keys), [selection.keys]);
  const selectedItems = useMemo(() => {
    const byKey = new Map(items.map(item => [item.formKey, item] as const));
    return selection.keys.flatMap(key => {
      const item = byKey.get(key);
      return item ? [item] : [];
    });
  }, [items, selection.keys]);
  const activeItem = useMemo(() => items.find(item => item.formKey === selection.anchor) ?? null, [items, selection.anchor]);

  const copy = (lines: string[]) => {
    if (lines.length === 0) return;
    navigator.clipboard?.writeText(lines.join('\n')).catch(e => setActionError(errorText(e)));
  };

  const remember = (picked: BaseObjectItem[]) => {
    if (picked.length === 0) return;
    setLibrary(prev => ({ ...prev, recent: pushRecent(prev.recent, picked) }));
  };

  const place = (picked: BaseObjectItem[], mode: PlaceMode) => {
    remember(picked);
    onPlace(picked, mode);
  };

  const runMenu = (action: MenuAction) => {
    setMenu(null);
    if (selectedItems.length === 0) return;
    switch (action) {
      case 'placeViewCentre': place(selectedItems, 'viewCentre'); break;
      case 'placeOrigin': place(selectedItems, 'origin'); break;
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
    const picked = selection.keys.includes(item.formKey) ? selectedItems : [item];
    if (!selection.keys.includes(item.formKey)) setSelection(selectOnly(item.formKey));
    const payload = makeDragPayload(picked);
    setActiveDragPayload(payload);
    dragging.current = picked;
    e.dataTransfer.setData(BASE_OBJECT_MIME, encodeDragPayload(payload));
    e.dataTransfer.effectAllowed = 'copy';
    onDragStart?.(picked.length);
  };

  const onRowDragEnd = (e: ReactDragEvent<HTMLDivElement>) => {
    setActiveDragPayload(null);
    const effect = e.dataTransfer?.dropEffect ?? 'none';
    if (effect !== 'none') remember(dragging.current);
    drawer.dragEnd(effect);
  };

  const moveActive = (step: number, extend: boolean) => {
    if (order.length === 0) return;
    const current = selection.anchor === null ? -1 : order.indexOf(selection.anchor);
    const base = current < 0 ? (step > 0 ? -1 : order.length) : current;
    const next = order[Math.min(order.length - 1, Math.max(0, base + step))];
    setSelection(extend ? rangeSelection(selection, order, next) : selectOnly(next));
  };

  const onGridKey = (e: ReactKeyboardEvent<HTMLDivElement>) => {
    const modified = e.ctrlKey || e.metaKey;
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      moveActive(e.key === 'ArrowDown' ? 1 : -1, e.shiftKey);
    } else if (e.key === 'Enter') {
      if (selectedItems.length === 0) return;
      e.preventDefault();
      place(selectedItems, 'viewCentre');
    } else if (e.key === 'Escape') {
      if (selection.keys.length === 0) return;
      e.preventDefault();
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
  };

  const changeSnap = (value: string) => {
    const grid = value === 'off' ? null : Number(value);
    setSnap(grid);
    saveSnapSetting(grid);
  };

  const onSort = (column: BaseObjectSort) => setFilters(prev => ({ ...prev, ...nextSortState(prev, column) }));

  const onResize = (column: BrowserColumn, width: number) => {
    setColumns(prev => ({ ...prev, [column]: { ...prev[column], width } }));
  };

  const selectCategory = (next: CategorySelection) => {
    setCategory(next);
    setSelection(EMPTY_SELECTION);
    if (next.kind === 'all') {
      setFilters(prev => (prev.type === '' ? prev : { ...prev, type: '' }));
    } else if (next.kind === 'type') {
      const signature = next.signature;
      setFilters(prev => (prev.type === signature ? prev : { ...prev, type: signature }));
    } else if (next.kind === 'folder' || next.kind === 'plugin') {
      const signature = next.signature;
      setFilters(prev => (prev.type === signature ? prev : { ...prev, type: signature }));
    }
  };

  const toggleFavouriteItem = (item: BaseObjectItem) => {
    setLibrary(prev => ({ ...prev, favourites: toggleFavourite(prev.favourites, item) }));
  };

  const status = failed ? 'Scan failed'
    : loading ? (items.length === 0 ? INDEXING_MESSAGE : 'Loading\u2026')
    : `${items.length} of ${total} objects${scan.capped ? ` (first ${SCAN_CAP} loaded)` : ''}`;

  return (
    <div className="ab-root">
      <DrawerToolbar
        filters={filters}
        setFilters={setFilters}
        text={text}
        onText={setText}
        onClearSearch={clearSearch}
        onRememberSearch={rememberSearch}
        history={history}
        onPickHistory={pickHistory}
        searchFocusToken={drawer.searchToken}
        activePlugin={activePlugin}
        snap={snap}
        onSnap={changeSnap}
        tileSize={prefs.tileSize}
        onTileSize={drawer.setTileSize}
        view={prefs.view}
        onView={drawer.setView}
      />

      <div className="ab-body">
        {prefs.treeOpen && (
          <nav className="ab-tree-pane" aria-label="Object categories">
            <CategoryTree nodes={tree} selection={category} onSelect={selectCategory} />
          </nav>
        )}
        {prefs.view === 'list' ? (
          <AssetBrowserTable
            items={items}
            total={total}
            loading={loading}
            failed={failed}
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
            onKeyDown={onGridKey}
          />
        ) : (
          <TileGrid
            items={items}
            tileSize={prefs.tileSize}
            loading={loading}
            failed={failed}
            selectedKeys={selectedKeys}
            activeKey={selection.anchor}
            onRowClick={onRowClick}
            onRowContext={onRowContext}
            onRowDragStart={onRowDragStart}
            onRowDragEnd={onRowDragEnd}
            onNavigate={moveActive}
            onKeyDown={onGridKey}
          />
        )}
        <DetailPane
          item={activeItem}
          favourite={activeItem !== null && isFavourite(library.favourites, activeItem.formKey)}
          collapsed={!prefs.detailOpen}
          onToggleCollapsed={() => drawer.setDetailOpen(!prefs.detailOpen)}
          onPlace={item => place([item], 'viewCentre')}
          onCopyKeys={item => copy([item.formKey])}
          onToggleFavourite={toggleFavouriteItem}
        />
      </div>

      <div className="ab-footer">
        <span className="ab-status">{status}</span>
        <button type="button" className="ab-more" aria-pressed={prefs.treeOpen} onClick={() => drawer.setTreeOpen(!prefs.treeOpen)}>
          {prefs.treeOpen ? 'Hide categories' : 'Show categories'}
        </button>
      </div>

      {shownError && (
        <div role="alert" className="ab-error">
          {shownError}
          {retryable && <button type="button" className="ab-more" onClick={scan.retry}>Retry</button>}
        </div>
      )}

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
