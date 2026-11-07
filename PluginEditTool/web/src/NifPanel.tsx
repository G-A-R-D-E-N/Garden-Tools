import { Fragment, useState, useEffect, useCallback, useMemo, useRef, type DragEvent } from 'react';
import { Box, X, FileInput, ScanSearch, CheckCircle2, Wrench, Play, FolderOpen, AlertTriangle, XCircle, Eye, Grid3x3, Image, SlidersHorizontal, Palette, Link2, Layers, Shapes, Crosshair } from 'lucide-react';
import { getNif, getMaterial, type MaterialField, type NifConnectBlock, type NifCollisionShape } from './backend';
import NifViewport, { type NifGeo, type NifConnectMarker } from './NifViewport';
import NifEditor, { type NifTree } from './NifEditor';
import NifConnectPoints, { type ConnectFocus } from './NifConnectPoints';
import NifUvEditor from './NifUvEditor';
import NifVertexColors from './NifVertexColors';
import NifGltfExport from './NifGltfExport';
import NifGltfImport from './NifGltfImport';
import MaterialEditor from './MaterialEditor';
import MaterialLink from './nifmodes/MaterialLink';
import Segments from './nifmodes/Segments';
import Collision from './nifmodes/Collision';
import { type CollisionGeo } from './util/collisionGeometry';
import { useDialogs } from './useDialogs';
import Issues, { type VerifyCheck } from './nifmodes/Issues';
import './PapyrusPanel.css';
import './NifPanel.css';

type Mode = 'view' | 'edit' | 'uv' | 'import' | 'inspect' | 'verify' | 'fix' | 'materials' | 'link' | 'segments' | 'collision';
const LS = (k: string, d: string) => localStorage.getItem('nif.' + k) ?? d;
const LSB = (k: string, d: boolean) => { const v = localStorage.getItem('nif.' + k); return v === null ? d : v === '1'; };
const setLS = (k: string, v: string | boolean) => localStorage.setItem('nif.' + k, typeof v === 'boolean' ? (v ? '1' : '0') : v);
const COLLISION_SHAPES: NifCollisionShape[] = ['none', 'box', 'hull', 'mesh'];
const storedCollision = (): NifCollisionShape => {
  const v = LS('collision', 'none');
  return v === '1' ? 'box' : COLLISION_SHAPES.find(s => s === v) ?? 'none';
};

