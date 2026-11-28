import { useState } from 'react';
import { AlertTriangle, CheckCircle2, Save, Wrench } from 'lucide-react';
import type { NifHost } from '../backend';
import { useDialogs } from '../useDialogs';

export interface VerifyCheck { name: string; failure: string; fixable: boolean; }

const fileName = (p: string) => p.split(/[\\/]/).pop() || p;
const TANGENT_CHECK = 'regenerate tangent frames';

export default function Issues(
  { nif, path, checks, onWritten, appendLog }: {
    nif: NifHost;
    path: string;
    checks: VerifyCheck[];
    onWritten: (outPath: string) => void;
    appendLog: (line: string) => void;
  }
) {
  const { confirm } = useDialogs();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const name = fileName(path);
  const failing = checks.filter(c => c.failure !== '');
  const fixable = failing.filter(c => c.fixable);

  const writeFix = async (outPath: string, check: string | null, overwrite: boolean) => {
    setBusy(true);
    setError('');
    try {
      let res = await nif.Fix(path, outPath, check, overwrite);
      if (res.startsWith('EXISTS:')) {
        const replace = await confirm({
          title: 'Replace existing NIF?',
          message: `${outPath} already exists. Overwrite it with the fixed copy?`,
          confirmLabel: 'Overwrite',
          danger: true,
        });
        if (!replace) {
          appendLog(`✗ fix ${name} -- ${fileName(outPath)} was left unchanged`);
          return;
        }
        res = await nif.Fix(path, outPath, check, true);
      }
      if (!/RESULT: fix OK/.test(res)) {
        const line = res.split('\n')[0];
        setError(line);
        appendLog(`✗ fix ${name} -- ${line}`);
        return;
      }
      appendLog(`✓ fixed ${name} → ${fileName(outPath)}`);
      onWritten(outPath);
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      setError(msg);
      appendLog(`✗ fix ${name} -- ${msg}`);
    } finally {
      setBusy(false);
    }
  };

  const fixInPlace = async (check?: string) => {
    const ok = await confirm({
      title: 'Overwrite NIF with fixes?',
      message: check ? `${name} is rewritten in place to repair ${check}.` : `${name} is rewritten in place with all fixes.`,
      confirmLabel: 'Fix in place',
      danger: true,
    });
    if (ok) await writeFix(path, check ?? null, true);
  };

  const saveAs = async () => {
    const out = await nif.BrowseForSave('Save fixed NIF as', 'NIF mesh (*.nif)|*.nif');
    if (out) await writeFix(out, null, false);
  };

  if (checks.length === 0) return null;

  return (
    <div className="nif-issues">
      <div className="nif-issues-head">
        {failing.length === 0
          ? <span className="nif-issues-summary ok"><CheckCircle2 size={14} /> All {checks.length} checks pass</span>
          : <span className="nif-issues-summary"><AlertTriangle size={14} /> {failing.length} of {checks.length} checks failed</span>}
        <div className="nif-issues-actions">
          {fixable.length > 0 && (<>
            <button className="sidebar-action-btn" disabled={busy} onClick={saveAs}><Save size={13} /> Save As…</button>
            <button className="papyrus-run" disabled={busy} onClick={() => fixInPlace()}><Wrench size={13} /> Fix all ({fixable.length})</button>
          </>)}
          <button className="sidebar-action-btn" disabled={busy} onClick={() => fixInPlace(TANGENT_CHECK)}
            title="Rewrite every shape's tangent frames from its normals and UVs">
            <Wrench size={13} /> Regenerate tangents
          </button>
        </div>
      </div>
      {error && <div className="papyrus-warn">{error}</div>}
      {failing.map(check => (
        <section key={check.name} className="nif-issue-group">
          <div className="nif-issue-head">
            <span className="nif-issue-name">{check.name}</span>
            {check.fixable && (
              <button className="sidebar-action-btn" disabled={busy} onClick={() => fixInPlace(check.name)} title="Repairs this check">
                <Wrench size={12} /> Fix
              </button>
            )}
          </div>
          <p className="nif-issue-finding">{check.failure}</p>
        </section>
      ))}
    </div>
  );
}
