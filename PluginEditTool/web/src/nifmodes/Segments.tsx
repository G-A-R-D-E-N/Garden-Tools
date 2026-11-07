import { useState } from 'react';
import { Layers, Play, Save, RotateCcw } from 'lucide-react';
import type { NifHost } from '../backend';
import { useDialogs } from '../useDialogs';
import './Segments.css';

export interface SubSegment { startIndex: number; primitives: number; parentArrayIndex: number; unused: number }
export interface Segment { startIndex: number; primitives: number; parentArrayIndex: number; subSegments: SubSegment[] }
export interface UserData { userIndex: number; boneId: number; cutOffsets: number[] }
export interface SegmentTable {
  block: number;
  name: string;
  primitives: number;
  totalSegments: number;
  segments: Segment[];
  segmentStarts: number[];
  userData: UserData[];
  ssfFile: string;
  sidecar?: { path: string; found: boolean; listed: boolean; baseBone?: string; error?: string };
}

const MAX_UINT = 4294967295;
const NOT_GAME_CHECKED = 'Game acceptance of a rewritten segment table is unproven: the game has not been checked against an edited table.';
const SAVED = 'Saved to the NIF in place. ' + NOT_GAME_CHECKED;

const isWhole = (value: number) => Number.isInteger(value) && value >= 0 && value <= MAX_UINT;
const splitList = (text: string) => text.split(',').map(part => part.trim()).filter(part => part !== '');
const wholeList = (text: string): number[] | null => {
  const values = splitList(text).map(Number);
  return values.every(isWhole) ? values : null;
};
const floatList = (text: string): number[] | null => {
  const values = splitList(text).map(Number);
  return values.every(Number.isFinite) ? values : null;
};
const errorText = (error: unknown) => (error instanceof Error ? error.message : String(error));
const copy = (table: SegmentTable): SegmentTable => structuredClone(table);
const hasNonFiniteCut = (table: SegmentTable) =>
  table.userData.some(user => user.cutOffsets.some(cut => !Number.isFinite(cut)));

function tableNumbers(table: SegmentTable): number[] {
  return [
    table.primitives,
    table.totalSegments,
    ...table.segments.flatMap(segment => [
      segment.startIndex, segment.primitives, segment.parentArrayIndex,
      ...segment.subSegments.flatMap(sub => [sub.startIndex, sub.primitives, sub.parentArrayIndex, sub.unused]),
    ]),
    ...table.userData.flatMap(user => [user.userIndex, user.boneId]),
  ];
}

function wireValue(table: SegmentTable, starts: number[], cuts: number[][]) {
  return {
    primitives: table.primitives,
    totalSegments: table.totalSegments,
    segments: table.segments.map(segment => ({
      startIndex: segment.startIndex,
      primitives: segment.primitives,
      parentArrayIndex: segment.parentArrayIndex,
      subSegments: segment.subSegments.map(sub => ({
        startIndex: sub.startIndex, primitives: sub.primitives, parentArrayIndex: sub.parentArrayIndex, unused: sub.unused,
      })),
    })),
    segmentStarts: starts,
    userData: table.userData.map((user, index) => ({ userIndex: user.userIndex, boneId: user.boneId, cutOffsets: cuts[index] })),
    ssfFile: table.ssfFile,
  };
}

function NumberCell({ label, value, onChange }: { label: string; value: number; onChange: (next: number) => void }) {
  return (
    <label className="seg-cell">
      <span>{label}</span>
      <input type="number" min={0} step={1} value={Number.isNaN(value) ? '' : value}
             onChange={e => onChange(e.target.value === '' ? NaN : Number(e.target.value))} />
    </label>
  );
}