export default function NifPanel({ onClose }: { onClose: () => void }) {
  const [mode, setMode] = useState<Mode>(() => (LS('mode', 'view') as Mode));


  const [geo, setGeo] = useState<NifGeo | null>(null);
  const [connectState, setConnectState] = useState<{ path: string; blocks: NifConnectBlock[] } | null>(null);
  const [connectFocus, setConnectFocus] = useState<(ConnectFocus & { path: string }) | null>(null);
  const [connectRev, setConnectRev] = useState(0);
  const [treeRev, setTreeRev] = useState(0);
  const [wireframe, setWireframe] = useState(() => LSB('wireframe', false));
  const [textured, setTextured] = useState(() => LSB('textured', true));
  const [texRoot, setTexRoot] = useState(() => LS('texRoot', ''));
  const [geoInfo, setGeoInfo] = useState('');
  const [geoPath, setGeoPath] = useState('');
  const [geoRev, setGeoRev] = useState(0);
  const [uvSel, setUvSel] = useState<{ block?: number; verts: number[] } | null>(null);
  const uvApi = useRef<{ applyPick: (block: number, verts: readonly number[]) => void } | null>(null);
  const onUvSelection = useCallback((block: number | undefined, verts: readonly number[]) => {
    setUvSel(prev => {
      if (prev && prev.block === block && prev.verts.length === verts.length && verts.every((v, i) => prev.verts[i] === v)) return prev;
      return { block, verts: [...verts] };
    });
  }, []);
  const onUvApi = useCallback((api: { applyPick: (block: number, verts: readonly number[]) => void } | null) => {
    uvApi.current = api;
  }, []);
  const onPickVertex = useCallback((block: number, vertex: number) => {
    uvApi.current?.applyPick(block, [vertex]);
    setUvSel({ block, verts: [vertex] });
  }, []);


  const [tree, setTree] = useState<NifTree | null>(null);
  const [treePath, setTreePath] = useState('');
  const [editorDirty, setEditorDirty] = useState(false);


  const [objPath, setObjPath] = useState(() => LS('objPath', ''));
  const [outNif, setOutNif] = useState(() => LS('outNif', ''));
  const [material, setMaterial] = useState(() => LS('material', ''));
  const [texD, setTexD] = useState(() => LS('texD', ''));
  const [texN, setTexN] = useState(() => LS('texN', ''));
  const [collision, setCollision] = useState<NifCollisionShape>(storedCollision);
  const [massKg, setMassKg] = useState(() => LS('massKg', ''));
  const [fromBlender, setFromBlender] = useState(() => LSB('fromBlender', false));
  const [gltfPath, setGltfPath] = useState(() => LS('gltfPath', ''));
  const [skeletonPath, setSkeletonPath] = useState(() => LS('skeletonPath', ''));


  const [nifPath, setNifPath] = useState(() => LS('nifPath', ''));
  const [fixOut, setFixOut] = useState(() => LS('fixOut', ''));


  const [matPath, setMatPath] = useState(() => LS('matPath', ''));
  const [matFields, setMatFields] = useState<MaterialField[] | null>(null);
  const [matLoadedPath, setMatLoadedPath] = useState('');
  const [matDirty, setMatDirty] = useState(false);
  const [matFileName, setMatFileName] = useState('');
  const [matError, setMatError] = useState('');

  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState('');
  const [report, setReport] = useState<{ path: string; checks: VerifyCheck[] } | null>(null);
  const [log, setLog] = useState<string[]>([]);
  const [dragOver, setDragOver] = useState(false);
  const [lastOutDir, setLastOutDir] = useState('');

  const nif = getNif();
  const unavailable = !nif;
  const { confirm: askConfirm } = useDialogs();
  const confirmDiscard = async (dirty: boolean, message: string) => !dirty || askConfirm({
    title: 'Discard unsaved edits?', danger: true, confirmLabel: 'Discard', message,
  });
  const changeMode = async (next: Mode) => {
    if (next === mode) return true;
    if (!await confirmDiscard(editorDirty || matDirty, 'Switching modes discards the unsaved edits in the open file.')) return false;
    setMode(next);
    return true;
  };

  useEffect(() => setLS('mode', mode), [mode]);
  useEffect(() => setLS('objPath', objPath), [objPath]);
  useEffect(() => setLS('gltfPath', gltfPath), [gltfPath]);
  useEffect(() => setLS('skeletonPath', skeletonPath), [skeletonPath]);
  useEffect(() => setLS('outNif', outNif), [outNif]);
  useEffect(() => setLS('material', material), [material]);
  useEffect(() => setLS('texD', texD), [texD]);
  useEffect(() => setLS('texN', texN), [texN]);
  useEffect(() => { setLS('collision', collision); setLS('fromBlender', fromBlender); }, [collision, fromBlender]);
  useEffect(() => setLS('massKg', massKg), [massKg]);
  useEffect(() => setLS('nifPath', nifPath), [nifPath]);
  useEffect(() => setLS('fixOut', fixOut), [fixOut]);
  useEffect(() => setLS('matPath', matPath), [matPath]);
  useEffect(() => setLS('wireframe', wireframe), [wireframe]);
  useEffect(() => setLS('textured', textured), [textured]);
  useEffect(() => setLS('texRoot', texRoot), [texRoot]);
  useEffect(() => {
    if (!nif || !treePath || mode !== 'edit') return;
    let live = true;
    void (async () => {
      try {
        const parsed = JSON.parse(await nif.ConnectPoints(treePath)) as NifConnectBlock[] | { error: string };
        if (live) setConnectState({ path: treePath, blocks: Array.isArray(parsed) ? parsed : [] });
      } catch {
        if (live) setConnectState({ path: treePath, blocks: [] });
      }
    })();
    return () => { live = false; };
  }, [nif, treePath, geo, connectRev, mode]);
  const activeFocus = connectFocus?.path === treePath ? connectFocus : null;
  const connectMarkers = useMemo<NifConnectMarker[]>(() => {
    if (mode !== 'edit' || !connectState || connectState.path !== treePath) return [];
    return connectState.blocks.flatMap(b => {
      if (b.type !== 'BSConnectPoint::Parents') return [];
      return b.points.flatMap((p, index) => {
        if (!p.world) return [];
        const active = activeFocus !== null && activeFocus.block === b.block && activeFocus.index === index;
        return [{ world: p.world, active }];
      });
    });
  }, [mode, connectState, treePath, activeFocus]);

  const loadTexture = useCallback(async (rel: string): Promise<string> => {
    const n = getNif();
    if (!n || !geoPath) return '';
    try { return await n.GetTexture(geoPath, rel, texRoot); } catch { return ''; }
  }, [geoPath, texRoot]);

  const loadCollision = useCallback(async (): Promise<CollisionGeo> => {
    const n = getNif();
    if (!n || !geoPath) throw new Error('no NIF loaded');
    const parsed = JSON.parse(await n.CollisionGeo(geoPath)) as CollisionGeo & { error?: string };
    if (parsed.error) throw new Error(parsed.error);
    return parsed;
  }, [geoPath]);

  const appendLog = (line: string) =>
    setLog(prev => [`[${new Date().toLocaleTimeString()}] ${line}`, ...prev].slice(0, 200));
  const baseName = (p: string) => p.replace(/[\\/]+$/, '').split(/[\\/]/).pop() || p;

  const browseObj = async () => { if (nif) { const p = await nif.BrowseForFile('Select an OBJ mesh', 'Wavefront OBJ (*.obj)|*.obj|All files|*.*'); if (p) setObjPath(p); } };
  const browseGltf = async () => { if (nif) { const p = await nif.BrowseForFile('Select a glTF mesh', 'glTF 2.0 (*.glb;*.gltf)|*.glb;*.gltf|All files|*.*'); if (p) setGltfPath(p); } };
  const browseSkeleton = async () => { if (nif) { const p = await nif.BrowseForFile('Select the skeleton NIF', 'NIF mesh (*.nif)|*.nif|All files|*.*'); if (p) setSkeletonPath(p); } };
  const browseNif = async () => { if (nif) { const p = await nif.BrowseForFile('Select a NIF', 'NIF mesh (*.nif)|*.nif|All files|*.*'); if (p) setNifPath(p); } };
  const browseOutNif = async () => { if (nif) { const p = await nif.BrowseForSave('Save NIF as', 'NIF mesh (*.nif)|*.nif'); if (p) setOutNif(p); } };
  const browseFixOut = async () => { if (nif) { const p = await nif.BrowseForSave('Save fixed NIF as', 'NIF mesh (*.nif)|*.nif'); if (p) setFixOut(p); } };
  const browseTexRoot = async () => { if (nif) { const p = await nif.BrowseForFolder('Select a texture root (Data or Textures folder)'); if (p) setTexRoot(p); } };
  const browseMat = async () => { const m = getMaterial(); if (m) { const p = await m.BrowseForFile('Select a material', 'FO4 material (*.bgsm;*.bgem)|*.bgsm;*.bgem|BGSM lighting material (*.bgsm)|*.bgsm|BGEM effect material (*.bgem)|*.bgem|All files|*.*'); if (p) setMatPath(p); } };



  const loadGeo = async (path?: string) => {
    const p = (path ?? nifPath).trim();
    if (!nif || !p) return;
    setBusy(true); setGeoInfo('Loading…');
    try {
      const raw = await nif.Geo(p);
      const parsed = JSON.parse(raw) as NifGeo & { error?: string };
      if (parsed.error) throw new Error(parsed.error);
      if (!parsed.shapes) throw new Error('no shapes');
      setGeo(parsed);
      setGeoPath(p);
      setGeoRev(r => r + 1);
      setUvSel(null);
      const nv = parsed.shapes.reduce((s, sh) => s + sh.verts.length / 3, 0);
      const nt = parsed.shapes.reduce((s, sh) => s + sh.tris.length / 3, 0);
      setGeoInfo(`${parsed.shapes.length} shape(s) · ${nv.toLocaleString()} verts · ${nt.toLocaleString()} tris`);
      appendLog(`• viewed ${baseName(p)}`);
    } catch (e) {
      setGeo(null);
      setGeoInfo('Could not load geometry: ' + (e instanceof Error ? e.message : String(e)));
    } finally { setBusy(false); }
  };

  const loadUv = async (path?: string) => {
    const p = (path ?? nifPath).trim();
    if (!await confirmDiscard(editorDirty, `Loading ${baseName(p)} replaces the open file and discards its unsaved UV edits.`)) return;
    await loadGeo(p);
  };

  const loadTree = async (path?: string) => {
    const p = (path ?? nifPath).trim();
    if (!nif || !p) return;
    if (!await confirmDiscard(editorDirty && p !== treePath, `Loading ${baseName(p)} replaces the open file and discards its unsaved edits.`)) return;
    setBusy(true); setGeoInfo('Loading…');
    try {
      const raw = await nif.Tree(p);
      const parsed = JSON.parse(raw) as NifTree & { error?: string };
      if (parsed.error) throw new Error(parsed.error);
      if (!parsed.blocks) throw new Error('no blocks');
      setTree(parsed);
      setTreePath(p);
      setGeoInfo(`${parsed.blocks.length} editable block(s)`);
      appendLog(`• editing ${baseName(p)}`);
      loadGeo(p);
    } catch (e) {
      setGeoInfo('Could not load tree: ' + (e instanceof Error ? e.message : String(e)));
    } finally { setBusy(false); }
  };


  const loadMaterial = async (path?: string) => {
    const p = (path ?? matPath).trim();
    const m = getMaterial();
    if (!m || !p) return;
    if (!await confirmDiscard(matDirty && p !== matLoadedPath, `Loading ${baseName(p)} replaces the open material and discards its unsaved edits.`)) return;
    setBusy(true); setMatError('');
    try {
      const raw = await m.Inspect(p);
      const parsed = JSON.parse(raw) as { fileName?: string; version?: number; fields?: MaterialField[]; error?: string };
      if (parsed.error) throw new Error(parsed.error);
      if (!parsed.fields) throw new Error('no fields');
      setMatFields(parsed.fields);
      setMatLoadedPath(p);
      setMatFileName(parsed.fileName ? `${parsed.fileName} (BGSM v${parsed.version})` : '');
      appendLog(`• loaded material ${baseName(p)}`);
    } catch (e) {
      setMatFields(null);
      setMatError('Could not load material: ' + (e instanceof Error ? e.message : String(e)));
    } finally { setBusy(false); }
  };


  const onDrop = async (e: DragEvent) => {
    e.preventDefault(); setDragOver(false);
    if (!nif) return;
    const f = e.dataTransfer.files?.[0];
    if (!f) return;
    try {
      const buf = await f.arrayBuffer();
      let bin = ''; const bytes = new Uint8Array(buf);
      for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
      const path = await nif.StageDroppedFile(f.name, btoa(bin));
      if (path.startsWith('ERR:')) { appendLog('✗ drop failed -- ' + path); return; }
      if (/\.obj$/i.test(f.name)) { if (!await changeMode('import')) return; setObjPath(path); }
      else if (/\.(glb|gltf)$/i.test(f.name)) { if (!await changeMode('import')) return; setGltfPath(path); }
      else if (/\.bg(sm|em)$/i.test(f.name)) { setMatPath(path); loadMaterial(path); }
      else if (/\.nif$/i.test(f.name)) {
        setNifPath(path);
        if (mode === 'view') loadGeo(path);
        else if (mode === 'uv') loadUv(path);
        else if (mode === 'edit') loadTree(path);
        else if (mode === 'import') setMode('inspect');
      }
      appendLog('• dropped ' + f.name);
    } catch (err) {
      appendLog('✗ drop failed -- ' + (err instanceof Error ? err.message : String(err)));
    }
  };

  const onEditorSaved = () => { setTreeRev(r => r + 1); loadTree(treePath); };
  const onGeoSaved = () => { loadGeo(geoPath); };

  const requestClose = async () => {
    if (!await confirmDiscard(editorDirty || matDirty, 'Closing the NIF panel discards the unsaved edits in the open file.')) return;
    onClose();
  };

  const verifyPath = async (path: string) => {
    if (!nif) return '';
    const raw = await nif.Verify(path);
    const reply = parseVerify(raw);
    setReport(reply ? { path, checks: reply.checks } : null);
    return reply ? reply.text : raw;
  };

  const onIssuesWritten = async (outPath: string) => {
    setLastOutDir(outPath);
    setResult(`RESULT: fix OK (in-process)\nSaved: ${outPath}`);
    await verifyPath(outPath);
  };

  const run = async () => {
    if (mode === 'materials') { await loadMaterial(); return; }
    if (!nif) return;
    if (mode === 'view') { await loadGeo(); return; }
    if (mode === 'uv') { await loadUv(); return; }
    if (mode === 'edit') { await loadTree(); return; }
    setBusy(true); setResult('Working…'); setLastOutDir('');
    try {
      let out = '';
      if (mode === 'import') {
        if (!objPath.trim() || !outNif.trim()) { setResult('Pick an OBJ and an output .nif path.'); setBusy(false); return; }
        const mass = collision === 'hull' && massKg.trim() !== '' ? Number(massKg) : 0;
        if (!Number.isFinite(mass) || mass < 0) { setResult('Mass must be a number of kilograms, or blank for static.'); setBusy(false); return; }
        out = await nif.Import(objPath, outNif, material, texD, texN, collision, fromBlender, mass);
        if (/RESULT: import OK/.test(out)) setLastOutDir(outNif);
        appendLog(`${/OK/.test(out) ? '✓' : '✗'} import ${baseName(objPath)} → ${baseName(outNif)}`);
      } else if (mode === 'inspect') {
        if (!nifPath.trim()) { setResult('Pick a NIF to inspect.'); setBusy(false); return; }
        out = await nif.Inspect(nifPath);
        appendLog(`• inspected ${baseName(nifPath)}`);
      } else if (mode === 'verify') {
        if (!nifPath.trim()) { setResult('Pick a NIF to verify.'); setBusy(false); return; }
        out = await verifyPath(nifPath);
        const r = out.split('\n').find(l => l.startsWith('RESULT:')) ?? '';
        const failed = /^RESULT: \d+ checks, (\d+) failed/.exec(r);
        appendLog(`${failed?.[1] === '0' ? '✓' : '✗'} verify ${baseName(nifPath)} -- ${r.replace('RESULT:', '').trim()}`);
      } else {
        if (!nifPath.trim() || !fixOut.trim()) { setResult('Pick a NIF and an output path.'); setBusy(false); return; }
        out = await nif.Fix(nifPath, fixOut);
        if (/RESULT: fix OK/.test(out)) { setLastOutDir(fixOut); await verifyPath(fixOut); }
        appendLog(`${/OK/.test(out) ? '✓' : '✗'} fix ${baseName(nifPath)} → ${baseName(fixOut)}`);
      }
      setResult(out || '(no output)');
    } catch (e) {
      const msg = 'Error: ' + (e instanceof Error ? e.message : String(e));
      setResult(msg); appendLog(`✗ ${mode} -- ${msg}`);
    } finally { setBusy(false); }
  };

  const openOut = async () => { if (nif && lastOutDir) await nif.OpenFolder(lastOutDir); };

  const matHost = getMaterial();
  const matUnavailable = !matHost;

  const banner = result && result !== 'Working…' ? makeBanner(result) : null;
  const primaryPath = mode === 'import' ? objPath : mode === 'materials' ? matPath : nifPath;
  const runLabel = mode === 'view' ? 'Load & View' : mode === 'edit' ? 'Load & Edit' : mode === 'uv' ? 'Load & UV'
    : mode === 'materials' ? 'Load Material'
    : mode === 'import' ? 'Author NIF' : mode === 'inspect' ? 'Inspect' : mode === 'verify' ? 'Verify' : 'Fix';
  const wide = mode === 'view' || mode === 'edit' || mode === 'uv' || mode === 'materials' || mode === 'link'
    || mode === 'segments' || mode === 'collision';

  return (
    <div className="papyrus-overlay" onClick={requestClose}>
      <div className={`papyrus-modal glass-panel ${wide ? 'nif-modal-wide' : ''}`} onClick={e => e.stopPropagation()}>
        <div className="papyrus-header">
          <span className="papyrus-title"><Box size={16} /> NIF</span>
          <div className="papyrus-modes">
            <button className={`papyrus-mode ${mode === 'view' ? 'active' : ''}`} onClick={() => changeMode('view')}><Eye size={14} /> View</button>
            <button className={`papyrus-mode ${mode === 'edit' ? 'active' : ''}`} onClick={() => changeMode('edit')}><SlidersHorizontal size={14} /> Edit</button>
            <button className={`papyrus-mode ${mode === 'uv' ? 'active' : ''}`} onClick={() => changeMode('uv')}><Crosshair size={14} /> UV</button>
            <button className={`papyrus-mode ${mode === 'materials' ? 'active' : ''}`} onClick={() => changeMode('materials')}><Palette size={14} /> Materials</button>
            <button className={`papyrus-mode ${mode === 'link' ? 'active' : ''}`} onClick={() => changeMode('link')}><Link2 size={14} /> Material link</button>
            <button className={`papyrus-mode ${mode === 'import' ? 'active' : ''}`} onClick={() => changeMode('import')}><FileInput size={14} /> Import</button>
            <button className={`papyrus-mode ${mode === 'inspect' ? 'active' : ''}`} onClick={() => changeMode('inspect')}><ScanSearch size={14} /> Inspect</button>
            <button className={`papyrus-mode ${mode === 'verify' ? 'active' : ''}`} onClick={() => changeMode('verify')}><CheckCircle2 size={14} /> Verify</button>
            <button className={`papyrus-mode ${mode === 'fix' ? 'active' : ''}`} onClick={() => changeMode('fix')}><Wrench size={14} /> Fix</button>
            <button className={`papyrus-mode ${mode === 'segments' ? 'active' : ''}`} onClick={() => changeMode('segments')}><Layers size={14} /> Segments</button>
            <button className={`papyrus-mode ${mode === 'collision' ? 'active' : ''}`} onClick={() => changeMode('collision')}><Shapes size={14} /> Collision</button>
          </div>
          <button className="papyrus-close" onClick={requestClose} title="Close"><X size={16} /></button>
        </div>

        {mode !== 'materials' && unavailable && <div className="papyrus-warn">NIF bridge not available -- run the desktop app (not the browser dev server).</div>}
        {mode === 'materials' && matUnavailable && <div className="papyrus-warn">Material bridge not available -- run the desktop app (not the browser dev server).</div>}

        <div className={`papyrus-body ${mode === 'edit' || mode === 'materials' ? 'nif-edit-body' : ''} ${mode === 'collision' ? 'nif-collision-body' : ''}`}>
          {mode !== 'collision' && <div className={`papyrus-form ${mode === 'edit' || mode === 'materials' ? 'nif-edit-sidebar' : ''}`}>
            {mode === 'link' ? null : mode === 'materials' ? (
              <div className="nif-edit-picker">
                <div className={`papyrus-drop ${dragOver ? 'over' : ''}`}
                     onDragOver={e => { e.preventDefault(); setDragOver(true); }}
                     onDragLeave={() => setDragOver(false)} onDrop={onDrop}>
                  <input value={matPath} onChange={e => setMatPath(e.target.value)} placeholder="Path, or drag a .bgsm/.bgem here…" />
                </div>
                <div className="papyrus-input-row">
                  <button className="sidebar-action-btn" onClick={browseMat} disabled={matUnavailable}>Browse…</button>
                  <button className="papyrus-run nif-load-btn" onClick={() => loadMaterial()} disabled={busy || matUnavailable || !matPath.trim()}>
                    <Play size={13} /> {busy ? 'Loading…' : 'Load Material'}
                  </button>
                </div>
                {matFileName && <span className="nif-geo-info">{matFileName}</span>}
              </div>
            ) : mode === 'edit' ? (
              <>
                <div className="nif-edit-picker">
                  <div className={`papyrus-drop ${dragOver ? 'over' : ''}`}
                       onDragOver={e => { e.preventDefault(); setDragOver(true); }}
                       onDragLeave={() => setDragOver(false)} onDrop={onDrop}>
                    <input value={nifPath} onChange={e => setNifPath(e.target.value)} placeholder="Path, or drag a .nif here…" />
                  </div>
                  <div className="papyrus-input-row">
                    <button className="sidebar-action-btn" onClick={browseNif} disabled={unavailable}>NIF…</button>
                    <button className="papyrus-run nif-load-btn" onClick={() => loadTree()} disabled={busy || unavailable || !nifPath.trim()}>
                      <Play size={13} /> {busy ? 'Loading…' : 'Load & Edit'}
                    </button>
                  </div>
                  <div className="nif-edit-toggles">
                    <label><input type="checkbox" checked={textured} onChange={e => setTextured(e.target.checked)} /> <Image size={12} /> Textured</label>
                    <label><input type="checkbox" checked={wireframe} onChange={e => setWireframe(e.target.checked)} /> <Grid3x3 size={12} /> Wireframe</label>
                    {geoInfo && <span className="nif-geo-info">{geoInfo}</span>}
                  </div>
                </div>
                <div className="nif-edit-editorwrap">
                  {tree && nif ? <Fragment key={treePath}>
                    <NifEditor tree={tree} nif={nif} nifPath={treePath} onSaved={onEditorSaved} onDirtyChange={setEditorDirty} appendLog={appendLog} />
                    <NifConnectPoints nif={nif} nifPath={treePath} dirty={editorDirty} reloadKey={treeRev}
                      onSaved={() => setConnectRev(r => r + 1)}
                      onFocusPoint={focus => setConnectFocus(focus && { ...focus, path: treePath })} />
                  </Fragment>
                    : <div className="nif-view-empty">{unavailable ? 'NIF bridge unavailable.' : 'Pick or drag a .nif, then Load & Edit.'}</div>}
                </div>
              </>
            ) : mode === 'view' || mode === 'uv' ? (
              <>
                <label className="papyrus-field">
                  <span>{mode === 'view' ? 'NIF to view' : 'NIF to edit UVs'}</span>
                  <div className={`papyrus-drop ${dragOver ? 'over' : ''}`}
                       onDragOver={e => { e.preventDefault(); setDragOver(true); }}
                       onDragLeave={() => setDragOver(false)} onDrop={onDrop}>
                    <input value={nifPath} onChange={e => setNifPath(e.target.value)} placeholder="Path, or drag a .nif here…" />
                  </div>
                  <div className="papyrus-input-row">
                    <button className="sidebar-action-btn" onClick={browseNif} disabled={unavailable}>NIF…</button>
                  </div>
                </label>
                {mode === 'view' && <div className="papyrus-opts">
                  <label><input type="checkbox" checked={textured} onChange={e => setTextured(e.target.checked)} /> <Image size={12} /> Textured</label>
                  <label><input type="checkbox" checked={wireframe} onChange={e => setWireframe(e.target.checked)} /> <Grid3x3 size={12} /> Wireframe</label>
                </div>}
                <label className="papyrus-field">
                  <span>Texture folder (optional -- a Data or Textures root)</span>
                  <div className="papyrus-input-row">
                    <input value={texRoot} onChange={e => setTexRoot(e.target.value)} placeholder="(auto: resolves against the NIF's Data\ folder)" />
                    <button className="sidebar-action-btn" onClick={browseTexRoot} disabled={unavailable}>Folder…</button>
                  </div>
                </label>
                {geoInfo && <div className="nif-geo-info">{geoInfo}</div>}
                {mode === 'view' && <div className="nif-view-hint">Drag to orbit · scroll to zoom · right-drag to pan</div>}
              </>
            ) : mode === 'import' ? (
              <>
                <label className="papyrus-field">
                  <span>Source mesh (.obj from Blender)</span>
                  <div className={`papyrus-drop ${dragOver ? 'over' : ''}`}
                       onDragOver={e => { e.preventDefault(); setDragOver(true); }}
                       onDragLeave={() => setDragOver(false)} onDrop={onDrop}>
                    <input value={objPath} onChange={e => setObjPath(e.target.value)} placeholder="Path, or drag a .obj here…" />
                  </div>
                  <div className="papyrus-input-row">
                    <button className="sidebar-action-btn" onClick={browseObj} disabled={unavailable}>OBJ…</button>
                  </div>
                </label>
                <label className="papyrus-field">
                  <span>Source glTF (.glb or .gltf, optional)</span>
                  <div className={`papyrus-drop ${dragOver ? 'over' : ''}`}
                       onDragOver={e => { e.preventDefault(); setDragOver(true); }}
                       onDragLeave={() => setDragOver(false)} onDrop={onDrop}>
                    <input value={gltfPath} onChange={e => setGltfPath(e.target.value)} placeholder="Path, or drag a .glb or .gltf here…" />
                  </div>
                  <div className="papyrus-input-row">
                    <button className="sidebar-action-btn" onClick={browseGltf} disabled={unavailable}>glTF…</button>
                  </div>
                </label>
                <label className="papyrus-field">
                  <span>Skeleton NIF (needed when the glTF has skinned meshes)</span>
                  <div className="papyrus-input-row">
                    <input value={skeletonPath} onChange={e => setSkeletonPath(e.target.value)} placeholder="…\skeleton.nif" />
                    <button className="sidebar-action-btn" onClick={browseSkeleton} disabled={unavailable}>NIF…</button>
                  </div>
                </label>
                <label className="papyrus-field">
                  <span>Output NIF</span>
                  <div className="papyrus-input-row">
                    <input value={outNif} onChange={e => setOutNif(e.target.value)} placeholder="…\meshes\mymod\thing.nif" />
                    <button className="sidebar-action-btn" onClick={browseOutNif} disabled={unavailable}>Save…</button>
                  </div>
                </label>
                <label className="papyrus-field">
                  <span>Material (.bgsm, optional)</span>
                  <input value={material} onChange={e => setMaterial(e.target.value)} placeholder="Materials\mymod\thing.bgsm" />
                </label>
                <label className="papyrus-field">
                  <span>Diffuse texture (slot 0, optional)</span>
                  <input value={texD} onChange={e => setTexD(e.target.value)} placeholder="Textures\mymod\thing_d.dds" />
                </label>
                <label className="papyrus-field">
                  <span>Normal texture (slot 1, optional)</span>
                  <input value={texN} onChange={e => setTexN(e.target.value)} placeholder="Textures\mymod\thing_n.dds" />
                </label>
                <label className="papyrus-field">
                  <span>Collision</span>
                  <select value={collision} onChange={e => setCollision(e.target.value as NifCollisionShape)}>
                    <option value="none">None</option>
                    <option value="box">Box (static)</option>
                    <option value="hull">Convex hull</option>
                    <option value="mesh">Mesh (static only)</option>
                  </select>
                </label>
                {collision === 'hull' && (
                  <label className="papyrus-field">
                    <span>Mass in kg (blank = static)</span>
                    <input value={massKg} onChange={e => setMassKg(e.target.value)} placeholder="e.g. 12" />
                  </label>
                )}
                <div className="papyrus-opts">
                  <label><input type="checkbox" checked={fromBlender} onChange={e => setFromBlender(e.target.checked)} /> Convert Blender Y-up → NIF Z-up</label>
                </div>
                <div className="nif-view-hint">Authored collision has not been tested in the game until the owner confirms it. Mesh collision is static only.</div>
              </>
            ) : mode === 'fix' ? (
              <>
                <label className="papyrus-field">
                  <span>NIF to repair</span>
                  <div className={`papyrus-drop ${dragOver ? 'over' : ''}`}
                       onDragOver={e => { e.preventDefault(); setDragOver(true); }}
                       onDragLeave={() => setDragOver(false)} onDrop={onDrop}>
                    <input value={nifPath} onChange={e => setNifPath(e.target.value)} placeholder="Path, or drag a .nif here…" />
                  </div>
                  <div className="papyrus-input-row">
                    <button className="sidebar-action-btn" onClick={browseNif} disabled={unavailable}>NIF…</button>
                  </div>
                </label>
                <label className="papyrus-field">
                  <span>Output NIF (may equal the input to overwrite)</span>
                  <div className="papyrus-input-row">
                    <input value={fixOut} onChange={e => setFixOut(e.target.value)} placeholder="…\thing_fixed.nif" />
                    <button className="sidebar-action-btn" onClick={browseFixOut} disabled={unavailable}>Save…</button>
                  </div>
                </label>
              </>
            ) : (
              <label className="papyrus-field">
                <span>{mode === 'inspect' ? 'NIF to inspect' : mode === 'segments' ? 'NIF with armor segments' : 'NIF to verify'}</span>
                <div className={`papyrus-drop ${dragOver ? 'over' : ''}`}
                     onDragOver={e => { e.preventDefault(); setDragOver(true); }}
                     onDragLeave={() => setDragOver(false)} onDrop={onDrop}>
                  <input value={nifPath} onChange={e => setNifPath(e.target.value)} placeholder="Path, or drag a .nif here…" />
                </div>
                <div className="papyrus-input-row">
                  <button className="sidebar-action-btn" onClick={browseNif} disabled={unavailable}>NIF…</button>
                </div>
                {mode === 'inspect' && <NifGltfExport nifPath={nifPath} onResult={setResult} appendLog={appendLog} />}
              </label>
            )}

            {mode !== 'edit' && mode !== 'materials' && mode !== 'link' && mode !== 'segments' && (
              <button className="papyrus-run" onClick={run} disabled={busy || unavailable || !primaryPath.trim()}>
                <Play size={14} /> {busy ? 'Working…' : runLabel}
              </button>
            )}
            {mode === 'import' && (
              <NifGltfImport gltfPath={gltfPath} outNif={outNif} skeleton={skeletonPath} material={material}
                texDiffuse={texD} texNormal={texN} collision={collision !== 'none'} onResult={setResult} appendLog={appendLog} />
            )}
          </div>}

          {mode === 'view' ? (
            <div className="papyrus-output nif-view-output">
              {geo ? <NifViewport data={geo} wireframe={wireframe} textured={textured} loadTexture={loadTexture} loadCollision={loadCollision} />
                : <div className="nif-view-empty">{unavailable ? 'NIF bridge unavailable.' : 'Pick or drag a .nif, then Load & View.'}</div>}
            </div>
          ) : mode === 'uv' ? (
            <div className="papyrus-output nif-uv-split">
              {geo && nif ? (<>
                <NifViewport data={geo} wireframe={wireframe} textured={textured} loadTexture={loadTexture}
                  highlight={uvSel} onPickVertex={onPickVertex} />
                <NifUvEditor key={geoRev} geo={geo} nif={nif} nifPath={geoPath} texRoot={texRoot} onSaved={onGeoSaved}
                  onDirtyChange={setEditorDirty} appendLog={appendLog} onSelectionChange={onUvSelection} onApi={onUvApi} />
                <NifVertexColors nif={nif} nifPath={geoPath} selection={uvSel} disabled={editorDirty}
                  onSaved={onGeoSaved} appendLog={appendLog} />
              </>) : <div className="nif-view-empty">{unavailable ? 'NIF bridge unavailable.' : 'Pick or drag a .nif, then Load & UV.'}</div>}
            </div>
          ) : mode === 'edit' ? (
            <div className="papyrus-output nif-view-output">
              {geo ? <NifViewport data={geo} wireframe={wireframe} textured={textured} loadTexture={loadTexture}
                loadCollision={loadCollision} connectPoints={connectMarkers} />
                : <div className="nif-view-empty">{unavailable ? 'NIF bridge unavailable.' : 'Load a NIF to preview it here.'}</div>}
            </div>
          ) : mode === 'collision' ? (
            <div className="papyrus-output nif-view-output">
              <Collision nif={nif} appendLog={appendLog} />
            </div>
          ) : mode === 'materials' ? (
            <div className="papyrus-output nif-view-output">
              {matFields && matHost ? (
                <MaterialEditor key={matLoadedPath} fields={matFields} path={matLoadedPath} material={matHost}
                  onSaved={() => loadMaterial(matLoadedPath)} onDirtyChange={setMatDirty} appendLog={appendLog} />
              ) : (
                <div className="nif-view-empty">
                  {matUnavailable ? 'Material bridge unavailable.' : matError || 'Pick or drag a .bgsm or .bgem, then Load Material.'}
                </div>
              )}
            </div>
          ) : mode === 'link' ? (
            <div className="papyrus-output nif-view-output">
              {nif ? <MaterialLink nif={nif} nifPath={nifPath} appendLog={appendLog} /> : <div className="nif-view-empty">NIF bridge unavailable.</div>}
            </div>
          ) : mode === 'segments' ? (
            <div className="papyrus-output nif-view-output">
              <Segments nif={nif} nifPath={nifPath} appendLog={appendLog} />
            </div>
          ) : (
          <div className="papyrus-output">
            {banner && (
              <div className={`papyrus-banner ${banner.kind}`}>
                {banner.kind === 'ok' ? <CheckCircle2 size={15} /> : banner.kind === 'warn' ? <AlertTriangle size={15} /> : <XCircle size={15} />}
                <span className="papyrus-banner-text">{banner.text}</span>
                {lastOutDir && <button className="papyrus-openfolder" onClick={openOut} title={lastOutDir}><FolderOpen size={13} /> Open folder</button>}
              </div>
            )}
            <div className="papyrus-output-head">
              <span>OUTPUT -- {mode}</span>
              {result && <button className="papyrus-copy" onClick={() => navigator.clipboard.writeText(result)}>Copy</button>}
            </div>
            {(mode === 'verify' || mode === 'fix') && report && nif
              ? <Issues key={report.path} nif={nif} path={report.path} checks={report.checks}
                  onWritten={onIssuesWritten} appendLog={appendLog} />
              : <pre className="papyrus-output-body nif-output">{result || 'Pick a file and run.'}</pre>}
            <div className="papyrus-log-head">
              <span>LOG ({log.length})</span>
              {log.length > 0 && <button className="papyrus-copy" onClick={() => setLog([])}>Clear</button>}
            </div>
            <div className="papyrus-log-body">
              {log.length === 0 ? <div className="papyrus-log-empty">No runs yet.</div>
                : log.map((l, i) => <div key={i} className={`papyrus-log-row ${l.includes('✗') ? 'err' : 'ok'}`}>{l}</div>)}
            </div>
          </div>
          )}
        </div>
      </div>
    </div>
  );
}

