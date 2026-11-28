import { useEffect, useState } from 'react';
import { Boxes } from 'lucide-react';
import type { NifHost } from './backend';
import {
  INTERPOLATIONS, blankKey, channelsFor, keyCount, keyValueLabels, parseKeys, resizeKeys, writeKeys, type KeyRow,
} from './keyToolsClient';

const message = (e: unknown) => (e instanceof Error ? e.message : String(e));

interface Props {
  nif: NifHost;
  nifPath: string;
  blocks: { id: number; type: string; name: string }[];
  disabled: boolean;
  expectedSource?: string;
  appendLog: (line: string) => void;
  onSaved: (result: string) => void;
  onBusyChange?: (busy: boolean) => void;
}

export default function NifKeyTools(
  { nif, nifPath, blocks, disabled, expectedSource = '', appendLog, onSaved, onBusyChange }: Props,
) {
  const [block, setBlock] = useState(-1);
  const [channelId, setChannelId] = useState('');
  const [interpolation, setInterpolation] = useState(1);
  const [rows, setRows] = useState<KeyRow[]>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => { onBusyChange?.(busy); }, [busy, onBusyChange]);

  const picked = blocks.find(b => b.id === block);
  const channels = picked ? channelsFor(picked.type) : [];
  const channel = channels.find(c => c.id === channelId);
  const labels = channel ? keyValueLabels(channel, interpolation) : [];
  const keys = parseKeys(rows);
  const idle = disabled || busy;

  const reshape = (next: typeof channel, kind: number) => {
    if (!next) { setRows([]); return; }
    const count = keyCount(kind, next.components, next.quaternion);
    setRows(current => (current.length === 0 ? [blankKey(count)] : resizeKeys(current, count)));
  };

  const pickBlock = (id: number) => {
    setBlock(id);
    setError('');
    const first = channelsFor(blocks.find(b => b.id === id)?.type ?? '')[0];
    setChannelId(first?.id ?? '');
    setRows([]);
    reshape(first, interpolation);
  };

  const pickChannel = (id: string) => {
    setChannelId(id);
    reshape(channels.find(c => c.id === id), interpolation);
  };

  const pickInterpolation = (kind: number) => {
    setInterpolation(kind);
    reshape(channel, kind);
  };

  const setCell = (row: number, cell: number | 'time', value: string) => {
    setRows(current => current.map((entry, at) => {
      if (at !== row) return entry;
      return cell === 'time'
        ? { ...entry, time: value }
        : { ...entry, value: entry.value.map((old, i) => (i === cell ? value : old)) };
    }));
  };

  const apply = async () => {
    if (idle || !channel || !keys) return;
    setBusy(true);
    setError('');
    try {
      const report = await writeKeys(nif, nifPath, block, channel.id, interpolation, keys, expectedSource);
      if (!report.ok) {
        setError(report.refusal);
        appendLog(`✗ set keys refused -- ${report.refusal}`);
        return;
      }
      appendLog(`✓ ${report.channel}: wrote ${report.keys} key${report.keys === 1 ? '' : 's'} to block ${block}`);
      onSaved(report.saved);
    } catch (e) {
      setError(message(e));
      appendLog(`✗ set keys failed -- ${message(e)}`);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="nif-meshtools">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Boxes size={12} /> Key tools</span>
        <select className="nif-in" value={block} disabled={idle} aria-label="Key tools block"
                onChange={e => pickBlock(Number(e.target.value))}>
          <option value={-1}>Select a key block…</option>
          {blocks.map(b => <option key={b.id} value={b.id}>{b.id}: {b.type}{b.name ? ` ${b.name}` : ''}</option>)}
        </select>
        <select className="nif-in" value={channelId} disabled={idle || !picked} aria-label="Key tools channel"
                onChange={e => pickChannel(e.target.value)}>
          {channels.map(c => <option key={c.id} value={c.id}>{c.label}</option>)}
        </select>
        <select className="nif-in" value={interpolation} disabled={idle || !channel} aria-label="Key tools interpolation"
                onChange={e => pickInterpolation(Number(e.target.value))}>
          {INTERPOLATIONS.map(i => <option key={i.value} value={i.value}>{i.label}</option>)}
        </select>
      </div>
      {channel && rows.map((row, at) => (
        <div key={at} className="nif-editor-actions">
          <input className="nif-in nif-num" type="number" step={0.01} value={row.time} disabled={idle}
                 aria-label={`Key ${at} time`} title="Key time in seconds"
                 onChange={e => setCell(at, 'time', e.target.value)} />
          {labels.map((label, i) => (
            <input key={i} className="nif-in nif-num" type="number" step="any" value={row.value[i] ?? ''} disabled={idle}
                   aria-label={`Key ${at} ${label}`} title={label} onChange={e => setCell(at, i, e.target.value)} />
          ))}
          <button className="sidebar-action-btn" disabled={idle || rows.length < 2} aria-label={`Remove key ${at}`}
                  onClick={() => setRows(current => current.filter((_, i) => i !== at))}>
            Remove
          </button>
        </div>
      ))}
      <div className="nif-editor-actions">
        <button className="sidebar-action-btn" disabled={idle || !channel}
                onClick={() => channel && setRows(current => [...current, blankKey(labels.length)])}>
          Add key
        </button>
        <button className="sidebar-action-btn" disabled={idle || !channel || !keys} onClick={() => void apply()}
                title="Replace the whole channel with these keys and save the file; times must not decrease">
          Apply keys
        </button>
      </div>
      {error && <span className="papyrus-warn" title={error}>{error.split('\n')[0]}</span>}
    </div>
  );
}
