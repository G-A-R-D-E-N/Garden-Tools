import { useState } from 'react';
import { Search } from 'lucide-react';
import type { HkxHost } from './backend';
import {
  auditChannels, describeChannels, describeObject, describePackfile, describeRepack, describeSearch, describeTree,
  describeUsage, readObject, readObjectTree, readPackfile, readRepackCheck, readSymbolUsage, searchHkx,
  type HkxChannelReport, type HkxObjectDetail, type HkxObjectLink, type HkxObjectTree, type HkxPackfileReport,
  type HkxRepackReport, type HkxSearchReport, type HkxUsageReport,
} from './hkxInspectClient';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

type Mode = 'object' | 'tree' | 'search' | 'packfile' | 'repack' | 'channels' | 'usage';
type Result =
  | { mode: 'object'; value: HkxObjectDetail }
  | { mode: 'tree'; value: HkxObjectTree }
  | { mode: 'search'; value: HkxSearchReport }
  | { mode: 'packfile'; value: HkxPackfileReport }
  | { mode: 'repack'; value: HkxRepackReport }
  | { mode: 'channels'; value: HkxChannelReport }
  | { mode: 'usage'; value: HkxUsageReport };

const MODES: { mode: Mode; label: string; extra?: string }[] = [
  { mode: 'object', label: 'Object', extra: 'Object id' },
  { mode: 'tree', label: 'Object tree' },
  { mode: 'search', label: 'Search', extra: 'Search text' },
  { mode: 'packfile', label: 'Packfile' },
  { mode: 'repack', label: 'Repack check' },
  { mode: 'channels', label: 'Channels', extra: 'Animation file or folder' },
  { mode: 'usage', label: 'Symbol usage' },
];

const linkText = (link: HkxObjectLink) =>
  `${link.member || 'offset ' + link.source_offset} -> `
  + (link.target_object >= 0 ? `object ${link.target_object} (${link.target_class})`
    : link.inner ? `inside its own span at ${link.target_offset}` : `section ${link.target_section} at ${link.target_offset}`);

interface Props {
  host?: HkxHost;
  initialPath: string;
  disabled: boolean;
}

