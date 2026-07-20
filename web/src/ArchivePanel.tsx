import { useState, useEffect } from 'react';
import { Archive as ArchiveIcon, X, FolderOpen, Download, DownloadCloud, CheckCircle2, XCircle } from 'lucide-react';
import { getArchive, type ArchiveEntry } from './backend';
import './PapyrusPanel.css';
import './ArchivePanel.css';

const LS = (k: string, d: string) => localStorage.getItem('archive.' + k) ?? d;
const setLS = (k: string, v: string) => localStorage.setItem('archive.' + k, v);

/** Browse and pull files out of a FO4 BA2/BSA archive by hand -- the GUI counterpart of the
 * archive_list/archive_extract/archive_extract_all MCP tools. Read-only against the archive itself;
 * extraction writes only to the destination the user picks. */
export default function ArchivePanel({ onClose }: { onClose: () => void }) {
  const [archivePath, setArchivePath] = useState(() => LS('archivePath', ''));
  const [filter, setFilter] = useState('');
  const [entries, setEntries] = useState<ArchiveEntry[] | null>(null);
  const [meta, setMeta] = useState<{ totalCount: number; shownCount: number; truncated: boolean } | null>(null);
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState('');
  const [error, setError] = useState('');
  const [lastOutDir, setLastOutDir] = useState('');

  const archive = getArchive();
  const unavailable = !archive;

  useEffect(() => setLS('archivePath', archivePath), [archivePath]);

  const browseArchive = async () => {
    if (!archive) return;
    const p = await archive.BrowseForFile('Select a BA2/BSA archive', 'Bethesda archive (*.ba2;*.bsa)|*.ba2;*.bsa|All files|*.*');
    if (p) { setArchivePath(p); load(p); }
  };

  const load = async (path?: string, f?: string) => {
    const p = (path ?? archivePath).trim();
    if (!archive || !p) return;
    setBusy(true); setError(''); setResult('');
    try {
      const raw = await archive.List(p, f ?? filter, 2000);
      const parsed = JSON.parse(raw) as { archiveName?: string; totalCount?: number; shownCount?: number; truncated?: boolean; entries?: ArchiveEntry[]; error?: string };
      if (parsed.error) throw new Error(parsed.error);
      if (!parsed.entries) throw new Error('no entries');
      setEntries(parsed.entries);
      setMeta({ totalCount: parsed.totalCount ?? parsed.entries.length, shownCount: parsed.shownCount ?? parsed.entries.length, truncated: !!parsed.truncated });
      setSelected(new Set());
    } catch (e) {
      setEntries(null);
      setError('Could not read archive: ' + (e instanceof Error ? e.message : String(e)));
    } finally { setBusy(false); }
  };

  const toggle = (path: string) => {
    setSelected(prev => {
      const next = new Set(prev);
      if (next.has(path)) next.delete(path); else next.add(path);
      return next;
    });
  };
  const toggleAll = () => {
    if (!entries) return;
    setSelected(prev => (prev.size === entries.length ? new Set() : new Set(entries.map(e => e.path))));
  };

  const extractSelected = async () => {
    if (!archive || !archivePath.trim() || selected.size === 0) return;
    const outDir = await archive.BrowseForFolder('Extract selected files to');
    if (!outDir) return;
    setBusy(true);
    try {
      const res = await archive.ExtractSelected(archivePath, JSON.stringify(Array.from(selected)), outDir);
      setResult(res);
      if (/^Extracted/.test(res)) setLastOutDir(outDir);
    } catch (e) {
      setResult('Error: ' + (e instanceof Error ? e.message : String(e)));
    } finally { setBusy(false); }
  };

  const extractAll = async () => {
    if (!archive || !archivePath.trim()) return;
    const outDir = await archive.BrowseForFolder('Extract all (matching) files to');
    if (!outDir) return;
    setBusy(true);
    try {
      const res = await archive.ExtractAll(archivePath, outDir, filter, 5000);
      setResult(res);
      if (/^Extracted/.test(res)) setLastOutDir(outDir);
    } catch (e) {
      setResult('Error: ' + (e instanceof Error ? e.message : String(e)));
    } finally { setBusy(false); }
  };

  const openOut = async () => { if (archive && lastOutDir) await archive.OpenFolder(lastOutDir); };

  return (
    <div className="papyrus-overlay" onClick={onClose}>
      <div className="papyrus-modal glass-panel archive-modal" onClick={e => e.stopPropagation()}>
        <div className="papyrus-header">
          <span className="papyrus-title"><ArchiveIcon size={16} /> Archive</span>
          <button className="papyrus-close" onClick={onClose} title="Close"><X size={16} /></button>
        </div>

        {unavailable && <div className="papyrus-warn">Archive bridge not available — run the desktop app (not the browser dev server).</div>}

        <div className="archive-toolbar">
          <input className="archive-path-input" value={archivePath} onChange={e => setArchivePath(e.target.value)}
                 placeholder="Path to a .ba2/.bsa…" onKeyDown={e => { if (e.key === 'Enter') load(); }} />
          <button className="sidebar-action-btn" onClick={browseArchive} disabled={unavailable}>Browse…</button>
          <input className="archive-filter" value={filter} onChange={e => setFilter(e.target.value)}
                 placeholder="Filter (path substring)…" onKeyDown={e => { if (e.key === 'Enter') load(); }} />
          <button className="papyrus-run" onClick={() => load()} disabled={busy || unavailable || !archivePath.trim()}>
            {busy ? 'Loading…' : 'List'}
          </button>
        </div>

        <div className="archive-body">
          {!entries ? (
            <div className="nif-view-empty">{unavailable ? 'Archive bridge unavailable.' : error || 'Pick a .ba2/.bsa, then List.'}</div>
          ) : (
            <>
              {meta && (
                <div className="archive-meta">
                  {meta.shownCount.toLocaleString()} of {meta.totalCount.toLocaleString()} entr{meta.totalCount === 1 ? 'y' : 'ies'} shown
                  {meta.truncated && <span className="archive-truncated"> — narrow with a filter to see the rest</span>}
                </div>
              )}
              <table className="archive-table">
                <thead>
                  <tr>
                    <th><input type="checkbox" checked={entries.length > 0 && selected.size === entries.length} onChange={toggleAll} /></th>
                    <th>Path</th>
                    <th>Size</th>
                  </tr>
                </thead>
                <tbody>
                  {entries.map(e => (
                    <tr key={e.path}>
                      <td><input type="checkbox" checked={selected.has(e.path)} onChange={() => toggle(e.path)} /></td>
                      <td className="archive-path">{e.path}</td>
                      <td className="archive-size">{e.size.toLocaleString()} B</td>
                    </tr>
                  ))}
                </tbody>
              </table>

              <div className="archive-actions">
                <button className="sidebar-action-btn" disabled={selected.size === 0 || busy} onClick={extractSelected}>
                  <Download size={13} /> Extract Selected ({selected.size})
                </button>
                <button className="sidebar-action-btn" disabled={busy} onClick={extractAll}>
                  <DownloadCloud size={13} /> Extract All{filter ? ' (matching filter)' : ''}
                </button>
              </div>
            </>
          )}

          {result && (
            <div className={`papyrus-banner ${/^Extracted/.test(result) ? 'ok' : 'error'}`}>
              {/^Extracted/.test(result) ? <CheckCircle2 size={15} /> : <XCircle size={15} />}
              <span className="papyrus-banner-text">{result}</span>
              {lastOutDir && <button className="papyrus-openfolder" onClick={openOut} title={lastOutDir}><FolderOpen size={13} /> Open folder</button>}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
