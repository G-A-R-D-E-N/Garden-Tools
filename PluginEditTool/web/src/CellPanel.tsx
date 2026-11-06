import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { useDialogs } from './useDialogs';
import { Boxes, X, Play, CheckCircle2, AlertTriangle, XCircle, Search, Eye, EyeOff, Info, ScrollText } from 'lucide-react';
import { getCell, getBackend, type CellPlacedReference, type CellReferencesResult, type CellSearchHit } from './backend';
import { errorText } from './errors';
import CellViewport, { type CellGeoMap, type CellTextureStats } from './CellViewport';
import { cellLayerOf, MARKER_LAYER } from './util/cellLayer';
import { parseCellGridCoordinate } from './util/cellGrid';
import { statusBanner } from './util/cellStatus';
import { pushHistory, redoHistory, undoHistory, type UndoHistory } from './util/undoHistory';
import './PapyrusPanel.css';
import './NifPanel.css';
import './CellPanel.css';

const LS = (k: string, d: string) => localStorage.getItem('cell.' + k) ?? d;
const setLS = (k: string, v: string) => localStorage.setItem('cell.' + k, v);

export default function CellPanel({ onClose }: { onClose: () => void }) {
  const { pickPlugin: askForTarget } = useDialogs();
  const [cellId, setCellId] = useState(() => LS('cellId', ''));
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('');
  const [result, setResult] = useState<CellReferencesResult | null>(null);
  const [loadedWorldspace, setLoadedWorldspace] = useState<string | null>(null);
  const [geometry, setGeometry] = useState<CellGeoMap>({});
  const [log, setLog] = useState<string[]>([]);





  const [sidebarTab, setSidebarTab] = useState<'info' | 'log'>('info');
  const [hasUnseenError, setHasUnseenError] = useState(false);


  const [selected, setSelected] = useState<CellPlacedReference | null>(null);
  const [hiddenTypes, setHiddenTypes] = useState<Set<string>>(new Set());
  const [hiddenRefs, setHiddenRefs] = useState<Set<string>>(new Set());





  type VisSnapshot = { types: Set<string>; refs: Set<string> };
  const [history, setHistory] = useState<UndoHistory<VisSnapshot>>({ undo: [], redo: [] });

  const snapshotVisibility = (): VisSnapshot => ({ types: new Set(hiddenTypes), refs: new Set(hiddenRefs) });
  const pushUndo = () => {
    const snap = snapshotVisibility();
    setHistory(h => pushHistory(h, snap));
  };
  const applySnapshot = (s: VisSnapshot) => { setHiddenTypes(s.types); setHiddenRefs(s.refs); };
  const undoVisibility = () => {
    const prev = history.undo.at(-1);
    if (!prev) return;
    const cur = snapshotVisibility();
    setHistory(h => undoHistory(h, cur));
    applySnapshot(prev);
  };
  const redoVisibility = () => {
    const next = history.redo.at(-1);
    if (!next) return;
    const cur = snapshotVisibility();
    setHistory(h => redoHistory(h, cur));
    applySnapshot(next);
  };
  const resetVisibilityHistory = () => setHistory({ undo: [], redo: [] });




  const [pluginCount, setPluginCount] = useState<number | null>(null);






  const [matches, setMatches] = useState<CellSearchHit[] | null>(null);
  const [matchesLoading, setMatchesLoading] = useState(false);
  const searchSeq = useRef(0);
  const loadSeq = useRef(0);




  const [geoProgress, setGeoProgress] = useState<{ done: number; total: number } | null>(null);





  const [texStats, setTexStats] = useState<CellTextureStats | null>(null);
  const texStatsLoggedRef = useRef('');

  const cell = getCell();
  const unavailable = !cell;

  const loadTexture = useCallback(async (modelPath: string, relTexPath: string): Promise<string> => {
    const c = getCell();
    if (!c) return '';
    try { return await c.GetTexture(modelPath, relTexPath); } catch { return ''; }
  }, []);

  const appendLog = (line: string) => {
    setLog(prev => [`[${new Date().toLocaleTimeString()}] ${line}`, ...prev].slice(0, 200));
    if (line.includes('✗') && sidebarTab !== 'log') setHasUnseenError(true);
  };

  const gizmoPatchRef = useRef<string | null>(null);



  const onTextureStats = useCallback((s: CellTextureStats) => {
    setTexStats(s);
    const failed = s.resolveFail + s.decodeFail;
    if (failed === 0 && s.noPath === 0) return;
    const line = `Textures: ${s.ok} loaded, ${s.resolveFail} not found, ${s.decodeFail} failed to decode, `
      + `${s.noPath} shape${s.noPath === 1 ? '' : 's'} with no texture path`
      + (s.firstFailure ? `. First failure: ${s.firstFailure}` : '');
    if (texStatsLoggedRef.current === line) return;
    texStatsLoggedRef.current = line;
    appendLog(`${failed > 0 ? '✗' : 'ℹ'} ${line}`);
  }, []);

  const checkPlugins = () => cell?.GetPlugins()
    .then(raw => {
      const list = JSON.parse(raw) as string[];
      setPluginCount(Array.isArray(list) ? list.length : 0);
    })
    .catch(e => { setPluginCount(0); setStatus('Could not list plugins: ' + errorText(e)); });

  useEffect(() => { checkPlugins(); }, []);


  useEffect(() => {
    const onFocus = () => checkPlugins();
    window.addEventListener('focus', onFocus);
    return () => window.removeEventListener('focus', onFocus);
  }, []);

  const noEnv = pluginCount === 0;




  useEffect(() => {
    if (!cell || noEnv) return;
    const mySeq = ++searchSeq.current;
    const t = setTimeout(async () => {
      setMatchesLoading(true);
      try {
        const raw = await cell.SearchCells(cellId.trim(), 30);
        if (searchSeq.current !== mySeq) return;
        setMatches(JSON.parse(raw) as CellSearchHit[]);
      } catch (e) {
        if (searchSeq.current === mySeq) { setMatches([]); setStatus('Could not search cells: ' + errorText(e)); }
      } finally {
        if (searchSeq.current === mySeq) setMatchesLoading(false);
      }
    }, 250);
    return () => clearTimeout(t);
  }, [cellId, cell, noEnv]);






  const [pickMode, setPickMode] = useState<'cell' | 'grid'>(() => (LS('pickMode', 'cell') === 'grid' ? 'grid' : 'cell'));
  const [worldspace, setWorldspace] = useState(() => LS('worldspace', ''));
  const [wsMatches, setWsMatches] = useState<CellSearchHit[] | null>(null);
  const [wsLoading, setWsLoading] = useState(false);
  const [gridX, setGridX] = useState(() => LS('gridX', '0'));
  const [gridY, setGridY] = useState(() => LS('gridY', '0'));
  const wsSeq = useRef(0);

  useEffect(() => {
    if (!cell || noEnv || pickMode !== 'grid') return;
    const mySeq = ++wsSeq.current;
    const t = setTimeout(async () => {
      setWsLoading(true);
      try {
        const raw = await cell.SearchWorldspaces(worldspace.trim(), 100);
        if (wsSeq.current !== mySeq) return;
        setWsMatches(JSON.parse(raw) as CellSearchHit[]);
      } catch (e) {
        if (wsSeq.current === mySeq) { setWsMatches([]); setStatus('Could not search worldspaces: ' + errorText(e)); }
      } finally {
        if (wsSeq.current === mySeq) setWsLoading(false);
      }
    }, 250);
    return () => clearTimeout(t);
  }, [worldspace, cell, noEnv, pickMode]);

  const load = async (idOverride?: string) => {
    const id = (idOverride ?? cellId).trim();
    if (!cell || !id) return;
    setLS('cellId', id);
    await runLoad(() => cell.GetPlacedReferences(id));
  };

  const resolveWorldspaceFormKey = async (ws: string): Promise<string | null> => {
    if (!cell || !ws) return null;
    try {
      const reply = JSON.parse(await cell.ResolveWorldspaceId(ws)) as { worldspaceFormKey?: string | null };
      return reply.worldspaceFormKey ?? null;
    } catch {
      return null;
    }
  };

  const loadGrid = async (wsOverride?: string) => {
    const ws = (wsOverride ?? worldspace).trim();
    const x = parseCellGridCoordinate(gridX);
    const y = parseCellGridCoordinate(gridY);
    if (!cell || !ws || x === null || y === null) return;
    setLS('worldspace', ws);
    setLS('gridX', String(x));
    setLS('gridY', String(y));
    await runLoad(() => cell.GetPlacedReferencesAtGrid(ws, x, y));
  };

  const createExteriorCell = async () => {
    const backend = getBackend();
    if (!backend || !cell) return;
    const formKey = await resolveWorldspaceFormKey(worldspace.trim());
    const x = parseCellGridCoordinate(gridX);
    const y = parseCellGridCoordinate(gridY);
    if (!formKey || x === null || y === null) {
      const msg = !formKey
        ? 'Pick a worldspace from the list (Create Record needs its FormKey).'
        : 'Grid X and Y must be integers.';
      setStatus(msg);
      appendLog('✗ ' + msg);
      return;
    }
    const picked = await askForTarget({
      title: 'Create exterior CELL into',
      description: `New exterior CELL at (${x}, ${y}) in ${formKey}.`,
      confirmLabel: 'Create',
      defaultTarget: LS('gizmoPatch', 'CellEdits.esp'),
      extraField: { label: 'EditorID (optional)', defaultValue: '', placeholder: 'MyNewCell' },
    });
    if (!picked) {
      appendLog('x create cancelled -- no target plugin chosen.');
      return;
    }
    setBusy(true);
    setStatus('Creating exterior CELL…');
    try {
      const payload: Record<string, unknown> = { worldspace: formKey, x, y };
      if (picked.extra) payload.editorId = picked.extra;
      const raw = await backend.CreateRecord(picked.target, 'CELL', payload);
      const created = typeof raw === 'string' ? JSON.parse(raw) : raw;
      if (!created?.formKey) throw new Error(typeof raw === 'string' ? raw : JSON.stringify(raw));
      const saveMsg = await backend.SavePlugin(picked.target, '');
      if (/error|fail/i.test(saveMsg)) {
        appendLog(`• created ${created.formKey} in memory; save: ${saveMsg}`);
        setStatus(`Created ${created.formKey}, but save reported: ${saveMsg}`);
      } else {
        appendLog(`• created exterior CELL ${created.formKey} at (${x}, ${y}) -> ${picked.target}`);
        setStatus(`Created ${created.formKey} at (${x}, ${y}).`);
        setLS('gizmoPatch', picked.target);
        gizmoPatchRef.current = picked.target;
        await loadGrid(formKey);
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      setStatus('Create failed: ' + msg);
      appendLog('✗ create exterior CELL: ' + msg);
    } finally {
      setBusy(false);
    }
  };

  const loadWorldspace = async (cellFormKey: string, live: () => boolean) => {
    if (!cell) return;
    try {
      const reply = JSON.parse(await cell.ResolveWorldspace(cellFormKey)) as { worldspaceFormKey?: string | null };
      if (live()) setLoadedWorldspace(reply.worldspaceFormKey ?? null);
    } catch {
      if (live()) setLoadedWorldspace(null);
    }
  };

  const runLoad = async (fetchReferences: () => Promise<string>) => {
    if (!cell) return;
    const mine = ++loadSeq.current;
    const live = () => mine === loadSeq.current;
    setBusy(true);
    setResult(null);
    setLoadedWorldspace(null);
    setGeometry({});
    setGeoProgress(null);
    setTexStats(null);
    texStatsLoggedRef.current = '';
    setSelected(null);
    setHiddenTypes(new Set());
    setHiddenRefs(new Set());
    resetVisibilityHistory();
    setStatus('Reading placed references…');
    try {
      const raw = await fetchReferences();
      if (!live()) return;
      const parsed = JSON.parse(raw) as CellReferencesResult;
      if (parsed.error) {
        setStatus(parsed.error);
        appendLog('✗ ' + parsed.error);
        return;
      }
      setResult(parsed);
      if (parsed.cellFormKey) void loadWorldspace(parsed.cellFormKey, live);
      const refs = parsed.references ?? [];
      appendLog(`• loaded ${parsed.cellEditorId || parsed.cellFormKey} -- ${refs.length} reference(s), ${parsed.withModelCount ?? 0} with a model`);



      const scolModels = refs.flatMap(r => (r.scolParts ?? []).map(p => p.modelPath));
      const uniqueModels = Array.from(new Set(
        [...refs.map(r => r.modelPath).filter((p): p is string => !!p), ...scolModels]));
      if (uniqueModels.length === 0) { setStatus('Loaded -- no placeable meshes in this cell.'); return; }

      setStatus(`Resolving + converting ${uniqueModels.length} unique mesh(es)…`);
      setGeoProgress({ done: 0, total: uniqueModels.length });
      const pollTimer = setInterval(async () => {
        try {
          const raw = await cell.GetGeometryBatchProgress();
          if (live()) setGeoProgress(JSON.parse(raw) as { done: number; total: number });
        } catch {  }
      }, 200);
      let geoRaw: string;
      try {
        geoRaw = await cell.GetGeometryBatch(JSON.stringify(uniqueModels));
      } finally {
        clearInterval(pollTimer);
        if (live()) setGeoProgress(null);
      }
      if (!live()) return;
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
      if (!live()) return;
      const msg = 'Error: ' + (e instanceof Error ? e.message : String(e));
      setStatus(msg);
      appendLog('✗ ' + msg);
    } finally {
      if (live()) setBusy(false);
    }
  };

  const pickMatch = (hit: CellSearchHit) => {
    setCellId(hit.FormKey);
    load(hit.FormKey);
  };

  const references: CellPlacedReference[] = result?.references ?? [];
  const failedModelCount = Object.values(geometry).filter(g => 'error' in g).length;
  const banner = statusBanner(status, busy);

  const typeGroups = useMemo(() => {
    const m = new Map<string, number>();
    for (const r of references) {
      const geo = r.modelPath ? geometry[r.modelPath] : (r.scolParts ?? []).map(p => geometry[p.modelPath]).find(g => g && 'shapes' in g);
      const kind = cellLayerOf(r, geo);
      m.set(kind, (m.get(kind) ?? 0) + 1);
    }
    return [...m.entries()].sort((a, b) => b[1] - a[1]);
  }, [references, geometry]);

  const toggleType = (t: string) => {
    pushUndo();
    setHiddenTypes(prev => {
      const next = new Set(prev);
      if (next.has(t)) next.delete(t); else next.add(t);
      return next;
    });
  };
  const showAllTypes = () => { pushUndo(); setHiddenTypes(new Set()); };


  const hideSelected = () => {
    if (!selected) return;
    pushUndo();
    setHiddenRefs(prev => new Set(prev).add(selected.formKey));
  };
  const unhideAll = () => { pushUndo(); setHiddenRefs(new Set()); };
  const toggleMarkers = () => toggleType(MARKER_LAYER);




  const moveEnd = useCallback(async (ref: CellPlacedReference) => {
    const c = getCell();
    const backend = getBackend();
    if (!c || !backend) return;
    let patch = gizmoPatchRef.current;
    if (!patch) {
      const picked = await askForTarget({
        title: 'Save cell edits into',
        description: 'This move, and any further moves this session, are written here.',
        confirmLabel: 'Save into', defaultTarget: LS('gizmoPatch', 'CellEdits.esp'),
      });
      if (!picked) { appendLog('x move not saved -- no patch plugin chosen.'); return; }
      patch = picked.target;
      gizmoPatchRef.current = patch;
      setLS('gizmoPatch', patch);
    }
    try {
      const msg = await c.SetPlacedReferenceTransform(
        ref.formKey, patch,
        ref.position.x, ref.position.y, ref.position.z,
        ref.rotation.x, ref.rotation.y, ref.rotation.z);
      if (/error|fail|invalid|could not|choose/i.test(msg)) { appendLog(`✗ move: ${msg}`); return; }
      const saveMsg = await backend.SavePlugin(patch, '');
      appendLog(`• moved ${ref.editorId || ref.formKey} -> ${patch}`);
      if (/error|fail/i.test(saveMsg)) appendLog('✗ save: ' + saveMsg);
    } catch (e) {
      appendLog('✗ move failed: ' + (e instanceof Error ? e.message : String(e)));
      return;
    }



    setSelected(prev => prev && prev.formKey === ref.formKey
      ? { ...prev, position: { ...ref.position }, rotation: { ...ref.rotation } }
      : prev);
  }, []);




  return (
    <div className="papyrus-overlay">
      <div className="papyrus-modal glass-panel nif-modal-wide">
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
          <div className="papyrus-form cell-sidebar">
            <div className="cell-sidebar-tabs">
              <button
                className={`cell-sidebar-tab${sidebarTab === 'info' ? ' active' : ''}`}
                onClick={() => setSidebarTab('info')}
              >
                <Info size={13} /> Info
              </button>
              <button
                className={`cell-sidebar-tab${sidebarTab === 'log' ? ' active' : ''}`}
                onClick={() => { setSidebarTab('log'); setHasUnseenError(false); }}
              >
                <ScrollText size={13} /> Log ({log.length})
                {hasUnseenError && <span className="cell-sidebar-tab-dot" title="Something failed" />}
              </button>
            </div>

            {sidebarTab === 'info' ? (
              <div className="cell-sidebar-pane">
                <div className="cell-mode-tabs">
                  <button
                    className={`cell-mode-tab${pickMode === 'cell' ? ' active' : ''}`}
                    onClick={() => { setPickMode('cell'); setLS('pickMode', 'cell'); }}
                  >By name</button>
                  <button
                    className={`cell-mode-tab${pickMode === 'grid' ? ' active' : ''}`}
                    onClick={() => { setPickMode('grid'); setLS('pickMode', 'grid'); }}
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
                          onChange={e => setWorldspace(e.target.value)}
                          placeholder={noEnv ? 'Load a modlist first…' : 'Search worldspaces…'}
                          disabled={unavailable || noEnv}
                          onKeyDown={e => { if (e.key === 'Enter') loadGrid(); }}
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
                              onClick={() => setWorldspace(h.FormKey)}
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
                        onChange={e => setGridX(e.target.value)}
                        disabled={unavailable || noEnv}
                        onKeyDown={e => { if (e.key === 'Enter') loadGrid(); }}
                      /></label>
                      <label>Y<input
                        type="number"
                        value={gridY}
                        onChange={e => setGridY(e.target.value)}
                        disabled={unavailable || noEnv}
                        onKeyDown={e => { if (e.key === 'Enter') loadGrid(); }}
                      /></label>
                    </div>
                    <button
                      className="papyrus-run cell-load-btn"
                      onClick={() => loadGrid()}
                      disabled={busy || unavailable || noEnv || !worldspace.trim()
                        || parseCellGridCoordinate(gridX) === null || parseCellGridCoordinate(gridY) === null}
                    >
                      <Play size={14} /> {busy ? 'Loading…' : 'Load Cell'}
                    </button>
                    <button
                      className="papyrus-run cell-load-btn"
                      onClick={() => void createExteriorCell()}
                      disabled={busy || unavailable || noEnv || !worldspace.trim()
                        || parseCellGridCoordinate(gridX) === null || parseCellGridCoordinate(gridY) === null}
                      title="Create a new exterior CELL via CreateRecord with worldspace FormKey, x, and y"
                    >
                      <Play size={14} /> {busy ? 'Working…' : 'Create exterior CELL'}
                    </button>
                  </>
                )}

                {pickMode === 'cell' && (
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
                )}
                {pickMode === 'cell' && (
                <button className="papyrus-run cell-load-btn" onClick={() => load()} disabled={busy || unavailable || noEnv || !cellId.trim()}>
                  <Play size={14} /> {busy ? 'Loading…' : 'Load Cell'}
                </button>
                )}

                {result && (
                  <div className="cell-stats">
                    <div className="cell-stat"><span>Cell</span><strong>{result.cellEditorId || result.cellFormKey}</strong></div>
                    {loadedWorldspace && <div className="cell-stat"><span>Worldspace</span><strong>{loadedWorldspace}</strong></div>}
                    <div className="cell-stat"><span>Interior</span><strong>{result.interior ? 'Yes' : 'No'}</strong></div>
                    <div className="cell-stat"><span>References</span><strong>{result.referenceCount ?? references.length}</strong></div>
                    <div className="cell-stat"><span>With model</span><strong>{result.withModelCount ?? 0}</strong></div>
                    {failedModelCount > 0 && (
                      <div className="cell-stat cell-stat-warn"><span>Meshes unavailable</span><strong>{failedModelCount}</strong></div>
                    )}
                  </div>
                )}

                {selected && (
                  <div className="cell-selected">
                    <div className="cell-selected-head">
                      <span>Selected</span>
                      <button className="cell-selected-clear" onClick={() => setSelected(null)} title="Deselect">
                        <X size={12} />
                      </button>
                    </div>
                    <div className="cell-selected-row"><span>Ref</span><strong>{selected.editorId || selected.formKey}</strong></div>
                    <div className="cell-selected-row"><span>Type</span><strong>{selected.baseType || selected.recordType}</strong></div>
                    <div className="cell-selected-row"><span>Base</span><strong>{selected.baseEditorId || selected.baseFormKey}</strong></div>
                    <div className="cell-selected-row"><span>Pos</span><strong>{selected.position.x.toFixed(0)}, {selected.position.y.toFixed(0)}, {selected.position.z.toFixed(0)}</strong></div>
                    <button className="cell-hide-btn" onClick={hideSelected}>
                      <EyeOff size={12} /> Hide this mesh <kbd>1</kbd>
                    </button>
                  </div>
                )}

                {texStats && (texStats.resolveFail > 0 || texStats.decodeFail > 0) && (
                  <div className="cell-hidden-bar" title={texStats.firstFailure ?? undefined}>
                    <span>
                      <AlertTriangle size={12} /> {texStats.resolveFail + texStats.decodeFail} texture
                      {texStats.resolveFail + texStats.decodeFail === 1 ? '' : 's'} missing
                      {texStats.ok > 0 ? ` (${texStats.ok} loaded)` : ''}
                    </span>
                    <div className="cell-hidden-actions">
                      <button className="papyrus-copy" onClick={() => setSidebarTab('log')}>Details</button>
                    </div>
                  </div>
                )}

                {(hiddenRefs.size > 0 || history.undo.length > 0 || history.redo.length > 0) && (
                  <div className="cell-hidden-bar">
                    <span>{hiddenRefs.size > 0 ? `${hiddenRefs.size} mesh${hiddenRefs.size === 1 ? '' : 'es'} hidden` : ''}</span>
                    <div className="cell-hidden-actions">
                      <button className="papyrus-copy" onClick={undoVisibility} disabled={history.undo.length === 0} title="Undo last hide/show">
                        Undo <kbd>Ctrl+Z</kbd>
                      </button>
                      <button className="papyrus-copy" onClick={redoVisibility} disabled={history.redo.length === 0} title="Redo">
                        Redo <kbd>Ctrl+Y</kbd>
                      </button>
                      {hiddenRefs.size > 0 && <button className="papyrus-copy" onClick={unhideAll}>Unhide all <kbd>Alt+1</kbd></button>}
                    </div>
                  </div>
                )}

                {result && typeGroups.length > 0 && (
                  <div className="cell-layers">
                    <div className="cell-layers-head">
                      <span>Layers ({typeGroups.length})</span>
                      {hiddenTypes.size > 0 && (
                        <button className="papyrus-copy" onClick={showAllTypes}>Show all</button>
                      )}
                    </div>
                    <div className="cell-layers-list">
                      {typeGroups.map(([type, count]) => {
                        const hidden = hiddenTypes.has(type);
                        return (
                          <div
                            key={type}
                            className={`cell-layer-row${hidden ? ' hidden' : ''}`}
                            onClick={() => toggleType(type)}
                            title={hidden ? 'Show' : 'Hide'}
                          >
                            {hidden ? <EyeOff size={13} /> : <Eye size={13} />}
                            <span className="cell-layer-name">{type}</span>
                            <span className="cell-layer-count">{count}</span>
                          </div>
                        );
                      })}
                    </div>
                  </div>
                )}

                <div className="cell-legend">
                  <div><span className="cell-swatch cell-swatch-mesh" /> resolved mesh</div>
                  <div><span className="cell-swatch cell-swatch-nomodel" /> no model (actor/trap)</div>
                  <div><span className="cell-swatch cell-swatch-failed" /> mesh unavailable</div>
                </div>
                <div className="cell-hotkeys">
                  <div><kbd>Click</kbd> select · <kbd>1</kbd> hide · <kbd>Alt+1</kbd> unhide all</div>
                  <div><kbd>Shift+F</kbd> focus · <kbd>T</kbd> top-down · <kbd>M</kbd> markers · <kbd>D</kbd> deselect</div>
                  <div><kbd>Ctrl+Z</kbd> undo · <kbd>Ctrl+Y</kbd> redo</div>
                  <div><kbd>G</kbd> move gizmo · <kbd>R</kbd> rotate gizmo (drag a selected reference)</div>
                </div>
                <div className="nif-view-hint">Drag to orbit · scroll to zoom · right-drag to pan</div>
              </div>
            ) : (
              <div className="cell-sidebar-pane cell-log-pane">
                <div className="papyrus-log-head">
                  <span>LOG ({log.length})</span>
                  {log.length > 0 && <button className="papyrus-copy" onClick={() => setLog([])}>Clear</button>}
                </div>
                <div className="papyrus-log-body cell-log-body">
                  {log.length === 0 ? <div className="papyrus-log-empty">No runs yet.</div>
                    : log.map((l, i) => <div key={i} className={`papyrus-log-row ${l.includes('✗') ? 'err' : 'ok'}`}>{l}</div>)}
                </div>
              </div>
            )}
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
              ? <CellViewport
                  references={references}
                  geometry={geometry}
                  loadTexture={loadTexture}
                  hiddenTypes={hiddenTypes}
                  hiddenRefs={hiddenRefs}
                  selectedKey={selected?.formKey ?? null}
                  onSelect={setSelected}
                  onHideSelected={hideSelected}
                  onUnhideAll={unhideAll}
                  onToggleMarkers={toggleMarkers}
                  onUndo={undoVisibility}
                  onRedo={redoVisibility}
                  onMoveEnd={moveEnd}
                  onTextureStats={onTextureStats}
                />
              : (
                <div className="nif-view-empty">
                  {unavailable ? 'Cell bridge unavailable.'
                    : noEnv ? 'Load a modlist first, then pick a cell from the list.'
                    : pickMode === 'grid' ? 'Pick a worldspace, enter its grid X/Y, and press Load Cell.'
                    : 'Pick a cell from the list, or paste a FormKey/EditorID and press Load Cell.'}
                </div>
              )}
          </div>
        </div>
      </div>
    </div>
  );
}
