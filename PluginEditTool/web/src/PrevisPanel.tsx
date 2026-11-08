import { useEffect, useMemo, useState } from 'react';
import { Eye, X, AlertTriangle, CheckCircle2, XCircle } from 'lucide-react';
import { getPrevis, type PrevisCellReport, type PrevisScanResult } from './backend';
import PrevisPatchWizard from './PrevisPatchWizard';
import './PapyrusPanel.css';
import './PrevisPanel.css';

const LS = (key: string, fallback: string) => localStorage.getItem('previs.' + key) ?? fallback;

function reasons(row: PrevisCellReport): string {
  const codes = [...row.issues.map(issue => issue.code), ...row.outdated.map(issue => issue.code)];
  return codes.length === 0 ? (row.skips.length === 0 ? 'clean' : 'skipped') : codes.join(', ');
}

function matches(row: PrevisCellReport, filter: string): boolean {
  if (!filter) return true;
  const needle = filter.toLowerCase();
  return row.cell.toLowerCase().includes(needle)
    || row.plugin.toLowerCase().includes(needle)
    || reasons(row).toLowerCase().includes(needle);
}

function Findings({ rows, empty, selected, onSelect }: {
  rows: PrevisCellReport[];
  empty: string;
  selected?: string;
  onSelect?: (row: PrevisCellReport) => void;
}) {
  if (rows.length === 0) return <div className="nif-view-empty">{empty}</div>;
  return (
    <table className="previs-table">
      <thead>
        <tr>
          <th>Cell</th>
          <th>Plugin</th>
          <th>Findings</th>
        </tr>
      </thead>
      <tbody>
        {rows.map(row => (
          <tr key={row.cell} className={selected === row.cell ? 'previs-row-selected' : ''}
              onClick={() => onSelect?.(row)}>
            <td className="previs-cell">{row.cell}</td>
            <td className="previs-plugin">{row.plugin}</td>
            <td className="previs-reasons">
              {row.issues.map(issue => (
                <span key={'issue-' + issue.code + issue.detail} className="previs-chip previs-chip-broken"
                      title={issue.detail}>{issue.code}</span>
              ))}
              {row.outdated.map(issue => (
                <span key={'outdated-' + issue.code + issue.detail} className="previs-chip previs-chip-outdated"
                      title={issue.detail}>{issue.code}</span>
              ))}
              {row.skips.map(skip => (
                <span key={'skip-' + skip} className="previs-chip previs-chip-skip" title={skip}>skipped</span>
              ))}
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

export default function PrevisPanel({ onClose }: { onClose: () => void }) {
  const [dataDir, setDataDir] = useState(() => LS('dataDir', ''));
  const [filter, setFilter] = useState('');
  const [scan, setScan] = useState<PrevisScanResult | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [log, setLog] = useState('');
  const [pick, setPick] = useState<PrevisCellReport | null>(null);

  const previs = getPrevis();
  const unavailable = !previs;

  useEffect(() => {
    localStorage.setItem('previs.dataDir', dataDir);
  }, [dataDir]);

  useEffect(() => {
    if (!previs) return;
    let cancelled = false;
    (async () => {
      try {
        const folder = await previs.GetDataFolder();
        if (!cancelled && folder?.dataDir) setDataDir(current => current || folder.dataDir);
      } catch {
        /* The Scan call must report a missing Data folder itself. */
      }
    })();
    return () => { cancelled = true; };
  }, [previs]);

  const run = async () => {
    if (!previs) return;
    setBusy(true);
    setError('');
    try {
      setScan(await previs.Scan(dataDir.trim()));
    } catch (e) {
      setScan(null);
      setError('Scan failed: ' + (e instanceof Error ? e.message : String(e)));
    } finally {
      setBusy(false);
    }
  };

  const broken = useMemo(() => (scan?.broken ?? []).filter(row => matches(row, filter)), [scan, filter]);
  const outdated = useMemo(() => (scan?.outdated ?? []).filter(row => matches(row, filter)), [scan, filter]);
  const skipped = useMemo(() => (scan?.skipped ?? []).filter(row => matches(row, filter)), [scan, filter]);
  const summary = scan?.summary;
  const clean = !!summary && summary.broken === 0 && summary.skipped === 0 && summary.problems === 0;

  return (
    <div className="papyrus-overlay" onClick={onClose}>
      <div className="papyrus-modal glass-panel previs-modal" onClick={e => e.stopPropagation()}>
        <div className="papyrus-header">
          <span className="papyrus-title"><Eye size={16} /> Previs report</span>
          <button className="papyrus-close" onClick={onClose} title="Close"><X size={16} /></button>
        </div>

        {unavailable && <div className="papyrus-warn">Previs bridge not available -- run the desktop app (not the browser dev server).</div>}

        <div className="previs-toolbar">
          <input className="previs-path-input" value={dataDir} onChange={e => setDataDir(e.target.value)}
                 placeholder="Path to the game's Data folder…" onKeyDown={e => { if (e.key === 'Enter') run(); }} />
          <button className="papyrus-run" onClick={run} disabled={busy || unavailable}>
            {busy ? 'Scanning…' : 'Scan'}
          </button>
          <input className="previs-filter" value={filter} onChange={e => setFilter(e.target.value)}
                 placeholder="Filter cells, plugins or findings…" />
        </div>

        <div className="previs-body">
          <div className="previs-hint">
            Scan uses saved plugins on disk. Click a broken or outdated row, then use the patch wizard below (no Creation Kit).
          </div>
          <div className="previs-hint">
            The base game itself reports cells here; a non-zero count is not automatically a mod problem.
          </div>

          {error && <div className="papyrus-banner error"><XCircle size={15} /><span className="papyrus-banner-text">{error}</span></div>}
          {log && <div className="papyrus-banner"><span className="papyrus-banner-text">{log}</span></div>}
          {!scan && !error && <div className="nif-view-empty">{busy ? 'Reading the Data folder…' : 'Press Scan to build the previs report.'}</div>}

          {scan && summary && (
            <>
              {scan.pendingPlugins.length > 0 && (
                <div className="papyrus-warn">
                  Unsaved edits in {scan.pendingPlugins.join(', ')} are not in this scan, which reads the saved files. Save them and scan again.
                </div>
              )}
              <div className="previs-summary">
                <span className="previs-stat"><b>{summary.cells.toLocaleString()}</b> cells</span>
                <span className={`previs-stat ${clean ? 'previs-stat-ok' : 'previs-stat-bad'}`}>
                  {clean ? <CheckCircle2 size={13} /> : <AlertTriangle size={13} />}
                  <b>{summary.broken.toLocaleString()}</b> broken
                </span>
                <span className="previs-stat"><b>{summary.outdated.toLocaleString()}</b> outdated</span>
                <span className="previs-stat"><b>{summary.skipped.toLocaleString()}</b> skipped</span>
                <span className="previs-stat">{summary.tomeFiles.toLocaleString()} tomes</span>
                <span className="previs-stat">{summary.ocNifFiles.toLocaleString()} OC NIFs</span>
                <span className="previs-stat">{summary.cdxRows.toLocaleString()} CDX rows</span>
                <span className="previs-stat">{summary.csgRows.toLocaleString()} CSG rows</span>
              </div>

              {scan.truncated && <div className="previs-hint">The list is truncated; narrow it with the filter.</div>}

              {scan.problems.length > 0 && (
                <div className="previs-problems">
                  <div className="previs-section-head">Scan problems ({scan.problems.length})</div>
                  {scan.problems.map(problem => <div key={problem} className="previs-problem-row">{problem}</div>)}
                </div>
              )}

              <div className="previs-section">
                <div className="previs-section-head">Broken cells ({summary.broken.toLocaleString()})</div>
                <Findings rows={broken} selected={pick?.cell}
                  onSelect={setPick}
                  empty={summary.broken === 0 ? 'No broken cells in this load order.' : 'No broken cells match the filter.'} />
              </div>

              <div className="previs-section">
                <div className="previs-section-head">Outdated cells ({summary.outdated.toLocaleString()})</div>
                <Findings rows={outdated} selected={pick?.cell}
                  onSelect={setPick}
                  empty={summary.outdated === 0 ? 'No stale previs stamps.' : 'No outdated cells match the filter.'} />
              </div>

              <div className="previs-section">
                <div className="previs-section-head">Skipped cells ({summary.skipped.toLocaleString()})</div>
                <Findings rows={skipped} empty={summary.skipped === 0 ? 'No unmeasured cells in this load order.' : 'No skipped cells match the filter.'} />
              </div>

              <PrevisPatchWizard selectedCell={pick?.cell} selectedPlugin={pick?.plugin} onLog={setLog} />
            </>
          )}
        </div>
      </div>
    </div>
  );
}
