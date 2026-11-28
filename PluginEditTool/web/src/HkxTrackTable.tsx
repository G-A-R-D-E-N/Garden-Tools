import { useEffect, useRef, useState } from 'react';
import { Table as TableIcon } from 'lucide-react';
import type { HkxAnimationInspect, HkxHost } from './backend';
import { filterTracks, trackRows } from './hkxTrackRows';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));
const PAGE_FRAMES = 500;

interface Props {
  host?: HkxHost;
  clipPath: string;
  skeletonPath?: string;
  disabled: boolean;
}

export default function HkxTrackTable({ host, clipPath, skeletonPath = '', disabled }: Props) {
  const [index, setIndex] = useState('0');
  const [reply, setReply] = useState<HkxAnimationInspect | null>(null);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [filter, setFilter] = useState('');
  const [names, setNames] = useState<HkxAnimationInspect | null>(null);
  const current = `${clipPath}\u0000${skeletonPath}`;
  const [seen, setSeen] = useState(current);
  if (current !== seen) {
    setSeen(current);
    setReply(null);
    setNames(null);
    setError('');
    setBusy(false);
  }
  const requestRef = useRef(0);
  useEffect(() => { requestRef.current += 1; }, [current]);

  const inspectCall = (track: number | null, limit: number | null) => {
    if (skeletonPath) return host!.InspectAnimation(clipPath, track, limit, skeletonPath);
    return track === null ? host!.InspectAnimation(clipPath) : host!.InspectAnimation(clipPath, track, limit);
  };

  const findTracks = async () => {
    if (!host || !clipPath) return;
    const ticket = ++requestRef.current;
    setBusy(true);
    setError('');
    try {
      const raw = await inspectCall(null, null);
      if (ticket !== requestRef.current) return;
      const parsed = (typeof raw === 'string' ? JSON.parse(raw) : raw) as HkxAnimationInspect & { error?: string };
      if (parsed.error) throw new Error(parsed.error);
      setNames(parsed);
    } catch (e) {
      if (ticket !== requestRef.current) return;
      setNames(null);
      setError(messageOf(e));
    } finally {
      if (ticket === requestRef.current) setBusy(false);
    }
  };

  const show = async (chosen?: number) => {
    if (!host || !clipPath) return;
    const wanted = chosen === undefined ? index.trim() : String(chosen);
    if (chosen !== undefined) setIndex(wanted);
    if (!/^\d+$/.test(wanted)) {
      setError('Track must be a whole number of zero or more');
      return;
    }
    const ticket = ++requestRef.current;
    setBusy(true);
    setError('');
    try {
      const raw = await inspectCall(Number(wanted), PAGE_FRAMES);
      if (ticket !== requestRef.current) return;
      const parsed = (typeof raw === 'string' ? JSON.parse(raw) : raw) as HkxAnimationInspect & { error?: string };
      if (parsed.error) throw new Error(parsed.error);
      trackRows(parsed);
      setReply(parsed);
    } catch (e) {
      if (ticket !== requestRef.current) return;
      setReply(null);
      setError(messageOf(e));
    } finally {
      if (ticket === requestRef.current) setBusy(false);
    }
  };

  const page = reply?.track;
  const flags = reply?.tracks?.find(track => track.index === page?.index);
  const table = reply ? trackRows(reply) : null;
  const matches = names ? filterTracks(names, filter) : [];
  const trackName = page ? reply?.tracks?.find(track => track.index === page.index)?.name : undefined;

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
        <input className="nif-in" value={filter} disabled={disabled || busy} aria-label="Track filter"
               placeholder="Filter tracks by bone name" onChange={e => setFilter(e.target.value)} />
        <button className="sidebar-action-btn" disabled={disabled || busy || !clipPath}
                onClick={() => void findTracks()}>
          Find tracks
        </button>
      </div>
      {names ? (
        <div className="nif-flabel" role="group" aria-label="Matching tracks">
          {matches.length === 0 ? 'no track matches' : (
            <>
              {matches.length} of {names.tracks?.length ?? 0} tracks
              {names.track_name_source && names.track_name_source !== 'none' ? ` named from the ${names.track_name_source} skeleton` : ''}
              {matches.map(match => (
                <button key={match.index} className="sidebar-action-btn" disabled={disabled || busy}
                        onClick={() => void show(match.index)}>
                  {match.index} {match.name}
                </button>
              ))}
            </>
          )}
        </div>
      ) : null}
      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {page && table ? (
        <>
          <div className="nif-flabel" role="status">
            Track {page.index}{trackName ? ` (${trackName})` : ''} of {reply?.track_count}: {page.frames} frames
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
