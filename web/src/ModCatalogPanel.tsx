import { useState, useEffect } from 'react';
import { FolderSearch, X, FolderOpen, Play } from 'lucide-react';
import { getModCatalog } from './backend';
import './PapyrusPanel.css';

const LS = (k: string, d: string) => localStorage.getItem('modcatalog.' + k) ?? d;
const setLS = (k: string, v: string) => localStorage.setItem('modcatalog.' + k, v);

/** Summarize an unfamiliar mod folder's contents by category (meshes/textures/materials/sounds/
 * scripts/plugins/archives/voice/...) before digging in by hand -- the GUI counterpart of the
 * catalog_mod_folder MCP tool. Read-only; never touches the folder it's pointed at. */
export default function ModCatalogPanel({ onClose }: { onClose: () => void }) {
  const [modPath, setModPath] = useState(() => LS('modPath', ''));
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState('');
  const [error, setError] = useState('');

  const modcatalog = getModCatalog();
  const unavailable = !modcatalog;

  useEffect(() => setLS('modPath', modPath), [modPath]);

  const browse = async () => {
    if (!modcatalog) return;
    const p = await modcatalog.BrowseForFolder('Select a mod folder to catalog');
    if (p) { setModPath(p); run(p); }
  };

  const run = async (path?: string) => {
    const p = (path ?? modPath).trim();
    if (!modcatalog || !p) return;
    setBusy(true); setResult(''); setError('');
    try {
      const text = await modcatalog.Catalog(p);
      setResult(text || '(no result)');
    } catch (e) {
      setError('Could not catalog: ' + (e instanceof Error ? e.message : String(e)));
    } finally { setBusy(false); }
  };

  const openFolder = async () => { if (modcatalog && modPath.trim()) await modcatalog.OpenFolder(modPath.trim()); };

  return (
    <div className="papyrus-overlay" onClick={onClose}>
      <div className="papyrus-modal glass-panel" onClick={e => e.stopPropagation()}>
        <div className="papyrus-header">
          <span className="papyrus-title"><FolderSearch size={16} /> Mod Catalog</span>
          <button className="papyrus-close" onClick={onClose} title="Close"><X size={16} /></button>
        </div>

        {unavailable && <div className="papyrus-warn">Mod catalog bridge not available — run the desktop app (not the browser dev server).</div>}

        <div className="papyrus-body">
          <div className="papyrus-form">
            <label className="papyrus-field">
              <span>Mod folder</span>
              <input value={modPath} onChange={e => setModPath(e.target.value)}
                     placeholder="Path to an unfamiliar mod's folder…"
                     onKeyDown={e => { if (e.key === 'Enter') run(); }} />
              <div className="papyrus-input-row">
                <button className="sidebar-action-btn" onClick={browse} disabled={unavailable}>Browse…</button>
                {modPath.trim() && (
                  <button className="sidebar-action-btn" onClick={openFolder} disabled={unavailable}>
                    <FolderOpen size={13} /> Open
                  </button>
                )}
              </div>
            </label>

            <button className="papyrus-run" onClick={() => run()} disabled={busy || unavailable || !modPath.trim()}>
              <Play size={14} /> {busy ? 'Cataloging…' : 'Catalog'}
            </button>
          </div>

          <div className="papyrus-output">
            <div className="papyrus-output-head">
              <span>CATALOG</span>
              {result && <button className="papyrus-copy" onClick={() => navigator.clipboard.writeText(result)}>Copy</button>}
            </div>
            <pre className="papyrus-output-body">
              {error || result || 'Pick a mod folder (or paste a path) and Catalog.'}
            </pre>
          </div>
        </div>
      </div>
    </div>
  );
}
