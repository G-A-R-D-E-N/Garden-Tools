import { useState } from 'react';
import { Search } from 'lucide-react';
import type { HkxHost } from './backend';
import {
  describeManifest, describeSubgraph, readSubgraph, type HkxSubgraphReply,
} from './hkxSubgraphClient';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

interface Props {
  host?: HkxHost;
  initialPlugin: string;
  initialDataFolder: string;
  disabled: boolean;
}

export default function HkxSubgraphTools({ host, initialPlugin, initialDataFolder, disabled }: Props) {
  const [plugin, setPlugin] = useState(initialPlugin);
  const [formId, setFormId] = useState('0x800');
  const [dataFolder, setDataFolder] = useState(initialDataFolder);
  const [reply, setReply] = useState<HkxSubgraphReply | null>(null);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const resolve = async () => {
    if (!host || !plugin.trim() || !formId.trim()) return;
    setBusy(true);
    setError('');
    try {
      const found = await readSubgraph(host, plugin.trim(), formId.trim(), dataFolder.trim());
      setReply(found);
      setNotice(describeSubgraph(found));
    } catch (e) {
      setReply(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="nif-flags">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Search size={12} /> Animation subgraph</span>
        <input className="nif-in" value={plugin} disabled={disabled || busy} aria-label="Race plugin path"
               placeholder="Path to a plugin .esp/.esm"
               onChange={e => setPlugin(e.target.value)} />
        <input className="nif-in" value={formId} disabled={disabled || busy} aria-label="Race form id"
               placeholder="0x0001A18D"
               onChange={e => setFormId(e.target.value)} />
        <input className="nif-in" value={dataFolder} disabled={disabled || busy} aria-label="Data folder"
               placeholder="Fallout 4 Data folder (optional)"
               onChange={e => setDataFolder(e.target.value)} />
        <button className="sidebar-action-btn" disabled={disabled || busy || !plugin.trim() || !formId.trim()}
                onClick={() => void resolve()}>
          Resolve subgraph
        </button>
      </div>

      {reply ? (
        <>
          <ul className="nif-flabel" role="list" aria-label="Subgraph ids">
            {reply.subgraph_ids.map(id => (
              <li key={id}>subgraph {id}</li>
            ))}
          </ul>
          <ul className="nif-flabel" role="list" aria-label="Race behavior graphs">
            {reply.behavior_graphs.map(graph => (
              <li key={graph}>{graph}</li>
            ))}
          </ul>
          <ul className="nif-flabel" role="list" aria-label="Race path prefixes">
            {reply.path_prefixes.map(prefix => (
              <li key={prefix}>{prefix}</li>
            ))}
          </ul>
          {reply.manifest ? (
            <div className="nif-flabel" aria-label="Manifest summary">
              {reply.manifest.path} ({reply.manifest.source}): {describeManifest(reply.manifest)}
            </div>
          ) : null}
          {reply.manifest && reply.manifest.unresolved.length > 0 ? (
            <ul className="papyrus-warn" role="list" aria-label="Unresolved animations">
              {reply.manifest.unresolved.map(animation => <li key={animation}>{animation}</li>)}
            </ul>
          ) : null}
          {reply.notes.length > 0 ? (
            <ul className="papyrus-warn" role="list" aria-label="Subgraph notes">
              {reply.notes.map(note => <li key={note}>{note}</li>)}
            </ul>
          ) : null}
        </>
      ) : null}

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}