export default function Segments({ nif, nifPath, appendLog }: {
  nif: NifHost | undefined;
  nifPath: string;
  appendLog: (line: string) => void;
}) {
  const [blocks, setBlocks] = useState<{ id: number; name: string }[]>([]);
  const [table, setTable] = useState<SegmentTable | null>(null);
  const [draft, setDraft] = useState<SegmentTable | null>(null);
  const [startsText, setStartsText] = useState('');
  const [cutTexts, setCutTexts] = useState<string[]>([]);
  const [dirty, setDirty] = useState(false);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  const { confirm: askConfirm } = useDialogs();

  const loadTable = async (block: number) => {
    if (!nif) return;
    setBusy(true);
    try {
      const parsed = JSON.parse(await nif.Segments(nifPath, block)) as SegmentTable & { error?: string };
      if (parsed.error) throw new Error(parsed.error);
      setTable(parsed);
      setDraft(copy(parsed));
      setStartsText(parsed.segmentStarts.join(', '));
      setCutTexts(parsed.userData.map(user => user.cutOffsets.join(', ')));
      setDirty(false);
    } catch (e) {
      setTable(null);
      setDraft(null);
      setMessage('Could not read segments: ' + errorText(e));
    } finally { setBusy(false); }
  };

  const loadBlocks = async () => {
    if (!nif || !nifPath.trim()) return;
    setBusy(true);
    setMessage('');
    try {
      const parsed = JSON.parse(await nif.Tree(nifPath)) as { blocks?: { id: number; type: string; name: string }[] };
      const found = (parsed.blocks ?? [])
        .filter(block => block.type === 'BSSubIndexTriShape')
        .map(block => ({ id: block.id, name: block.name }));
      setBlocks(found);
      if (found.length === 0) {
        setTable(null);
        setDraft(null);
        setMessage('This NIF has no BSSubIndexTriShape block.');
        return;
      }
      await loadTable(found[0].id);
    } catch (e) {
      setMessage('Could not read the NIF: ' + errorText(e));
    } finally { setBusy(false); }
  };

  const switchBlock = async (block: number) => {
    const discard = !dirty || await askConfirm({
      title: 'Discard unsaved edits?', danger: true, confirmLabel: 'Discard',
      message: 'Switching blocks discards the unsaved edits in this table.',
    });
    if (!discard) return;
    await loadTable(block);
  };

  const edit = (change: (next: SegmentTable) => void) => {
    setDraft(prev => {
      if (!prev) return prev;
      const next = copy(prev);
      change(next);
      return next;
    });
    setDirty(true);
  };

  const revert = () => {
    if (!table) return;
    setDraft(copy(table));
    setStartsText(table.segmentStarts.join(', '));
    setCutTexts(table.userData.map(user => user.cutOffsets.join(', ')));
    setDirty(false);
    setMessage('');
  };

  const starts = wholeList(startsText);
  const cuts = cutTexts.map(floatList);
  const wire = draft && starts && cuts.every(cut => cut !== null)
    ? wireValue(draft, starts, cuts as number[][]) : null;
  const numbersOk = draft ? tableNumbers(draft).every(isWhole) : false;
  const cutsLost = table !== null && hasNonFiniteCut(table);
  const canSave = Boolean(draft && wire && numbersOk && dirty && !busy && !cutsLost);

  const save = async (saveAs: boolean) => {
    if (!nif || !draft || !wire || !canSave) return;
    setBusy(true);
    setMessage('');
    try {
      let outNif = '';
      if (saveAs) {
        outNif = await nif.BrowseForSave('Save NIF as', 'NIF mesh (*.nif)|*.nif');
        if (!outNif) return;
      }
      const payload = JSON.stringify([{ id: draft.block, key: 'segments', value: wire }]);
      const res = await nif.ApplyEdits(nifPath, payload, outNif);
      const ok = /set OK/.test(res);
      appendLog(ok ? `✓ saved segments of block ${draft.block}${saveAs ? ' (as new file)' : ''}` : `✗ segments save failed -- ${res}`);
      if (ok && saveAs) {
        setMessage(`Saved a copy to ${outNif}. ` + NOT_GAME_CHECKED);
      } else if (ok) {
        await loadTable(draft.block);
        setMessage(SAVED);
      } else {
        setMessage(res);
      }
    } catch (e) {
      const msg = errorText(e);
      appendLog('✗ segments save failed -- ' + msg);
      setMessage(msg);
    } finally { setBusy(false); }
  };

  return (
    <div className="seg-root">
      <div className="seg-head">
        <span className="seg-title"><Layers size={14} /> Armor segments</span>
        <button className="sidebar-action-btn" onClick={loadBlocks} disabled={busy || !nif || !nifPath.trim()}>
          <Play size={13} /> Load segments
        </button>
        {blocks.length > 0 && (
          <label className="seg-cell">
            <span>Block</span>
            <select value={table?.block ?? blocks[0].id} disabled={busy} onChange={e => switchBlock(Number(e.target.value))}>
              {blocks.map(block => <option key={block.id} value={block.id}>{block.id} {block.name}</option>)}
            </select>
          </label>
        )}
      </div>
      <p className="seg-note">{NOT_GAME_CHECKED}</p>

      {draft && (
        <>
          <div className="seg-row">
            <NumberCell label="Primitives" value={draft.primitives} onChange={n => edit(next => { next.primitives = n; })} />
            <NumberCell label="Total segments" value={draft.totalSegments} onChange={n => edit(next => { next.totalSegments = n; })} />
            <label className="seg-cell seg-wide">
              <span>SSF file</span>
              <input type="text" value={draft.ssfFile} onChange={e => edit(next => { next.ssfFile = e.target.value; })} />
            </label>
          </div>
          {draft.sidecar && (
            <p className="seg-note">
              Sidecar {draft.sidecar.found ? 'found' : 'missing'}{draft.sidecar.found && draft.sidecar.listed ? ', lists this shape' : ''}: {draft.sidecar.path}
            </p>
          )}
          {draft.sidecar?.error && <p className="seg-error">Sidecar could not be read: {draft.sidecar.error}</p>}

          {draft.segments.map((segment, index) => (
            <fieldset key={index} className="seg-segment">
              <legend>Segment {index}</legend>
              <div className="seg-row">
                <NumberCell label="Start index" value={segment.startIndex}
                            onChange={n => edit(next => { next.segments[index].startIndex = n; })} />
                <NumberCell label="Primitives" value={segment.primitives}
                            onChange={n => edit(next => { next.segments[index].primitives = n; })} />
                <NumberCell label="Parent array index" value={segment.parentArrayIndex}
                            onChange={n => edit(next => { next.segments[index].parentArrayIndex = n; })} />
              </div>
              <table className="seg-sub">
                <thead><tr><th>Sub-segment</th><th>Start</th><th>Primitives</th><th>Parent array</th><th>Unused</th></tr></thead>
                <tbody>
                  {segment.subSegments.map((sub, subIndex) => (
                    <tr key={subIndex}>
                      <td>{subIndex}</td>
                      <td><NumberCell label="Start index" value={sub.startIndex} onChange={n => edit(next => { next.segments[index].subSegments[subIndex].startIndex = n; })} /></td>
                      <td><NumberCell label="Primitives" value={sub.primitives} onChange={n => edit(next => { next.segments[index].subSegments[subIndex].primitives = n; })} /></td>
                      <td><NumberCell label="Parent array index" value={sub.parentArrayIndex} onChange={n => edit(next => { next.segments[index].subSegments[subIndex].parentArrayIndex = n; })} /></td>
                      <td><NumberCell label="Unused" value={sub.unused} onChange={n => edit(next => { next.segments[index].subSegments[subIndex].unused = n; })} /></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </fieldset>
          ))}

          <label className="seg-cell seg-wide">
            <span>Segment starts (comma separated)</span>
            <input type="text" value={startsText} onChange={e => { setStartsText(e.target.value); setDirty(true); }} />
          </label>

          {draft.userData.map((user, index) => (
            <div key={index} className="seg-row">
              <NumberCell label="User index" value={user.userIndex} onChange={n => edit(next => { next.userData[index].userIndex = n; })} />
              <NumberCell label="Bone ID" value={user.boneId} onChange={n => edit(next => { next.userData[index].boneId = n; })} />
              <label className="seg-cell seg-wide">
                <span>Cut offsets (comma separated)</span>
                <input type="text" value={cutTexts[index] ?? ''} onChange={e => {
                  const text = e.target.value;
                  setCutTexts(prev => prev.map((old, i) => (i === index ? text : old)));
                  setDirty(true);
                }} />
              </label>
            </div>
          ))}

          {!numbersOk || !wire ? (
            <p className="seg-error">Every number must be a whole number from 0 to 4294967295, and every cut offset a finite number.</p>
          ) : null}
          {cutsLost ? (
            <p className="seg-error">A cut offset in this NIF is not a finite number, so saving would drop it and change the list length. Save is disabled.</p>
          ) : null}
          <div className="seg-actions">
            <button className="sidebar-action-btn" onClick={revert} disabled={!dirty || busy}><RotateCcw size={13} /> Revert</button>
            <button className="sidebar-action-btn" onClick={() => save(true)} disabled={!canSave}>Save As…</button>
            <button className="papyrus-run segments-save" onClick={() => save(false)} disabled={!canSave}>
              <Save size={13} /> {busy ? 'Working…' : 'Save segments'}
            </button>
          </div>
        </>
      )}

      {message && <p className={message.startsWith('Saved') ? 'seg-ok' : 'seg-error'}>{message}</p>}
    </div>
  );
}
