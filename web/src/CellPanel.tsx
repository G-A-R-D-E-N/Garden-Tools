import { useState } from 'react';
import { Boxes, X, Play, CheckCircle2, AlertTriangle, XCircle } from 'lucide-react';
import { getCell, type CellPlacedReference, type CellReferencesResult } from './backend';
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

  const cell = getCell();
  const unavailable = !cell;

  const appendLog = (line: string) =>
    setLog(prev => [`[${new Date().toLocaleTimeString()}] ${line}`, ...prev].slice(0, 200));

  const load = async () => {
    if (!cell || !cellId.trim()) return;
    setLS('cellId', cellId);
    setBusy(true);
    setResult(null);
    setGeometry({});
    setStatus('Reading placed references…');
    try {
      const raw = await cell.GetPlacedReferences(cellId.trim());
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

        <div className="papyrus-body">
          <div className="papyrus-form">
            <label className="papyrus-field">
              <span>Cell (FormKey like 001234:Fallout4.esm, or an EditorID)</span>
              <div className="papyrus-input-row">
                <input value={cellId} onChange={e => setCellId(e.target.value)}
                  placeholder="e.g. SanctuaryHouse01" onKeyDown={e => { if (e.key === 'Enter') load(); }} />
              </div>
            </label>
            <button className="papyrus-run" onClick={load} disabled={busy || unavailable || !cellId.trim()}>
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
              : <div className="nif-view-empty">{unavailable ? 'Cell bridge unavailable.' : 'Enter a cell FormKey or EditorID, then Load Cell.'}</div>}
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
