import { useEffect, useRef, useState, type Dispatch, type KeyboardEvent as ReactKeyboardEvent, type SetStateAction } from 'react';
import { ArrowDownUp, Search, X } from 'lucide-react';
import {
  BASE_OBJECT_MATCHES, BASE_OBJECT_SCOPES, BASE_OBJECT_SORTS,
  type BaseObjectMatch, type BaseObjectScope, type BaseObjectSort, type BrowserQueryState,
} from './baseObjects';
import { type BrowserView, TILE_SIZE_MAX, TILE_SIZE_MIN } from './drawerPrefs';
import { SNAP_GRID_SIZES } from './placement';

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

const SORT_LABELS: Record<BaseObjectSort, string> = {
  editorId: 'Editor ID',
  hexId: 'Form ID',
  name: 'Name',
  type: 'Type',
  modelPath: 'Model path',
  plugin: 'Plugin',
};

export interface DrawerToolbarProps {
  filters: BrowserQueryState;
  setFilters: Dispatch<SetStateAction<BrowserQueryState>>;
  text: string;
  onText: (text: string) => void;
  onClearSearch: () => void;
  onRememberSearch: () => void;
  history: string[];
  onPickHistory: (term: string) => void;
  searchFocusToken: number;
  activePlugin?: string;
  snap: number | null;
  onSnap: (value: string) => void;
  tileSize: number;
  onTileSize: (size: number) => void;
  view: BrowserView;
  onView: (view: BrowserView) => void;
}

export default function DrawerToolbar(props: DrawerToolbarProps) {
  const { filters, setFilters, text, activePlugin, snap, tileSize, view } = props;
  const inputRef = useRef<HTMLInputElement>(null);
  const [historyOpen, setHistoryOpen] = useState(false);

  useEffect(() => {
    if (props.searchFocusToken > 0) inputRef.current?.focus();
  }, [props.searchFocusToken]);

  const toggleScope = (scope: BaseObjectScope) => setFilters(prev => {
    const has = prev.scopes.includes(scope);
    if (has && prev.scopes.length === 1) return prev;
    return { ...prev, scopes: has ? prev.scopes.filter(s => s !== scope) : [...prev.scopes, scope] };
  });

  const toggleHideDeleted = () => setFilters(prev => ({ ...prev, hideDeleted: !prev.hideDeleted }));
  const toggleWithModel = () => setFilters(prev => ({ ...prev, placeableWithModel: !prev.placeableWithModel }));

  const toggleActivePlugin = () => setFilters(prev => ({ ...prev, activePlugin: prev.activePlugin !== '' ? '' : activePlugin ?? '' }));

  const onSearchKey = (e: ReactKeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      props.onRememberSearch();
    } else if (e.key === 'Escape') {
      e.preventDefault();
      props.onClearSearch();
    }
  };

  return (
    <div className="ab-toolbar">
      <div className="ab-search-row">
        <div className="ab-search">
          <Search size={13} className="ab-search-icon" />
          <input
            ref={inputRef}
            type="search"
            aria-label="Search objects"
            placeholder="Search objects (Ctrl+F)"
            value={text}
            onChange={e => props.onText(e.target.value)}
            onFocus={() => setHistoryOpen(true)}
            onBlur={() => setHistoryOpen(false)}
            onKeyDown={onSearchKey}
          />
          {text !== '' && (
            <button type="button" className="ab-icon-btn" aria-label="Clear search" onMouseDown={e => e.preventDefault()} onClick={props.onClearSearch}>
              <X size={12} />
            </button>
          )}
          {historyOpen && props.history.length > 0 && (
            <ul className="ab-history" role="listbox" aria-label="Search history">
              {props.history.map(term => (
                <li key={term} role="option" aria-selected={false}>
                  <button type="button" onMouseDown={e => e.preventDefault()} onClick={() => { props.onPickHistory(term); setHistoryOpen(false); }}>{term}</button>
                </li>
              ))}
            </ul>
          )}
        </div>
        <div className="ab-view-toggle" role="group" aria-label="View">
          <button type="button" aria-pressed={view === 'tiles'} className={`ab-chip${view === 'tiles' ? ' active' : ''}`} onClick={() => props.onView('tiles')}>Tiles</button>
          <button type="button" aria-pressed={view === 'list'} className={`ab-chip${view === 'list' ? ' active' : ''}`} onClick={() => props.onView('list')}>List</button>
        </div>
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

      <div className="ab-row-controls">
        <button type="button" aria-pressed={filters.activePlugin !== ''} disabled={!activePlugin} className={`ab-chip${filters.activePlugin !== '' ? ' active' : ''}`} onClick={toggleActivePlugin}>
          Active plugin only
        </button>
        <button type="button" aria-pressed={filters.hideDeleted} className={`ab-chip${filters.hideDeleted ? ' active' : ''}`} onClick={toggleHideDeleted}>
          Hide deleted
        </button>
        <button type="button" aria-pressed={filters.placeableWithModel} className={`ab-chip${filters.placeableWithModel ? ' active' : ''}`} onClick={toggleWithModel}>
          Only with model
        </button>
        <span className="ab-field">
          <ArrowDownUp size={12} aria-hidden="true" />
          <select aria-label="Sort by" value={filters.sortBy} onChange={e => setFilters(prev => ({ ...prev, sortBy: e.target.value as BaseObjectSort }))}>
            {BASE_OBJECT_SORTS.map(sort => <option key={sort} value={sort}>{SORT_LABELS[sort]}</option>)}
          </select>
          <button
            type="button"
            className="ab-icon-btn"
            aria-label="Toggle sort direction"
            onClick={() => setFilters(prev => ({ ...prev, sortDir: prev.sortDir === 'asc' ? 'desc' : 'asc' }))}
          >
            {filters.sortDir === 'asc' ? '\u25B2' : '\u25BC'}
          </button>
        </span>
        <span className="ab-field">
          <span>Snap grid</span>
          <select aria-label="Snap grid" value={snap === null ? 'off' : String(snap)} onChange={e => props.onSnap(e.target.value)}>
            <option value="off">Off</option>
            {SNAP_GRID_SIZES.map(size => <option key={size} value={size}>{size}</option>)}
          </select>
        </span>
        <label className="ab-field">
          <span>Tile size</span>
          <input
            type="range"
            aria-label="Tile size"
            min={TILE_SIZE_MIN}
            max={TILE_SIZE_MAX}
            step={8}
            value={tileSize}
            disabled={view !== 'tiles'}
            onChange={e => props.onTileSize(Number(e.target.value))}
          />
        </label>
      </div>
    </div>
  );
}