export default function HkxInspectTools({ host, initialPath, disabled }: Props) {
  const [mode, setMode] = useState<Mode>('object');
  const [path, setPath] = useState(initialPath);
  const [extra, setExtra] = useState('');
  const [result, setResult] = useState<Result | null>(null);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const current = MODES.find(item => item.mode === mode)!;
  const ready = path.trim() !== '' && (!current.extra || extra.trim() !== '');

  const run = async () => {
    if (!host || !ready) return;
    setBusy(true);
    setError('');
    try {
      const target = path.trim();
      if (mode === 'object') {
        const id = Number(extra.trim());
        if (!Number.isInteger(id) || id < 0) throw new Error('the object id must be zero or more');
        const value = await readObject(host, target, id);
        setResult({ mode, value });
        setNotice(describeObject(value));
      } else if (mode === 'tree') {
        const value = await readObjectTree(host, target);
        setResult({ mode, value });
        setNotice(describeTree(value));
      } else if (mode === 'search') {
        const value = await searchHkx(host, target, extra.trim());
        setResult({ mode, value });
        setNotice(describeSearch(value));
      } else if (mode === 'packfile') {
        const value = await readPackfile(host, target);
        setResult({ mode, value });
        setNotice(describePackfile(value));
      } else if (mode === 'repack') {
        const value = await readRepackCheck(host, target);
        setResult({ mode, value });
        setNotice(describeRepack(value));
      } else if (mode === 'channels') {
        const value = await auditChannels(host, target, extra.trim());
        setResult({ mode, value });
        setNotice(describeChannels(value));
      } else {
        const value = await readSymbolUsage(host, target);
        setResult({ mode, value });
        setNotice(describeUsage(value));
      }
    } catch (e) {
      setResult(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="nif-flags">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Search size={12} /> Inspect</span>
        <select className="nif-in" value={mode} disabled={disabled || busy} aria-label="Inspection"
                onChange={e => { setMode(e.target.value as Mode); setExtra(''); setResult(null); setNotice(''); setError(''); }}>
          {MODES.map(item => <option key={item.mode} value={item.mode}>{item.label}</option>)}
        </select>
        <input className="nif-in" value={path} disabled={disabled || busy}
               aria-label={mode === 'channels' ? 'Skeleton path' : 'Inspect path'}
               placeholder={mode === 'channels' ? 'Path to the skeleton .hkx' : mode === 'search' ? 'An .hkx file or a folder' : 'Path to an .hkx file'}
               onChange={e => setPath(e.target.value)} />
        {current.extra ? (
          <input className="nif-in" value={extra} disabled={disabled || busy} aria-label={current.extra}
                 placeholder={current.extra} onChange={e => setExtra(e.target.value)} />
        ) : null}
        <button className="sidebar-action-btn" disabled={disabled || busy || !ready} onClick={() => void run()}>
          Inspect
        </button>
      </div>

      {result?.mode === 'object' ? (
        <>
          <div className="nif-flabel">
            Object {result.value.object}: {result.value.class_name} at {result.value.offset}, signature {result.value.signature}
            {result.value.layout_known ? '' : ', no verified layout'}
          </div>
          <ul className="nif-flabel" role="list" aria-label="Object fields">
            {result.value.fields.map(field => (
              <li key={`${field.offset}:${field.name}`}>
                {field.name} +{field.offset}: {field.value ? `${field.value} (${field.hex})` : field.hex}{field.reason ? ` (${field.reason})` : ''}
              </li>
            ))}
          </ul>
          <ul className="nif-flabel" role="list" aria-label="Incoming references">
            {result.value.incoming.map((link, at) => <li key={at}>from object {link.source_object}: {linkText(link)}</li>)}
          </ul>
          <ul className="nif-flabel" role="list" aria-label="Outgoing pointers">
            {result.value.outgoing.map((link, at) => <li key={at}>{linkText(link)}</li>)}
          </ul>
          {result.value.partial ? <div className="papyrus-warn" aria-label="Inspection note">{result.value.note}</div> : null}
        </>
      ) : null}

      {result?.mode === 'tree' ? (
        <>
          <ul className="nif-flabel" role="list" aria-label="Object tree">
            {result.value.nodes.map(row => (
              <li key={row.node} style={{ paddingLeft: row.depth * 12 }}>
                {row.member ? `${row.member}: ` : ''}{row.class_name}{row.name ? ` "${row.name}"` : ''}
                {row.animation ? ` (${row.animation})` : ''}{row.summary ? ` ${row.summary}` : ''} #{row.object} @{row.offset}
                {row.repeated ? ' (shown above)' : ''}{row.cut ? ' (depth limit)' : ''}
              </li>
            ))}
          </ul>
          {result.value.partial ? <div className="papyrus-warn" aria-label="Inspection note">{result.value.note}</div> : null}
        </>
      ) : null}

      {result?.mode === 'search' ? (
        <ul className="nif-flabel" role="list" aria-label="Search hits">
          {result.value.hits.map((hit, at) => (
            <li key={at}>[{hit.kind}] {hit.text} - {hit.file}{hit.object >= 0 ? ` #${hit.object}` : ''}</li>
          ))}
        </ul>
      ) : null}

      {result?.mode === 'packfile' ? (
        <>
          {(result.value.classes ?? []).length > 0 ? (
            <ul className="nif-flabel" role="list" aria-label="Packfile classes">
              {(result.value.classes ?? []).map(row => (
                <li key={row.name + row.signature}>{row.name} x{row.count}: {row.layout_status.replace('_', ' ')}</li>
              ))}
            </ul>
          ) : null}
          {result.value.issues.length > 0 ? (
            <ul className="papyrus-warn" role="list" aria-label="Packfile issues">
              {result.value.issues.map((line, at) => <li key={at}>{line}</li>)}
            </ul>
          ) : null}
        </>
      ) : null}

      {result?.mode === 'repack' && result.value.verdict !== 'equal' && result.value.verdict !== 'refused' ? (
        <div className="papyrus-warn" role="note" aria-label="Repack check">{describeRepack(result.value)}</div>
      ) : null}

      {result?.mode === 'channels' ? (
        <>
          <ul className="nif-flabel" role="list" aria-label="Channel audit">
            {result.value.files.map(file => (
              <li key={file.file}>
                {file.file}: {file.tracks} tracks, undriven offsets T{file.offset_translation} R{file.offset_rotation} S{file.offset_scale}
                {file.offsets.map(track => (
                  <div key={track.track}>
                    {track.bone_name}: {[track.translation_undriven && 'translation', track.rotation_undriven && 'rotation',
                      track.scale_undriven && 'scale'].filter(Boolean).join(', ')} not animated
                  </div>
                ))}
              </li>
            ))}
          </ul>
          {result.value.skipped_files.length > 0 ? (
            <ul className="papyrus-warn" role="list" aria-label="Skipped clips">
              {result.value.skipped_files.map(file => <li key={file.file}>{file.file}: {file.reason}</li>)}
            </ul>
          ) : null}
        </>
      ) : null}

      {result?.mode === 'usage' ? (
        <>
          <ul className="nif-flabel" role="list" aria-label="Event usage">
            {result.value.events.map(event => (
              <li key={event.event_id}>
                {event.in_range ? event.name : `event ${event.event_id} (outside the table)`}: {event.sites.length === 0 ? 'no decoded site'
                  : event.sites.map(site => `${site.site} ${site.owner}${site.member ? ` ${site.member}` : ''}`).join('; ')}
              </li>
            ))}
          </ul>
          <ul className="nif-flabel" role="list" aria-label="State usage">
            {result.value.states.map(state => (
              <li key={state.state}>{state.name} (id {state.state_id}): {state.incoming} in, {state.outgoing} out{state.is_start ? ', start' : ''}</li>
            ))}
          </ul>
          {result.value.partial ? <div className="papyrus-warn" aria-label="Usage note">{result.value.note}</div> : null}
        </>
      ) : null}

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}
