import { useCallback, useEffect, useRef, useState } from 'react';
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

  // Persistent dropdown: search bar on top, a list underneath that's ALWAYS rendered once a
  // modlist is loaded (never appears/disappears based on typing). A new search REPLACES the list
  // only when its results actually arrive -- the previous list stays put (just dimmed) while a
  // query is in flight, so typing never blanks-then-repopulates the panel. That flash-on-every-
  // keystroke was the direct complaint that led to this rewrite.
  const [matches, setMatches] = useState<CellSearchHit[] | null>(null);  // null = not loaded yet
  const [matchesLoading, setMatchesLoading] = useState(false);
  const searchSeq = useRef(0);

  // Real N/total progress for the geometry-conversion phase (niftool runs one real child process
  // per unique mesh -- polled from NifService.GeoBatchDone/Total while the batch call is in flight,
  // not an indeterminate spinner).
  const [geoProgress, setGeoProgress] = useState<{ done: number; total: number } | null>(null);

  const cell = getCell();
  const unavailable = !cell;

  const loadTexture = useCallback(async (modelPath: string, relTexPath: string): Promise<string> => {
    const c = getCell();
    if (!c) return '';
    try { return await c.GetTexture(modelPath, relTexPath); } catch { return ''; }
  }, []);

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

  // Debounced search-as-you-type against the loaded load order. Empty query still searches (limit
  // cells in whatever order the load order returns them) so the panel reads as a real, immediately
  // browsable dropdown the moment a modlist is loaded, not an empty box waiting for input.
  useEffect(() => {
    if (!cell || noEnv) { setMatches(null); return; }
    const mySeq = ++searchSeq.current;
    setMatchesLoading(true);
    const t = setTimeout(async () => {
      try {
        const raw = await cell.SearchCells(cellId.trim(), 30);
        if (searchSeq.current !== mySeq) return; // a newer keystroke superseded this search
        setMatches(JSON.parse(raw) as CellSearchHit[]);
      } catch {
        if (searchSeq.current === mySeq) setMatches([]);
      } finally {
        if (searchSeq.current === mySeq) setMatchesLoading(false);
      }
    }, 250);
    return () => clearTimeout(t);
  }, [cellId, cell, noEnv]);

  const load = async (idOverride?: string) => {
    const id = (idOverride ?? cellId).trim();
    if (!cell || !id) return;
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
      appendLog(`• loaded ${parsed.cellEditorId || parsed.cellFormKey} -- ${refs.length} reference(s), ${parsed.withModelCount ?? 0} with a model`);

      const uniqueModels = Array.from(new Set(refs.map(r => r.modelPath).filter((p): p is string => !!p)));
      if (uniqueModels.length === 0) { setStatus('Loaded -- no placeable meshes in this cell.'); return; }

      setStatus(`Resolving + converting ${uniqueModels.length} unique mesh(es)…`);
      setGeoProgress({ done: 0, total: uniqueModels.length });
      const pollTimer = setInterval(async () => {
        try {
          const raw = await cell.GetGeometryBatchProgress();
          setGeoProgress(JSON.parse(raw) as { done: number; total: number });
        } catch { /* a missed poll tick isn't worth surfacing */ }
      }, 200);
      let geoRaw: string;
      try {
        geoRaw = await cell.GetGeometryBatch(JSON.stringify(uniqueModels));
      } finally {
        clearInterval(pollTimer);
        setGeoProgress(null);
      }
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
        ? `Loaded ${refs.length} reference(s). ${ok}/${uniqueModels.length} unique meshes resolved -- ${failed} unavailable (see markers).`
        : `Loaded ${refs.length} reference(s), all ${ok} unique mesh(es) resolved.`);
    } catch (e) {
      const msg = 'Error: ' + (e instanceof Error ? e.message : String(e));
      setStatus(msg);
      appendLog('✗ ' + msg);
    } finally {
      setBusy(false);
    }
  };

  const pickMatch = (hit: CellSearchHit) => {
    setCellId(hit.FormKey);
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

        {unavailable && <div className="papyrus-warn">Cell bridge not available -- run the desktop app (not the browser dev server).</div>}
        {!unavailable && noEnv && (
          <div className="papyrus-warn">
            No modlist loaded yet -- click <strong>Load Env</strong> or <strong>Open MO2</strong> in the Explorer sidebar first, then come back here.
          </div>
        )}

        <div className="papyrus-body">
          <div className="papyrus-form">
            <div className="cell-dropdown">
              <div className="cell-dropdown-search">
                <Search size={13} className="cell-search-icon" />
                <input
                  value={cellId}
                  onChange={e => setCellId(e.target.value)}
                  placeholder={noEnv ? 'Load a modlist first…' : 'Search cells by name, EditorID, or FormKey…'}
                  disabled={unavailable || noEnv}
                  onKeyDown={e => { if (e.key === 'Enter') load(); }}
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
                    <div key={`${h.FormKey}:${i}`} className="cell-suggest-row" onClick={() => pickMatch(h)}>
                      <span className="cell-suggest-id">{h.EditorID || h.FormKey}</span>
                      {h.Name && <span className="cell-suggest-name">"{h.Name}"</span>}
                      <span className="cell-suggest-plugin">{h.Plugin}</span>
                    </div>
                  ))
                )}
              </div>
            </div>
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
            {geoProgress ? (
              <div className="cell-progress">
                <div className="cell-progress-label">
                  <span>Converting meshes…</span>
                  <span>{geoProgress.done}/{geoProgress.total}</span>
                </div>
                <div className="cell-progress-track">
                  <div className="cell-progress-fill" style={{ width: `${geoProgress.total > 0 ? (geoProgress.done / geoProgress.total) * 100 : 0}%` }} />
                </div>
              </div>
            ) : banner && (
              <div className={`papyrus-banner ${banner.kind}`}>
                {banner.kind === 'ok' ? <CheckCircle2 size={15} /> : banner.kind === 'warn' ? <AlertTriangle size={15} /> : <XCircle size={15} />}
                <span className="papyrus-banner-text">{status}</span>
              </div>
            )}
            {references.length > 0
              ? <CellViewport references={references} geometry={geometry} loadTexture={loadTexture} />
              : (
                <div className="nif-view-empty">
                  {unavailable ? 'Cell bridge unavailable.'
                    : noEnv ? 'Load a modlist first, then pick a cell from the list.'
                    : 'Pick a cell from the list, or paste a FormKey/EditorID and press Load Cell.'}
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
