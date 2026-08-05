import { useState } from 'react';
import { ChevronDown, ChevronRight, RotateCcw } from 'lucide-react';
import type { RecordTypeEntry } from '../backend';
import { EMPTY_FILTERS, type NavigatorFilterState } from './filters';
import './NavigatorFilters.css';

interface NavigatorFiltersProps {
  types: RecordTypeEntry[];
  value: NavigatorFilterState;
  onChange: (next: NavigatorFilterState) => void;
}

export default function NavigatorFilters({ types, value, onChange }: NavigatorFiltersProps) {
  const [open, setOpen] = useState(true);
  const dirty = value.type !== '' || value.needle !== '';

  return (
    <section className="nav-section nav-filters">
      <header className="nav-section-head" onClick={() => setOpen(o => !o)}>
        {open ? <ChevronDown size={13} /> : <ChevronRight size={13} />}
        <span className="nav-section-title">FILTERS</span>
        {dirty && (
          <button
            className="nav-icon-btn"
            title="Reset filters"
            onClick={e => { e.stopPropagation(); onChange(EMPTY_FILTERS); }}
          >
            <RotateCcw size={13} />
          </button>
        )}
      </header>

      {open && (
        <div className="nf-body">
          <label className="nf-field">
            <span className="nf-label">Record Type</span>
            <select
              value={value.type}
              onChange={e => onChange({ ...value, type: e.target.value })}
            >
              <option value="">All types{types.length ? ` (${types.length})` : ''}</option>
              {types.map(t => (
                <option key={t.Type} value={t.Type}>
                  {t.FriendlyName} ({t.Count.toLocaleString()})
                </option>
              ))}
            </select>
          </label>

          <label className="nf-field">
            <span className="nf-label">FormID / Editor ID</span>
            <input
              value={value.needle}
              placeholder="e.g. 0001F1A2 or RedRocket"
              onChange={e => onChange({ ...value, needle: e.target.value })}
            />
          </label>

          <button
            className="nf-reset"
            disabled={!dirty}
            onClick={() => onChange(EMPTY_FILTERS)}
          >
            Reset Filters
          </button>
        </div>
      )}
    </section>
  );
}
