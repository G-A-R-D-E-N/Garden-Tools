import { useState } from 'react';
import { Scissors } from 'lucide-react';
import type { HkxHost } from './backend';
import { buildOp, describeClipEdit, editClip, OP_FIELDS, parseOps, type HkxClipEditReply } from './hkxTimelineClient';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

interface Props {
  host?: HkxHost;
  initialPath: string;
  disabled: boolean;
}

export default function HkxTimelineTools({ host, initialPath, disabled }: Props) {
  const [path, setPath] = useState(initialPath);
  const [ops, setOps] = useState('[{"op":"trim","first":0,"last":1}]');
  const [outPath, setOutPath] = useState('');
  const [overwrite, setOverwrite] = useState(false);
  const [spline, setSpline] = useState<boolean | null>(null);
  const [preview, setPreview] = useState<HkxClipEditReply | null>(null);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [formOp, setFormOp] = useState('trim');
  const [formValues, setFormValues] = useState<Record<string, string>>({});

  const addOp = () => {
    try {
      const built = buildOp(formOp, formValues);
      let list: unknown;
      try {
        list = JSON.parse(ops);
      } catch {
        throw new Error('edit ops are not valid JSON, fix the text before adding an op');
      }
      if (!Array.isArray(list)) throw new Error('edit ops must be a list, fix the text before adding an op');
      setOps(JSON.stringify([...list, built]));
      setFormValues({});
      setPreview(null);
      setError('');
    } catch (e) {
      setError(messageOf(e));
    }
  };

  const run = async (save: boolean) => {
    if (!host || !path.trim()) return;
    setBusy(true);
    setError('');
    try {
      const reply = await editClip(host, path.trim(), parseOps(ops), save ? outPath.trim() : '',
        save && preview ? preview.source : '', overwrite, spline);
      setPreview(save ? null : reply);
      setNotice(describeClipEdit(reply));
    } catch (e) {
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="nif-flags">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Scissors size={12} /> Clip timeline edit</span>
        <input className="nif-in" value={path} disabled={disabled || busy} aria-label="Clip path"
               placeholder="Path to the animation .hkx" onChange={e => { setPath(e.target.value); setPreview(null); }} />
        <label className="nif-flabel">
          Op to add
          <select value={formOp} disabled={disabled || busy} aria-label="Op to add"
                  onChange={e => { setFormOp(e.target.value); setFormValues({}); }}>
            {Object.keys(OP_FIELDS).map(name => <option key={name} value={name}>{name}</option>)}
          </select>
        </label>
        {OP_FIELDS[formOp].map(field => field.kind === 'flag' ? (
          <label className="nif-flabel" key={`${formOp}.${field.name}`}>
            <input type="checkbox" checked={formValues[field.name] === 'true'} disabled={disabled || busy}
                   onChange={e => setFormValues({ ...formValues, [field.name]: e.target.checked ? 'true' : '' })} /> {field.label}
          </label>
        ) : (
          <input className="nif-in" key={`${formOp}.${field.name}`} value={formValues[field.name] ?? ''}
                 disabled={disabled || busy} aria-label={field.label}
                 placeholder={field.optional ? `${field.label} (optional)` : field.label}
                 onChange={e => setFormValues({ ...formValues, [field.name]: e.target.value })} />
        ))}
        <button className="sidebar-action-btn" disabled={disabled || busy} onClick={addOp}>Add op</button>
        <button className="sidebar-action-btn" disabled={disabled || busy}
                onClick={() => { setOps('[]'); setPreview(null); }}>Clear ops</button>
        <textarea className="nif-in" value={ops} disabled={disabled || busy} aria-label="Edit ops" rows={4}
                  onChange={e => { setOps(e.target.value); setPreview(null); }} />
        <input className="nif-in" value={outPath} disabled={disabled || busy} aria-label="Output path"
               placeholder="Output .hkx (needed to save)" onChange={e => setOutPath(e.target.value)} />
        <label className="nif-flabel">
          <input type="checkbox" checked={overwrite} disabled={disabled || busy}
                 onChange={e => { setOverwrite(e.target.checked); setPreview(null); }} /> Replace an existing file
        </label>
        <label className="nif-flabel">
          Storage
          <select value={spline === null ? 'keep' : spline ? 'spline' : 'plain'} disabled={disabled || busy}
                  aria-label="Clip storage"
                  onChange={e => { setSpline(e.target.value === 'keep' ? null : e.target.value === 'spline'); setPreview(null); }}>
            <option value="keep">Keep the source's storage</option>
            <option value="spline">Spline compressed</option>
            <option value="plain">Uncompressed</option>
          </select>
        </label>
        <button className="sidebar-action-btn" disabled={disabled || busy || !path.trim()}
                onClick={() => void run(false)}>
          Preview edit
        </button>
        <button className="sidebar-action-btn" disabled={disabled || busy || !path.trim() || !outPath.trim() || !preview}
                onClick={() => void run(true)}>
          Save edit
        </button>
      </div>

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}
