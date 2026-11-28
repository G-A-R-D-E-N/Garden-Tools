import { useMemo, useRef, useState } from 'react';
import { Mountain, X, Play, Eye, CheckCircle2, XCircle, ListChecks, Save, Hammer } from 'lucide-react';
import { getLod, getNif } from './backend';
import NifViewport, { type NifGeo } from './NifViewport';
import {
  commitLedger, describeCandidates, describeProgress, describeVerify, levelOptions, listCandidates, loadWorlds,
  planRebakes, previewTile, tileKey, tilesFor, verifyProblems, verifyTiles, verifyWorldTiles,
  type LodCandidates, type LodPlan, type LodWorld, type LodTilePlan, type LodVerifyReport,
} from './lodClient';
import {
  adoptGenerated, describeGenerate, describeScopedProgress, generateScopedTiles, generateTiles, generateWorldTiles,
  parseProgress, type LodGenerateReport, type LodScopedProgress,
} from './lodGenerate';
import './LodPanel.css';

const LS = (k: string, d: string) => localStorage.getItem('lod.' + k) ?? d;
const setLS = (k: string, v: string) => localStorage.setItem('lod.' + k, v);

export default function LodPanel({ onClose }: { onClose: () => void }) {
  const [pluginPaths, setPluginPaths] = useState(() => LS('pluginPaths', ''));
  const [dataDir, setDataDir] = useState(() => LS('dataDir', ''));
  const [archivePaths, setArchivePaths] = useState(() => LS('archivePaths', ''));
  const [ledger, setLedger] = useState(() => LS('ledger', ''));

  const [worlds, setWorlds] = useState<LodWorld[]>([]);
  const [world, setWorld] = useState('');
  const [level, setLevel] = useState(4);
  const [plan, setPlan] = useState<LodPlan | null>(null);
  const [report, setReport] = useState<LodGenerateReport | null>(null);
  const [verify, setVerify] = useState<LodVerifyReport | null>(null);
  const [candidates, setCandidates] = useState<LodCandidates | null>(null);
  const [selected, setSelected] = useState('');
  const [geo, setGeo] = useState<NifGeo | null>(null);
  const [previewPath, setPreviewPath] = useState('');

  const [scoped, setScoped] = useState<LodScopedProgress | null>(null);
  const [scopedRunning, setScopedRunning] = useState(false);
  // Invariant: a poll that resolves after the run finished must not write a stale progress line.
  const active = useRef(false);
  const [busy, setBusy] = useState(false);
  const [banner, setBanner] = useState<{ kind: 'ok' | 'error'; text: string } | null>(null);
  const [log, setLog] = useState<string[]>([]);

  const lod = getLod();
  const nif = getNif();
  const unavailable = !lod;

  const persist = (key: string, value: string, set: (v: string) => void) => {
    set(value);
    setLS(key, value);
  };
  const appendLog = (line: string) =>
    setLog(prev => [`[${new Date().toLocaleTimeString()}] ${line}`, ...prev].slice(0, 200));

  const current = useMemo(() => worlds.find(w => w.name === world) ?? null, [worlds, world]);
  const levels = useMemo(() => levelOptions(current), [current]);
  const tiles = useMemo(() => tilesFor(plan, world, level), [plan, world, level]);
  const needing = tiles.filter(tile => tile.rebuild).length;
  const canPlan = !unavailable && !busy && pluginPaths.trim().length > 0 && world.length > 0;
  const canPreview = !unavailable && !busy && selected.length > 0 && plan !== null;
  const canCommit = !unavailable && !busy && plan !== null && plan.commit.length > 0;
  // Invariant: a write needs a pending tile and a data folder.
  const canDryRun = !unavailable && !busy && plan !== null && plan.plan.some(tile => tile.rebuild);
  const canWrite = canDryRun && dataDir.trim().length > 0;
  const canWriteWorld = canWrite && world.length > 0;
  const canVerify = !unavailable && !busy && pluginPaths.trim().length > 0;
  const canCandidates = canVerify && world.length > 0 && level > 0;

  // Invariant: every host call shares one busy/banner/log pattern, so a host failure is never silent.
  // Constraint: a refused call must not clear the banner or toggle busy, so every handler checks its guard first.
  const run = async (label: string, fn: () => Promise<void>, onFail?: () => void, clearBanner = true) => {
    setBusy(true);
    if (clearBanner) setBanner(null);
    try {
      await fn();
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      onFail?.();
      setBanner({ kind: 'error', text: message });
      appendLog(`✗ ${label} -- ${message}`);
    } finally {
      setBusy(false);
    }
  };

  const load = () =>
    !lod ? undefined : run('worlds', async () => {
      if (!lod) return;
      const found = await loadWorlds(pluginPaths, dataDir, archivePaths);
      setWorlds(found);
      const first = found.length > 0 ? found[0] : null;
      setWorld(first ? first.name : '');
      if (first) {
        const options = levelOptions(first);
        setLevel(options.length > 0 ? options[options.length - 1] : first.levelMin);
      }
      setPlan(null);
      setReport(null);
      setGeo(null);
      appendLog(`• ${found.length} worldspace(s) with LOD settings`);
      if (found.length === 0) {
        setBanner({ kind: 'error', text: 'No worldspace carries LOD settings in this load order.' });
      }
    });

  const makePlan = () =>
    !canPlan ? undefined : run('plan', async () => {
      if (!lod || !canPlan) return;
      const next = await planRebakes(pluginPaths, dataDir, archivePaths, ledger);
      setPlan(next);
      setReport(null);
      setSelected('');
      appendLog(`• planned ${next.plan.length} tile(s)`);
      if (next.plan.length === 0) setBanner({ kind: 'error', text: 'The planner found no tiles for this load order.' });
    });

  const preview = () =>
    !canPreview ? undefined : run('preview', async () => {
      if (!lod || !canPreview) return;
      const tile = tiles.find(item => tileKey(item) === selected);
      if (!tile) return;
      const path = await previewTile(pluginPaths, dataDir, archivePaths, tile.world, tile.level, tile.x, tile.y,
                                     tile.kind === 'object');
      setPreviewPath(path);
      if (nif) {
        const parsed = JSON.parse(await nif.Geo(path)) as NifGeo & { error?: string };
        if (parsed.error) throw new Error(parsed.error);
        setGeo(parsed);
      }
      appendLog(`• previewed ${tile.kind} level ${tile.level} at ${tile.x},${tile.y}`);
    }, () => setGeo(null));

  const record = () =>
    !plan ? undefined : run('ledger', async () => {
      if (!lod || !plan) return;
      const merged = await commitLedger(plan.ledger, plan.commit);
      persist('ledger', merged, setLedger);
      // Invariant: a recorded plan closes the generate actions.
      setPlan({ plan: plan.plan.map(tile => ({ ...tile, rebuild: false })), ledger: merged, commit: '' });
      setBanner({ kind: 'ok', text: 'Recorded the baked tiles in the ledger.' });
      appendLog('✓ ledger updated');
    }, undefined, false);

  const generate = (apply: boolean) =>
    !canDryRun || (apply && !canWrite) ? undefined : run('generate', async () => {
      if (!lod || !plan || !canDryRun || (apply && !canWrite)) return;
      const result = await generateTiles(pluginPaths, dataDir, archivePaths, plan.ledger, apply);
      setReport(result.report);
      if (apply) {
        persist('ledger', result.ledger, setLedger);
        setPlan(adoptGenerated(plan, result));
      }
      setBanner({ kind: result.report.failed > 0 ? 'error' : 'ok', text: describeGenerate(result.report) });
      if (!apply) appendLog('• generate dry run');
      else if (result.report.failed > 0) appendLog(`✗ generate -- ${result.report.failed} tile(s) failed`);
      else appendLog('✓ generate applied');
    }, () => setReport(null));

  const listObjectCandidates = () =>
    !canCandidates ? undefined : run('candidates', async () => {
      if (!lod || !canCandidates) return;
      const found = await listCandidates(pluginPaths, dataDir, archivePaths, world, level);
      setCandidates(found);
      setBanner({ kind: 'ok', text: describeCandidates(found) });
      appendLog(`• ${found.included} of ${found.referencesSeen} object(s) at level ${found.level}`);
    }, () => setCandidates(null));

  const generateForWorld = () =>
    !canWriteWorld ? undefined : run(`generate ${world}`, async () => {
      if (!lod || !plan || !canWriteWorld) return;
      const result = await generateWorldTiles(pluginPaths, dataDir, archivePaths, world, plan.ledger, true);
      setReport(result.report);
      persist('ledger', result.ledger, setLedger);
      setPlan(adoptGenerated(plan, result));
      setBanner({ kind: result.report.failed > 0 ? 'error' : 'ok', text: describeGenerate(result.report) });
      appendLog(result.report.failed > 0 ? `✗ generate ${world} -- ${result.report.failed} tile(s) failed`
                                         : `✓ generate ${world}`);
    }, () => setReport(null));

  // Invariant: progress is polled from the host, so a level bake reports tiles without waiting for the whole run.
  const generateLevel = async () => {
    if (!lod || !plan || !canWriteWorld) return;
    active.current = true;
    // Constraint: the previous counters must be cleared before the first poll, or a stale line shows.
    setScoped(null);
    setScopedRunning(true);
    const poll = window.setInterval(() => {
      void lod.Progress().then(text => {
        if (!active.current) return;
        const parsed = parseProgress(text);
        if (parsed.total > 0) setScoped(parsed);
      }).catch(() => undefined);
    }, 250);
    try {
      await run(`generate ${world} level ${level}`, async () => {
        const result = await generateScopedTiles(pluginPaths, dataDir, archivePaths, world, plan.ledger, true, level,
                                                 level);
        setReport(result.report);
        persist('ledger', result.ledger, setLedger);
        setPlan(adoptGenerated(plan, result));
        setBanner({ kind: result.report.failed > 0 ? 'error' : 'ok', text: describeGenerate(result.report) });
        appendLog(result.report.cancelled
          ? `• generate ${world} level ${level} cancelled`
          : `✓ generate ${world} level ${level}`);
      }, () => setReport(null));
    } finally {
      active.current = false;
      window.clearInterval(poll);
      setScoped(null);
      setScopedRunning(false);
    }
  };

  // Invariant: an empty scope judges the whole load order; a named world judges only that world.
  const judgeTiles = (scope: string) =>
    !canVerify || (scope.length > 0 && world.length === 0) ? undefined
      : run(scope.length > 0 ? `verify ${scope}` : 'verify', async () => {
      if (!lod || !canVerify || (scope.length > 0 && world.length === 0)) return;
      const found = scope.length > 0
        ? await verifyWorldTiles(pluginPaths, dataDir, archivePaths, scope, ledger)
        : await verifyTiles(pluginPaths, dataDir, archivePaths, ledger);
      setVerify(found);
      const broken = verifyProblems(found).length + found.texturesMissing + found.texturesStale;
      setBanner({ kind: broken > 0 ? 'error' : 'ok', text: describeVerify(found) });
      appendLog(scope.length > 0 ? `• verified ${scope}: ${found.tiles.length} tile(s)`
                                : `• verified ${found.tiles.length} tile(s)`);
    }, () => setVerify(null));

  return (
    <div className="lod-overlay" onClick={onClose}>
      <div className="lod-modal glass-panel" onClick={e => e.stopPropagation()}>
        <div className="lod-header">
          <span className="lod-title"><Mountain size={16} /> LODGen</span>
          <button className="lod-close" onClick={onClose} title="Close"><X size={16} /></button>
        </div>

        {unavailable && (
          <div className="lod-warn">LOD bridge not available -- run the desktop app (not the browser dev server).</div>
        )}

        <div className="lod-body">
          <div className="lod-form">
            <label className="lod-field">
              <span>Plugins (one path per line, load order)</span>
              <textarea value={pluginPaths} disabled={busy}
                        onChange={e => persist('pluginPaths', e.target.value, setPluginPaths)}
                        placeholder={'C:\\MO2\\mods\\...\\Fallout4.esm'} />
            </label>
            <label className="lod-field">
              <span>Data folder</span>
              <input value={dataDir} disabled={busy}
                     onChange={e => persist('dataDir', e.target.value, setDataDir)}
                     placeholder="C:\Games\Fallout 4\Data" />
            </label>
            <label className="lod-field">
              <span>Archives (one path per line, optional)</span>
              <textarea value={archivePaths} disabled={busy}
                        onChange={e => persist('archivePaths', e.target.value, setArchivePaths)}
                        placeholder="Fallout4 - Meshes.ba2" />
            </label>

            <div className="lod-actions">
              <button className="sidebar-action-btn" onClick={load}
                      disabled={unavailable || busy || !pluginPaths.trim()}>
                Load worlds
              </button>
            </div>

            <label className="lod-field">
              <span>Worldspace</span>
              <select value={world} disabled={busy || worlds.length === 0}
                      onChange={e => {
                        setWorld(e.target.value);
                        const next = worlds.find(w => w.name === e.target.value) ?? null;
                        const options = levelOptions(next);
                        setLevel(options.length > 0 ? options[0] : next ? next.levelMin : 4);
                        setSelected('');
                        setGeo(null);
                        setCandidates(null);
                      }}>
                {worlds.length === 0 ? <option value="">Load worlds first…</option> : null}
                {worlds.map(w => <option key={w.name} value={w.name}>{w.name}</option>)}
              </select>
            </label>
            <label className="lod-field">
              <span>Level</span>
              <select value={level} disabled={busy || levels.length === 0}
                      onChange={e => {
                        setLevel(Number(e.target.value));
                        setSelected('');
                        setGeo(null);
                        setCandidates(null);
                      }}>
                {levels.map(l => <option key={l} value={l}>{l}</option>)}
              </select>
            </label>

            <div className="lod-actions">
              <button className="sidebar-action-btn" onClick={makePlan} disabled={!canPlan}>
                <Play size={13} /> Plan rebakes
              </button>
              <button className="sidebar-action-btn" onClick={preview} disabled={!canPreview}>
                <Eye size={13} /> Preview tile
              </button>
              <button className="sidebar-action-btn" onClick={listObjectCandidates} disabled={!canCandidates}
                      title="List the references that may enter object LOD for this world and level, with a count per skip reason.">
                Candidates
              </button>
              <button className="sidebar-action-btn" onClick={() => generate(false)} disabled={!canDryRun}
                      title="Report what a bake would write, without changing any file.">
                Dry run
              </button>
              <button className="sidebar-action-btn" onClick={() => generate(true)}
                      disabled={!canWrite}
                      title={
                        'Write every tile that needs a rebake, in every worldspace and level, ' +
                        'then keep only the ledger the writer committed.'
                      }>
                <Hammer size={13} /> Generate tiles
              </button>
              <button className="sidebar-action-btn" onClick={generateForWorld}
                      disabled={!canWriteWorld}
                      title={`Write only the tiles of ${world || 'the selected worldspace'} that need a rebake.`}>
                <Hammer size={13} /> Generate world
              </button>
              <button className="sidebar-action-btn" onClick={generateLevel}
                      disabled={!canWriteWorld}
                      title={`Write only level ${level} of ${world || 'the selected worldspace'} that needs a rebake.`}>
                <Hammer size={13} /> Generate level
              </button>
              {scopedRunning && (
                <button className="sidebar-action-btn"
                        onClick={() => {
                          try {
                            void Promise.resolve(lod?.CancelScoped()).catch(() => undefined);
                          } catch {
                            // Constraint: a refused cancel must not become an unhandled rejection.
                          }
                        }}
                        title="Stop the running bake after the tile in flight.">
                  Cancel
                </button>
              )}
              <button className="sidebar-action-btn" onClick={record} disabled={!canCommit}
                      title="Only after the planned tiles were written: merge them into the ledger.">
                <Save size={13} /> Record baked tiles
              </button>
              <button className="sidebar-action-btn" onClick={() => judgeTiles('')} disabled={!canVerify}
                      title="Judge every tile and atlas map on disk against the stored ledger.">
                <CheckCircle2 size={13} /> Verify tiles
              </button>
              <button className="sidebar-action-btn" onClick={() => judgeTiles(world)}
                      disabled={!canVerify || world.length === 0}
                      title="Judge the selected worldspace's tiles and atlas maps on disk against the stored ledger.">
                <CheckCircle2 size={13} /> Verify world
              </button>
            </div>

            <div className="lod-tiles" aria-label="Planned tiles">
              {tiles.length === 0 ? (
                <div className="lod-empty">No tiles planned for this world and level.</div>
              ) : tiles.map(tile => (
                <button key={tileKey(tile)}
                        className={tileClass(tile, selected)}
                        onClick={() => setSelected(tileKey(tile))}
                        title={`${tile.inputs} input placement(s)`}>
                  <span>{tile.kind}</span>
                  <span>x {tile.x}</span>
                  <span>y {tile.y}</span>
                  <span>{tile.rebuild ? 'rebake' : 'current'}</span>
                </button>
              ))}
            </div>
          </div>

          <div className="lod-output">
            {banner && (
              <div className={`lod-banner ${banner.kind}`}>
                {banner.kind === 'ok' ? <CheckCircle2 size={14} /> : <XCircle size={14} />}
                <span>{banner.text}</span>
              </div>
            )}
            <div className="lod-progress">
              <ListChecks size={13} /> {describeProgress(tiles.length, needing)}
            </div>
            {scoped && (
              <div className="lod-progress" aria-label="Scoped progress">
                <ListChecks size={13} /> {describeScopedProgress(scoped)}
              </div>
            )}
            {report && report.objectDrops.length > 0 && (
              <div className="lod-drops" aria-label="Object drops">
                <div className="lod-drops-title">
                  {report.objectDrops.reduce((sum, drop) => sum + drop.count, 0)} object(s) skipped, by reason
                </div>
                {report.objectDrops.map(drop => (
                  <div key={drop.reason} className="lod-drop-row" title={drop.reason}>
                    <span>{drop.reason}</span>
                    <span>{drop.count}</span>
                  </div>
                ))}
              </div>
            )}
            {report && report.textures.some(texture => texture.status === 'failed') && (
              <div className="lod-drops" aria-label="Failed textures">
                <div className="lod-drops-title">Texture maps that failed</div>
                {report.textures.filter(texture => texture.status === 'failed').map(texture => (
                  <div key={`${texture.world}:${texture.map}`} className="lod-drop-row" title={texture.error}>
                    <span>{texture.world}</span>
                    <span>{texture.map}</span>
                  </div>
                ))}
              </div>
            )}
            {report && (
              <div className="lod-report" aria-label="Generate report">
                {report.tiles.length === 0 ? <div className="lod-empty">The writer produced no tiles.</div>
                  : report.tiles.map(tile => (
                    <div key={`${tile.world}:${tile.kind}:${tile.level}:${tile.x}:${tile.y}`}
                         className={`lod-report-row ${tile.status}`}
                         title={tile.error || `${tile.bytes} byte(s)`}>
                      <span>{tile.status}</span>
                      <span>{tile.world}</span>
                      <span>{tile.kind}</span>
                      <span>{tile.level}</span>
                      <span>{tile.x},{tile.y}</span>
                      <span>{tile.bytes} B</span>
                    </div>
                  ))}
              </div>
            )}
            {verify && (verifyProblems(verify).length > 0 || verify.textures.some(texture => texture.state !== 'ok')) && (
              <div className="lod-drops" aria-label="Verify rows">
                <div className="lod-drops-title">Tiles and maps that need action</div>
                {verifyProblems(verify).map(tile => (
                  <div key={`${tile.world}:${tile.kind}:${tile.level}:${tile.x}:${tile.y}`} className="lod-drop-row"
                       title={`${tile.local} / ledger ${tile.ledger}`}>
                    <span>{tile.world}</span>
                    <span>{tile.kind} {tile.level} {tile.x},{tile.y}</span>
                    <span>{tile.state}</span>
                  </div>
                ))}
                {verify.textures.filter(texture => texture.state !== 'ok').map(texture => (
                  <div key={`${texture.world}:${texture.map}`} className="lod-drop-row" title={`ledger ${texture.ledger}`}>
                    <span>{texture.world}</span>
                    <span>{texture.map}</span>
                    <span>{texture.state}</span>
                  </div>
                ))}
              </div>
            )}
            {candidates && candidates.skipped.length > 0 && (
              <div className="lod-drops" aria-label="Candidate skips">
                <div className="lod-drops-title">Object(s) that cannot enter object LOD at this level, by reason</div>
                {candidates.skipped.map(skip => (
                  <div key={skip.reason} className="lod-drop-row" title={skip.reason}>
                    <span>{skip.reason}</span>
                    <span>{skip.count}</span>
                  </div>
                ))}
              </div>
            )}
            <div className="lod-viewport">
              {geo ? (
                <NifViewport data={geo} wireframe={false} textured={false} />
              ) : (
                <div className="lod-empty">
                  Select a tile, then preview it to render the exact NIF the writer produces.
                </div>
              )}
            </div>
            {previewPath && <span className="lod-preview-path">{previewPath}</span>}
            <div className="lod-log">
              {log.length === 0 ? <div className="lod-empty">No runs yet.</div>
                : log.map((line, i) => (
                  <div key={i} className={`lod-log-row ${line.includes('✗') ? 'err' : 'ok'}`}>{line}</div>
                ))}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

/** Invariant: the row marks a pending rebake and the previewed tile. */
function tileClass(tile: LodTilePlan, selected: string): string {
  const parts = ['lod-tile'];
  if (tile.rebuild) parts.push('rebuild');
  if (selected === tileKey(tile)) parts.push('selected');
  return parts.join(' ');
}
