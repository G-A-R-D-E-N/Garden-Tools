import { useEffect, useRef, useState } from 'react';
import { Search, X } from 'lucide-react';
import type { SearchHit } from './backend';
import { errorText } from './errors';
import './RecordPicker.css';

const back = () => window.chrome?.webview?.hostObjects?.backend;

export interface PickedRecord { formKey: string; plugin: string; }

export default function RecordPicker(
  { title, refType, refTypes, onPick, onClose }:
  { title: string; refType: string | null; refTypes: string | null; onPick: (formKey: string, plugin: string) => void; onClose: () => void }
) {
  const [query, setQueryState] = useState('');
  const [hits, setHits] = useState<SearchHit[] | null>(null);
  const [error, setError] = useState('');
  const setQuery = (v: string) => { setQueryState(v); setHits(null); };
  const [filterOn, setFilterOn] = useState(!!refTypes);
  const timer = useRef<number | null>(null);

  const activeFilter = filterOn && refTypes ? refTypes : '';


  useEffect(() => {
    const b = back();
    if (!b) return;
    if (timer.current) window.clearTimeout(timer.current);
    timer.current = window.setTimeout(async () => {
      try { setHits(JSON.parse(await b.SearchRecords(query, activeFilter))); setError(''); }
      catch (e) { setError(errorText(e)); setHits([]); }
    }, 250);
    return () => { if (timer.current) window.clearTimeout(timer.current); };
  }, [query, activeFilter]);

  return (
    <div className="rp-overlay" onClick={onClose}>
      <div className="rp-modal" onClick={e => e.stopPropagation()}>
        <div className="rp-header">
          <span>Pick a record{title ? ` -- ${title}` : ''}</span>
          <button onClick={onClose} title="Close"><X size={16} /></button>
        </div>
        <div className="rp-search">
          <Search size={14} />
          <input autoFocus value={query} onChange={e => setQuery(e.target.value)} placeholder="Search EditorID or FormID…" />
          {refTypes && (
            <button
              className={`rp-chip ${filterOn ? 'on' : ''}`}
              onClick={() => { setFilterOn(v => !v); setHits(null); }}
              title={filterOn ? `Showing only valid types -- click to show all` : 'Click to filter to the field type'}
            >
              {refType || 'type'}{filterOn ? '' : ' (off)'}
            </button>
          )}
        </div>
        <div className="rp-results">
          {hits === null ? <div className="rp-note">Searching…</div>
            : hits.length === 0 ? <div className="rp-note">{error ? `Search failed: ${error}` : 'No matches.'}</div>
            : hits.map(h => (
              <div key={h.Plugin + h.FormKey} className="rp-row" onClick={() => onPick(h.FormKey, h.Plugin)}>
                <span className="rp-type">{h.Type}</span>
                <span className="rp-id">{h.EditorID || '(no EditorID)'}</span>
                <span className="rp-fk">{h.FormKey}</span>
                <span className="rp-plugin">{h.Plugin}</span>
              </div>
            ))}
        </div>
        <div className="rp-footer">Pick sets the FormLink in the selected column. Save the plugin to persist.</div>
      </div>
    </div>
  );
}
