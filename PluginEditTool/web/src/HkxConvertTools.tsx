import { useState } from 'react';
import { RefreshCw } from 'lucide-react';
import type { HkxHost } from './backend';
import { convertAsset, describeConversion, type HkxConvertReply } from './hkxConvertClient';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

interface Props {
  host?: HkxHost;
  initialPath: string;
  disabled: boolean;
}

export default function HkxConvertTools({ host, initialPath, disabled }: Props) {
  const [path, setPath] = useState(initialPath);
  const [target, setTarget] = useState('');
  const [outPath, setOutPath] = useState('');
  const [overwrite, setOverwrite] = useState(false);
  const [preview, setPreview] = useState<HkxConvertReply | null>(null);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const run = async (save: boolean) => {
    if (!host || !path.trim()) return;
    setBusy(true);
    setError('');
    try {
      const reply = await convertAsset(host, path.trim(), target.trim(), save ? outPath.trim() : '',
        save && preview ? preview.source : '', overwrite);
      setPreview(save ? null : reply);
      setNotice(describeConversion(reply));
    } catch (e) {
      setError(messageOf(e));
      setNotice('');
      setPreview(null);
    } finally {
      setBusy(false);
    }
  };

  const reset = () => setPreview(null);

  return (
    <div className="nif-flags">
      <div className="nif-flags-head">
        <span className="nif-flabel"><RefreshCw size={12} /> Convert Havok version</span>
        <span className="nif-flabel">Verifies a same-version hk_2014.1.0-r1 packfile round trip; no field-changing version pair exists yet.</span>
        <input className="nif-in" value={path} disabled={disabled || busy} aria-label="Convert source path"
               placeholder="Path to the .hkx" onChange={e => { setPath(e.target.value); reset(); }} />
        <input className="nif-in" value={target} disabled={disabled || busy} aria-label="Target version"
               placeholder="Target version (default: the file's own)" onChange={e => { setTarget(e.target.value); reset(); }} />
        <input className="nif-in" value={outPath} disabled={disabled || busy} aria-label="Convert output path"
               placeholder="Output .hkx, a different file (needed to save)" onChange={e => setOutPath(e.target.value)} />
        <label className="nif-flabel">
          <input type="checkbox" checked={overwrite} disabled={disabled || busy}
                 onChange={e => { setOverwrite(e.target.checked); reset(); }} /> Replace an existing output file
        </label>
        <button className="sidebar-action-btn" disabled={disabled || busy || !path.trim()}
                onClick={() => void run(false)}>
          Preview conversion
        </button>
        <button className="sidebar-action-btn"
                disabled={disabled || busy || !path.trim() || !outPath.trim() || !preview || !preview.ok}
                onClick={() => void run(true)}>
          Save conversion
        </button>
      </div>

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
      {preview && preview.opaque_classes.length > 0
        ? <div className="nif-flabel">Left opaque: {preview.opaque_classes.join(', ')}</div> : null}
      {preview && preview.changes.length > 0 ? (
        <ul className="hkx-bones" aria-label="Changed fields">
          {preview.changes.map((c, i) => (
            <li key={i}>{`${c.class} ${c.object} ${c.member}: ${c.before} to ${c.after}`}</li>
          ))}
        </ul>
      ) : null}
      {preview && preview.diagnostics.length > 0 ? (
        <ul className="hkx-bones" aria-label="Conversion diagnostics">
          {preview.diagnostics.map((d, i) => <li key={i}>{`${d.level}: ${d.member || 'object'} ${d.message}`}</li>)}
        </ul>
      ) : null}
    </div>
  );
}
