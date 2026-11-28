import { Play, Search } from 'lucide-react';
import type { CellSearchHit } from './backend';
import type { CellSearch } from './useCellSearch';
import { parseCellGridCoordinate } from './util/cellGrid';

type Props = {
  pickMode: 'cell' | 'grid';
  onPickMode: (mode: 'cell' | 'grid') => void;
  search: CellSearch;
  cellId: string;
  onCellId: (value: string) => void;
  worldspace: string;
  onWorldspace: (value: string) => void;
  gridX: string;
  gridY: string;
  onGridX: (value: string) => void;
  onGridY: (value: string) => void;
  busy: boolean;
  unavailable: boolean;
  noEnv: boolean;
  onLoad: () => void;
  onLoadGrid: () => void;
  onCreate: () => void;
  onPickMatch: (hit: CellSearchHit) => void;
};

export default function CellSourcePicker(p: Props) {
  const { pickMode, search, cellId, worldspace, gridX, gridY, busy, unavailable, noEnv } = p;
  const { matches, matchesLoading, wsMatches, wsLoading } = search;
  const gridInvalid = !worldspace.trim() || parseCellGridCoordinate(gridX) === null || parseCellGridCoordinate(gridY) === null;
  return (
    <>
      <div className="cell-mode-tabs">
        <button
          className={`cell-mode-tab${pickMode === 'cell' ? ' active' : ''}`}
          onClick={() => p.onPickMode('cell')}
        >By name</button>
        <button
          className={`cell-mode-tab${pickMode === 'grid' ? ' active' : ''}`}
          onClick={() => p.onPickMode('grid')}
          title="Exterior cells are addressed by worldspace + grid coordinate"
        >Exterior grid</button>
      </div>

      {pickMode === 'grid' && (
        <>
          <div className="cell-dropdown">
            <div className="cell-dropdown-search">
              <Search size={13} className="cell-search-icon" />
              <input
                value={worldspace}
                onChange={e => p.onWorldspace(e.target.value)}
                placeholder={noEnv ? 'Load a modlist first…' : 'Search worldspaces…'}
                disabled={unavailable || noEnv}
                onKeyDown={e => { if (e.key === 'Enter') p.onLoadGrid(); }}
              />
              {wsLoading && <span className="cell-dropdown-spinner" title="Updating…" />}
            </div>
            <div className="cell-dropdown-list cell-dropdown-list-short">
              {noEnv || unavailable ? (
                <div className="cell-suggest-empty">{unavailable ? 'Cell bridge unavailable.' : 'Load a modlist to browse worldspaces.'}</div>
              ) : wsMatches === null ? (
                <div className="cell-suggest-empty">Loading…</div>
              ) : wsMatches.length === 0 ? (
                <div className="cell-suggest-empty">No matching worldspaces.</div>
              ) : (
                wsMatches.map((h, i) => (
                  <div
                    key={`${h.FormKey}:${i}`}
                    className={`cell-suggest-row${worldspace.trim() === h.FormKey ? ' selected' : ''}`}
                    onClick={() => p.onWorldspace(h.FormKey)}
                  >
                    <span className="cell-suggest-id">{h.EditorID || h.FormKey}</span>
                    {h.Name && <span className="cell-suggest-name">"{h.Name}"</span>}
                    <span className="cell-suggest-plugin">{h.Plugin}</span>
                  </div>
                ))
              )}
            </div>
          </div>
          <div className="cell-grid-inputs">
            <label>X<input
              type="number"
              value={gridX}
              onChange={e => p.onGridX(e.target.value)}
              disabled={unavailable || noEnv}
              onKeyDown={e => { if (e.key === 'Enter') p.onLoadGrid(); }}
            /></label>
            <label>Y<input
              type="number"
              value={gridY}
              onChange={e => p.onGridY(e.target.value)}
              disabled={unavailable || noEnv}
              onKeyDown={e => { if (e.key === 'Enter') p.onLoadGrid(); }}
            /></label>
          </div>
          <button
            className="papyrus-run cell-load-btn"
            onClick={() => p.onLoadGrid()}
            disabled={busy || unavailable || noEnv || gridInvalid}
          >
            <Play size={14} /> {busy ? 'Loading…' : 'Load Cell'}
          </button>
          <button
            className="papyrus-run cell-load-btn"
            onClick={() => p.onCreate()}
            disabled={busy || unavailable || noEnv || gridInvalid}
            title="Create a new exterior CELL via CreateRecord with worldspace FormKey, x, and y"
          >
            <Play size={14} /> {busy ? 'Working…' : 'Create exterior CELL'}
          </button>
        </>
      )}

      {pickMode === 'cell' && (
        <>
          <div className="cell-dropdown">
            <div className="cell-dropdown-search">
              <Search size={13} className="cell-search-icon" />
              <input
                value={cellId}
                onChange={e => p.onCellId(e.target.value)}
                placeholder={noEnv ? 'Load a modlist first…' : 'Search cells by name, EditorID, or FormKey…'}
                disabled={unavailable || noEnv}
                onKeyDown={e => { if (e.key === 'Enter') p.onLoad(); }}
              />
              {matchesLoading && <span className="cell-dropdown-spinner" title="Updating…" />}
            </div>
            <div className="cell-dropdown-list">
              {noEnv || unavailable ? (
                <div className="cell-suggest-empty">{unavailable ? 'Cell bridge unavailable.' : 'Load a modlist to browse cells.'}</div>
              ) : matches === null ? (
                <div className="cell-suggest-empty">Loading…</div>
              ) : matches.length === 0 ? (
                <div className="cell-suggest-empty">No matching cells.</div>
              ) : (
                matches.map((h, i) => (
                  <div key={`${h.FormKey}:${i}`} className="cell-suggest-row" onClick={() => p.onPickMatch(h)}>
                    <span className="cell-suggest-id">{h.EditorID || h.FormKey}</span>
                    {h.Name && <span className="cell-suggest-name">"{h.Name}"</span>}
                    <span className="cell-suggest-plugin">{h.Plugin}</span>
                  </div>
                ))
              )}
            </div>
          </div>
          <button className="papyrus-run cell-load-btn" onClick={() => p.onLoad()} disabled={busy || unavailable || noEnv || !cellId.trim()}>
            <Play size={14} /> {busy ? 'Loading…' : 'Load Cell'}
          </button>
        </>
      )}
    </>
  );
}
