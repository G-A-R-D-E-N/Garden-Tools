import { useMemo, useState } from 'react';
import { ChevronDown, ChevronRight, FilePlus, Search, X } from 'lucide-react';
import type { ActivePlugin } from '../backend';
import { pluginBadge, pluginColorVar } from '../util/pluginColor';
import './FileBrowser.css';

interface FileBrowserProps {
  plugins: ActivePlugin[];
  /** Plugins the navigator is restricted to. Empty means "no restriction", not "show nothing". */
  selected: Set<string>;
  onToggleSelected: (name: string) => void;
  onClearSelection: () => void;
  onAddFile: () => void;
}

const KIND_LABEL: Record<string, string> = { master: 'ESM', plugin: 'ESP', light: 'ESL' };

/** Active Files: the load order, with a per-file filter and click-to-restrict-the-navigator. */
export default function FileBrowser({
  plugins, selected, onToggleSelected, onClearSelection, onAddFile,
}: FileBrowserProps) {
  const [open, setOpen] = useState(true);
  const [filter, setFilter] = useState('');

  const shown = useMemo(() => {
    const q = filter.trim().toLowerCase();
    return q ? plugins.filter(p => p.Name.toLowerCase().includes(q)) : plugins;
  }, [plugins, filter]);

  return (
    <section className="nav-section file-browser">
      <header className="nav-section-head" onClick={() => setOpen(o => !o)}>
        {open ? <ChevronDown size={13} /> : <ChevronRight size={13} />}
        <span className="nav-section-title">ACTIVE FILES ({plugins.length})</span>
        {selected.size > 0 && (
          <span
            className="fb-clear"
            role="button"
            title="Clear file restriction"
            onClick={e => { e.stopPropagation(); onClearSelection(); }}
          >
            {selected.size} selected <X size={11} />
          </span>
        )}
        <button
          className="nav-icon-btn"
          title="Add a plugin"
          onClick={e => { e.stopPropagation(); onAddFile(); }}
        >
          <FilePlus size={14} />
        </button>
      </header>

      {open && (
        <>
          <div className="nav-filter">
            <Search size={12} />
            <input
              value={filter}
              placeholder="Filter files..."
              onChange={e => setFilter(e.target.value)}
            />
            {filter && (
              <button className="nav-filter-clear" onClick={() => setFilter('')} title="Clear">
                <X size={12} />
              </button>
            )}
          </div>

          <div className="fb-list">
            {plugins.length === 0 ? (
              <div className="nav-empty">No plugins loaded.</div>
            ) : shown.length === 0 ? (
              <div className="nav-empty">No file matches "{filter}".</div>
            ) : (
              shown.map(p => (
                <div
                  key={p.Name}
                  className={`fb-row ${selected.has(p.Name) ? 'selected' : ''}`}
                  onClick={() => onToggleSelected(p.Name)}
                  title={`${p.Name} - load order ${p.LoadOrder}${p.Editable ? ' - open for editing' : ''}`}
                >
                  <span className="fb-badge" style={{ background: pluginColorVar(p.Name) }}>
                    {pluginBadge(p.Name)}
                  </span>
                  <span className="fb-name">{p.Name}</span>
                  {p.Editable && <span className="fb-dot" title="Open for editing" />}
                  <span className={`fb-kind fb-kind-${p.Kind}`}>{KIND_LABEL[p.Kind] ?? 'ESP'}</span>
                </div>
              ))
            )}
          </div>
        </>
      )}
    </section>
  );
}