function parseVerify(raw: string): { text: string; checks: VerifyCheck[] } | null {
  try {
    const parsed = JSON.parse(raw) as { text?: unknown; checks?: unknown };
    return typeof parsed.text === 'string' && Array.isArray(parsed.checks)
      ? { text: parsed.text, checks: parsed.checks as VerifyCheck[] }
      : null;
  } catch {
    return null;
  }
}

function makeBanner(text: string): { kind: 'ok' | 'warn' | 'error'; text: string } {
  const result = text.split('\n').find(l => l.startsWith('RESULT:'));
  if (result) {
    const failedMatch = result.match(/(\d+)\s+failed/);
    const failed = failedMatch ? failedMatch[1] : null;
    if (failed !== null) return { kind: failed !== '0' ? 'warn' : 'ok', text: result.replace('RESULT:', '').trim() };
    if (/FAILED/i.test(result)) return { kind: 'error', text: result.replace('RESULT:', '').trim() };
    return { kind: 'ok', text: result.replace('RESULT:', '').trim() };
  }
  if (/^\s*\{/.test(text)) return { kind: 'ok', text: 'Inspected -- JSON below' };
  if (/error|cannot|not found|FAILED/i.test(text)) return { kind: 'error', text: 'Failed -- see output' };
  return { kind: 'ok', text: 'Done' };
}
