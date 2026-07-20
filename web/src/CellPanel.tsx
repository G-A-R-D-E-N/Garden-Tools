import { useEffect, useRef, useState } from 'react';
import { Boxes, X, Play, CheckCircle2, AlertTriangle, XCircle, Search } from 'lucide-react';
import { getCell, type CellPlacedReference, type CellReferencesResult, type CellSearchHit } from './backend';
import CellViewport, { type CellGeoMap } from './CellViewport';
import './PapyrusPanel.css';
import './NifPanel.css';
import './CellPanel.css';

const LS = (k: string, d: string) => localStorage.getItem('cell.' + k) ?? d;
const setLS = (k: string, v: string) => localStorage.setItem('cell.' + k, v);

export default function CellPanel({ onClose }: { onClose: () => void }) {
  const [cellId, setCellId] = useState(() => LS('cellId', ''));
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('');
  const [result, setResult] = useState<CellReferencesResult | null>(null);
  const [geometry, setGeometry] = useState<CellGeoMap>({});
  const [log, setLog] = useState<string[]>([]);

  // Env-loaded gate: an empty plugin list means "nothing loaded yet" -- point the user at the
  // Explorer sidebar's Load Env / Open MO2 buttons instead of letting them type a cell id blind
  // into a panel that can't possibly resolve anything yet.
  const [pluginCount, setPluginCount] = useState<number | null>(null);   // null = still checking

  // Type-ahead cell picker: debounced search-as-you-type, dropdown of matches under the input.
  const [suggestions, setSuggestions] = useState<CellSearchHit[] | null>(null);
  const [suggestOpen, setSuggestOpen] = useState(false);
  const [suggestBusy, setSuggestBusy] = useState(false);
  const searchSeq = useRef(0);
  const boxRef = useRef<HTMLDivElement>(null);

  const cell = getCell();
  const unavailable = !cell;

  const appendLog = (line: string) =>
    setLog(prev => [`[${new Date().toLocaleTimeString()}] ${line}`, ...prev].slice(0, 200));

  const checkPlugins = async () => {
    if (!cell) { setPluginCount(0); return; }
    try {
      const raw = await cell.GetPlugins();
      const list = JSON.parse(raw) as string[];
      setPluginCount(Array.isArray(list) ? list.length : 0);
    } catch { setPluginCount(0); }
  };

  useEffect(() => { checkPlugins(); }, []); // eslint-disable-line react-hooks/exhaustive-deps
  // Re-check when the panel regains focus (covers "opened Cell Viewer, then loaded a modlist,
  // then came back") without needing a manual refresh button.
  useEffect(() => {
    const onFocus = () => checkPlugins();
    window.addEventListener('focus', onFocus);
    return () => window.removeEventListener('focus', onFocus);
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  const noEnv = pluginCount === 0;

  // Close the dropdown on an outside click.
  useEffect(() => {
    const onDocClick = (e: MouseEvent) => {
      if (boxRef.current && !boxRef.current.contains(e.target as Node)) setSuggestOpen(false);
    };
    document.addEventListener('mousedown', onDocClick);
    return () => document.removeEventListener('mousedown', onDocClick);
  }, []);

  // Debounced search-as-you-type against the loaded load order.
  useEffect(() => {
    if (!cell || noEnv) { setSuggestions(null); return; }
    const q = cellId.trim();
    if (q.length < 2) { setSuggestions(null); return; }
    const mySeq = ++searchSeq.current;
    setSuggestBusy(true);
    const t = setTimeout(async () => {
      try {
        const raw = await cell.SearchCells(q, 20);
        if (searchSeq.current !== mySeq) return; // a newer keystroke superseded this search
        setSuggestions(JSON.parse(raw) as CellSearchHit[]);
        setSuggestOpen(true);
      } catch {
        if (searchSeq.current === mySeq) setSuggestions([]);
      } finally {
        if (searchSeq.current === mySeq) setSuggestBusy(false);
      }
    }, 250);
    return () => clearTimeout(t);
  }, [cellId, cell, noEnv]);

  const load = async (idOverride?: string) => {
    const id = (idOverride ?? cellId).trim();
    if (!cell || !id) return;
    setSuggestOpen(false);
    setLS('cellId', id);
    setBusy(true);
    setResult(null);
    setGeometry({});
    setStatus('Reading placed references…');
    try {
      const raw = await cell.GetPlacedReferences(id);
      const parsed = JSON.parse(raw) as CellReferencesResult;
      if (parsed.error) {
        setStatus(parsed.error);
        appendLog('✗ ' + parsed.error);
        return;
      }
      setResult(parsed);
      const refs = parsed.references ?? [];
      appendLog(`• loaded ${parsed.cellEditorId || parsed.cellFormKey} — ${refs.length} reference(s), ${parsed.withModelCount ?? 0} with a model`);

      const uniqueModels = Array.from(new Set(refs.map(r => r.modelPath).filter((p): p is string => !!p)));
      if (uniqueModels.length === 0) { setStatus('Loaded — no placeable meshes in this cell.'); return; }

      setStatus(`Resolving + converting ${uniqueModels.length} unique mesh(es)…`);
      const geoRaw = await cell.GetGeometryBatch(JSON.stringify(uniqueModels));
      const geoParsed = JSON.parse(geoRaw) as { count?: number; geometry?: CellGeoMap; error?: string };
      if (geoParsed.error) {
        appendLog('✗ geometry batch: ' + geoParsed.error);
        setStatus('Loaded references, but geometry batch failed: ' + geoParsed.error);
        return;
      }
      const geoMap = geoParsed.geometry ?? {};
      setGeometry(geoMap);
      const failed = Object.values(geoMap).filter(g => 'error' in g).length;
      const ok = uniqueModels.length - failed;
      appendLog(`• geometry: ${ok}/${uniqueModels.length} mesh(es) resolved, ${failed} failed`);
      setStatus(failed > 0
        ? `Loaded ${refs.length} reference(s). ${ok}/${uniqueModels.length} unique meshes resolved — ${failed} unavailable (see markers).`
        : `Loaded ${refs.length} reference(s), all ${ok} unique mesh(es) resolved.`);
    } catch (e) {
      const msg = 'Error: ' + (e instanceof Error ? e.message : String(e));
      setStatus(msg);
      appendLog('✗ ' + msg);
    } finally {
      setBusy(false);
    }
  };

  const pickSuggestion = (hit: CellSearchHit) => {
    setCellId(hit.FormKey);
    setSuggestOpen(false);
    load(hit.FormKey);
  };

  const references: CellPlacedReference[] = result?.references ?? [];
  const failedModelCount = Object.values(geometry).filter(g => 'error' in g).length;
  const banner = statusBanner(status, busy);

  return (
    <div className="papyrus-overlay" onClick={onClose}>
      <div className="papyrus-modal glass-panel nif-modal-wide" onClick={e => e.stopPropagation()}>
        <div className="papyrus-header">
          <span className="papyrus-title"><Boxes size={16} /> Cell Viewer</span>
          <button className="papyrus-close" onClick={onClose} title="Close"><X size={16} /></button>
        </div>

        {unavailable && <div className="papyrus-warn">Cell bridge not available — run the desktop app (not the browser dev server).</div>}
        {!unavailable && noEnv && (
          <div className="papyrus-warn">
            No modlist loaded yet — click <strong>Load Env</strong> or <strong>Open MO2</strong> in the Explorer sidebar first, then come back here.
          </div>
        )}

        <div className="papyrus-body">
          <div className="papyrus-form">
            <label className="papyrus-field">
              <span>Cell — search by name/EditorID, or paste a FormKey</span>
              <div className="cell-search-box" ref={boxRef}>
                <div className="papyrus-input-row">
                  <Search size={13} className="cell-search-icon" />
                  <input
                    value={cellId}
                    onChange={e => setCellId(e.target.value)}
                    onFocus={() => { if (suggestions && suggestions.length > 0) setSuggestOpen(true); }}
                    placeholder={noEnv ? 'Load a modlist first…' : 'e.g. Sanctuary, or SanctuaryExt'}
                    disabled={unavailable || noEnv}
                    onKeyDown={e => { if (e.key === 'Enter') load(); if (e.key === 'Escape') setSuggestOpen(false); }}
                  />
                </div>
                {suggestOpen && (
                  <div className="cell-suggest">
                    {suggestBusy ? (
                      <div className="cell-suggest-empty">Searching…</div>
                    ) : !suggestions || suggestions.length === 0 ? (
                      <div className="cell-suggest-empty">No matching cells.</div>
                    ) : (
                      suggestions.map((h, i) => (
                        <div key={`${h.FormKey}:${i}`} className="cell-suggest-row" onClick={() => pickSuggestion(h)}>
                          <span className="cell-suggest-id">{h.EditorID || h.FormKey}</span>
                          {h.Name && <span className="cell-suggest-name">"{h.Name}"</span>}
                          <span className="cell-suggest-plugin">{h.Plugin}</span>
                        </div>
                      ))
                    )}
                  </div>
                )}
              </div>
            </label>
            <button className="papyrus-run cell-load-btn" onClick={() => load()} disabled={busy || unavailable || noEnv || !cellId.trim()}>
              <Play size={14} /> {busy ? 'Loading…' : 'Load Cell'}
            </button>

            {result && (
              <div className="cell-stats">
                <div className="cell-stat"><span>Cell</span><strong>{result.cellEditorId || result.cellFormKey}</strong></div>
                <div className="cell-stat"><span>Interior</span><strong>{result.interior ? 'Yes' : 'No'}</strong></div>
                <div className="cell-stat"><span>References</span><strong>{result.referenceCount ?? references.length}</strong></div>
                <div className="cell-stat"><span>With model</span><strong>{result.withModelCount ?? 0}</strong></div>
                {failedModelCount > 0 && (
                  <div className="cell-stat cell-stat-warn"><span>Meshes unavailable</span><strong>{failedModelCount}</strong></div>
                )}
              </div>
            )}

            <div className="cell-legend">
              <div><span className="cell-swatch cell-swatch-mesh" /> resolved mesh</div>
              <div><span className="cell-swatch cell-swatch-nomodel" /> no model (actor/trap)</div>
              <div><span className="cell-swatch cell-swatch-failed" /> mesh unavailable</div>
            </div>
            <div className="nif-view-hint">Drag to orbit · scroll to zoom · right-drag to pan</div>

            <div className="papyrus-log-head">
              <span>LOG ({log.length})</span>
              {log.length > 0 && <button className="papyrus-copy" onClick={() => setLog([])}>Clear</button>}
            </div>
            <div className="papyrus-log-body cell-log-body">
              {log.length === 0 ? <div className="papyrus-log-empty">No runs yet.</div>
                : log.map((l, i) => <div key={i} className={`papyrus-log-row ${l.includes('✗') ? 'err' : 'ok'}`}>{l}</div>)}
            </div>
          </div>

          <div className="papyrus-output nif-view-output">
            {banner && (
              <div className={`papyrus-banner ${banner.kind}`}>
                {banner.kind === 'ok' ? <CheckCircle2 size={15} /> : banner.kind === 'warn' ? <AlertTriangle size={15} /> : <XCircle size={15} />}
                <span className="papyrus-banner-text">{status}</span>
              </div>
            )}
            {references.length > 0
              ? <CellViewport references={references} geometry={geometry} />
              : (
                <div className="nif-view-empty">
                  {unavailable ? 'Cell bridge unavailable.'
                    : noEnv ? 'Load a modlist first, then search for a cell.'
                    : 'Search for a cell above, or paste a FormKey/EditorID and press Load Cell.'}
                </div>
              )}
          </div>
        </div>
      </div>
    </div>
  );
}

function statusBanner(status: string, busy: boolean): { kind: 'ok' | 'warn' | 'error' } | null {
  if (!status || busy) return null;
  if (/error|could not|not available/i.test(status)) return { kind: 'error' };
  if (/unavailable/i.test(status)) return { kind: 'warn' };
  return { kind: 'ok' };
}
