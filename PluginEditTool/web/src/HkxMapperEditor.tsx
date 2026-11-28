import { useState } from 'react';
import type { HkxHost } from './backend';
import { describeMapperEdit, editMapper, type HkxMapperEditReply } from './hkxMapperClient';
import { describeMapperRow, readMappers } from './hkxToolsClient';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

interface Props {
  host?: HkxHost;
  path: string;
  sourceSha: string;
  mapperCount: number;
  disabled: boolean;
  onNotice: (text: string) => void;
  onError: (text: string) => void;
}

// Invariant: a refusal keeps the pending edits and shows no result, so a refused save never reads as a written file.
export default function HkxMapperEditor({ host, path, sourceSha, mapperCount, disabled, onNotice, onError }: Props) {
  const [mapper, setMapper] = useState(0);
  const [text, setText] = useState('[{"kind": "build_rows"}]');
  const [out, setOut] = useState('');
  const [overwrite, setOverwrite] = useState(false);
  const [result, setResult] = useState<HkxMapperEditReply | null>(null);
  const [working, setWorking] = useState(false);
  const [sha, setSha] = useState(sourceSha);

  const [seen, setSeen] = useState({ path, sourceSha });
  if (seen.path !== path || seen.sourceSha !== sourceSha) {
    setSeen({ path, sourceSha });
    setSha(sourceSha);
    setResult(null);
  }

  const run = async (save: boolean) => {
    if (!host) return;
    let edits: unknown;
    try {
      edits = JSON.parse(text);
    } catch {
      onError('the edits are not valid JSON');
      return;
    }
    if (!Array.isArray(edits) || edits.length === 0) {
      onError('the edits must be a non-empty JSON list');
      return;
    }
    if (save && !out.trim()) {
      onError('choose an output path to save');
      return;
    }
    setWorking(true);
    onError('');
    try {
      const reply = await editMapper(host, path, mapper, edits, save ? out.trim() : '', overwrite, sha, 2000);
      setResult(reply);
      onNotice(describeMapperEdit(reply));
      if (save && reply.saved) {
        const fresh = await readMappers(host, path, false, 1).catch(() => null);
        if (fresh?.source_sha256) setSha(fresh.source_sha256);
      }
    } catch (e) {
      setResult(null);
      onNotice('');
      onError(messageOf(e));
    } finally {
      setWorking(false);
    }
  };

  const shown = result?.mappers[result.mapper];
  return (
    <div>
      <div className="nif-flabel">
        Edit the mapper. Kinds: build_rows, set_row_bones, set_row_transform, set_row_pose, insert_row,
        insert_row_with_transform, remove_row, recompute_row, set_chain_bones, set_chain_transforms,
        set_mapping_type, set_keep_unmapped_local. Chain rows are edited in place only.
      </div>
      <textarea className="nif-in" rows={5} value={text} disabled={disabled || working} aria-label="Mapper edits"
                onChange={e => setText(e.target.value)} />
      <div className="nif-editor-actions">
        <input className="nif-in" type="number" min={0} max={Math.max(0, mapperCount - 1)} value={mapper}
               disabled={disabled || working} aria-label="Mapper position"
               onChange={e => setMapper(Math.max(0, Math.trunc(Number(e.target.value) || 0)))} />
        <input className="nif-in" value={out} disabled={disabled || working} aria-label="Mapper output path"
               placeholder="Output .hkx path" onChange={e => setOut(e.target.value)} />
        <label className="nif-flabel">
          <input type="checkbox" checked={overwrite} disabled={disabled || working}
                 onChange={e => setOverwrite(e.target.checked)} />
          overwrite an existing file (the source included)
        </label>
        <button className="sidebar-action-btn" disabled={disabled || working || !host} onClick={() => void run(false)}>
          Preview edits
        </button>
        <button className="sidebar-action-btn" disabled={disabled || working || !host || !out.trim()}
                onClick={() => void run(true)}>
          Save mapper as
        </button>
      </div>
      {shown ? (
        <ul className="nif-flabel" role="list" aria-label="Mapper edit result">
          {shown.rows.map((row, at) => <li key={`${at}:${row.bone_a}:${row.bone_b}`}>{describeMapperRow(row)}</li>)}
        </ul>
      ) : null}
    </div>
  );
}
