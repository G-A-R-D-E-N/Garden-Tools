import { useState } from 'react';
import { Table as TableIcon } from 'lucide-react';
import type { HkxAnimationInspect, HkxHost } from './backend';
import { trackRows } from './hkxTrackRows';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));
const PAGE_FRAMES = 500;

interface Props {
  host?: HkxHost;
  clipPath: string;
  disabled: boolean;
}

export default function HkxTrackTable({ host, clipPath, disabled }: Props) {
  const [index, setIndex] = useState('0');
  const [reply, setReply] = useState<HkxAnimationInspect | null>(null);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const show = async () => {
    if (!host || !clipPath) return;
    if (!/^\d+$/.test(index.trim())) {
      setError('Track must be a whole number of zero or more');
      return;
    }
    setBusy(true);
    setError('');
    try {
      const raw = await host.InspectAnimation(clipPath, Number(index.trim()), PAGE_FRAMES);
      const parsed = (typeof raw === 'string' ? JSON.parse(raw) : raw) as HkxAnimationInspect & { error?: string };
      if (parsed.error) throw new Error(parsed.error);
      trackRows(parsed);
      setReply(parsed);
    } catch (e) {
      setReply(null);
      setError(messageOf(e));
    } finally {
      setBusy(false);
    }
  };

  const page = reply?.track;
  const flags = reply?.tracks?.find(track => track.index === page?.index);
  const table = reply ? trackRows(reply) : null;

  return (
    <div className="nif-flags">
      <div className="nif-flags-head">
        <span className="nif-flabel"><TableIcon size={12} /> Clip tracks</span>
        <input className="nif-in" value={index} disabled={disabled || busy} aria-label="Track index"
               onChange={e => { setIndex(e.target.value); setReply(null); }} />
        <button className="sidebar-action-btn" disabled={disabled || busy || !clipPath}
                onClick={() => void show()}>
          Show track
        </button>
      </div>
      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {page && table ? (
        <>
          <div className="nif-flabel" role="status">
            Track {page.index} of {reply?.track_count}: {page.frames} frames
            {page.truncated ? `, first ${page.shown} shown` : ''}
            {flags ? ` · animated: ${[flags.translation_animated && 'translation', flags.rotation_animated && 'rotation',
              flags.scale_animated && 'scale'].filter(Boolean).join(', ') || 'none'}` : ''}
          </div>
          <table className="nif-flags-table" aria-label="Track frames">
            <thead>
              <tr><th>Frame</th><th>Time (s)</th><th>Translation</th><th>Rotation</th><th>Scale</th></tr>
            </thead>
            <tbody>
              {table.rows.map(row => (
                <tr key={row.frame}>
                  <td>{row.frame}</td><td>{row.time.toFixed(3)}</td><td>{row.position}</td><td>{row.rotation}</td><td>{row.scale}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </>
      ) : null}
    </div>
  );
}
