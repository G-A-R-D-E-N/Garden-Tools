import { useEffect, useState } from 'react';
import { Shapes } from 'lucide-react';
import type { NifHost } from './backend';
import { EditorTabPanel, EditorTabs } from './Editors/EditorTabs';
import {
  describeSkinEditReport, describeSkinReport, readInfluencedBy, readInfluences, readSkinReport, runSkinEdit,
  type SkinFinding, type SkinInfluence,
} from './skinToolsClient';

const message = (e: unknown) => (e instanceof Error ? e.message : String(e));
const MAX_INFLUENCES = 4;

type SkinTab = 'influences' | 'bone' | 'mirror' | 'check';

// Invariant: the four tabs are the four BGS SkinEdit entry points plus the validator, in the order a rigger uses them.
const TABS = [
  { id: 'influences', label: 'Influences' },
  { id: 'bone', label: 'Bone' },
  { id: 'mirror', label: 'Mirror' },
  { id: 'check', label: 'Check' },
];

interface Props {
  nif: NifHost;
  nifPath: string;
  shapes: { id: number; name: string }[];
  disabled: boolean;
  expectedSource?: string;
  appendLog: (line: string) => void;
  onSaved: (result: string) => void;
  onBusyChange?: (busy: boolean) => void;
}

const rows = (list: SkinInfluence[]): SkinInfluence[] => {
  const kept = list.slice(0, MAX_INFLUENCES);
  while (kept.length < MAX_INFLUENCES) kept.push({ bone: -1, name: '', weight: 0 });
  return kept;
};

export default function NifSkinTools(
  { nif, nifPath, shapes, disabled, expectedSource = '', appendLog, onSaved, onBusyChange }: Props,
) {
  const [tab, setTab] = useState<SkinTab>('influences');
  const [block, setBlock] = useState(-1);
  const [bones, setBones] = useState<string[]>([]);
  const [vertex, setVertex] = useState(0);
  const [influences, setInfluences] = useState<SkinInfluence[]>(rows([]));
  const [boneIndex, setBoneIndex] = useState(0);
  const [minimum, setMinimum] = useState(0.01);
  const [fromBone, setFromBone] = useState(0);
  const [toBone, setToBone] = useState(1);
  const [keepSource, setKeepSource] = useState(false);
  const [lane, setLane] = useState(0);
  const [fromPositive, setFromPositive] = useState(true);
  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState('');
  const [findings, setFindings] = useState<SkinFinding[]>([]);
  const [error, setError] = useState('');

  useEffect(() => { onBusyChange?.(busy); }, [busy, onBusyChange]);

  const load = async (target: number, index: number) => {
    if (target < 0) return;
    setBusy(true);
    setError('');
    try {
      const read = await readInfluences(nif, nifPath, target, index);
      setBones(read.bones);
      setInfluences(rows(read.influences));
      setBoneIndex(value => (value < read.bones.length ? value : 0));
      setFromBone(value => (value < read.bones.length ? value : 0));
      setToBone(value => (value < read.bones.length ? value : Math.min(1, Math.max(0, read.bones.length - 1))));
      setNotice(
        read.influences.length === 0
          ? `vertex ${index} of ${read.name} moves with no bone`
          : `vertex ${index}: ` + read.influences.map(v => `${v.name || `bone ${v.bone}`} ${v.weight.toFixed(3)}`).join(', '),
      );
    } catch (e) {
      setError(message(e));
    } finally {
      setBusy(false);
    }
  };

  const apply = async (request: Parameters<typeof runSkinEdit>[2]) => {
    if (block < 0 || busy) return;
    setBusy(true);
    setError('');
    try {
      const report = await runSkinEdit(nif, nifPath, request, expectedSource);
      if (!report.ok) {
        setError(report.refusal);
        appendLog(`✗ skin ${request.kind} refused -- ${report.refusal}`);
        return;
      }
      setNotice(describeSkinEditReport(report));
      appendLog(`✓ ${describeSkinEditReport(report)}`);
      onSaved(report.saved);
    } catch (e) {
      setError(message(e));
      appendLog(`✗ skin ${request.kind} failed -- ${message(e)}`);
    } finally {
      setBusy(false);
    }
  };

  const check = async () => {
    if (block < 0 || busy) return;
    setBusy(true);
    setError('');
    try {
      const report = await readSkinReport(nif, nifPath, block);
      setFindings(report.findings);
      setNotice(describeSkinReport(report));
      appendLog(`${report.ok ? '✓' : '✗'} ${describeSkinReport(report)}`);
    } catch (e) {
      setError(message(e));
    } finally {
      setBusy(false);
    }
  };

  const setRow = (index: number, next: Partial<SkinInfluence>) => {
    setInfluences(current => current.map((value, at) => (at === index ? { ...value, ...next } : value)));
  };

  const chosen = influences.filter(value => value.bone >= 0 && value.weight > 0);
  const idle = disabled || busy || block < 0;
  const copyOk = fromBone >= 0 && toBone >= 0 && fromBone !== toBone && fromBone < bones.length && toBone < bones.length;
  const bonesOk = bones.length > 1;

  return (
    <div className="nif-meshtools">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Shapes size={12} /> Skin tools</span>
        <select className="nif-in" value={block} disabled={disabled || busy} aria-label="Skin tools shape"
                onChange={e => { const next = Number(e.target.value); setBlock(next); void load(next, vertex); }}>
          <option value={-1}>Select a shape…</option>
          {shapes.map(s => <option key={s.id} value={s.id}>{s.id}: {s.name || '(unnamed)'}</option>)}
        </select>
        <input className="nif-in nif-num" type="number" min={0} step={1} value={vertex} disabled={disabled || busy}
               aria-label="Skin tools vertex" title="Vertex index inside the selected shape"
               onChange={e => setVertex(Number(e.target.value))} />
      </div>
      <EditorTabs ariaLabel="Skin tools" idPrefix="skin" tabs={TABS} active={tab}
                  onChange={id => setTab(id as SkinTab)} />

      <EditorTabPanel id="influences" idPrefix="skin" active={tab === 'influences'}>
        <div className="nif-editor-actions">
          <button className="sidebar-action-btn" disabled={idle} onClick={() => void load(block, vertex)}>
            Read influences
          </button>
          {influences.map((value, at) => (
            <span key={at} className="nif-vc-colour">
              <select className="nif-in" value={value.bone} disabled={idle} aria-label={`Influence ${at} bone`}
                      onChange={e => setRow(at, { bone: Number(e.target.value) })}>
                <option value={-1}>bone…</option>
                {bones.map((name, index) => <option key={index} value={index}>{name || `bone ${index}`}</option>)}
              </select>
              <input className="nif-in nif-num" type="number" min={0} max={1} step={0.05} value={value.weight}
                     disabled={idle} aria-label={`Influence ${at} weight`}
                     onChange={e => setRow(at, { weight: Number(e.target.value) })} />
            </span>
          ))}
          <button className="sidebar-action-btn" disabled={idle || chosen.length === 0}
                  onClick={() => void apply({
                    kind: 'set_influences', block, vertex,
                    influences: chosen.map(value => ({ bone: value.bone, weight: value.weight })),
                  })}>
            Set influences
          </button>
        </div>
      </EditorTabPanel>

      <EditorTabPanel id="bone" idPrefix="skin" active={tab === 'bone'}>
        <div className="nif-editor-actions">
          <select className="nif-in" value={boneIndex} disabled={idle || !bonesOk} aria-label="Skin tools bone"
                  onChange={e => setBoneIndex(Number(e.target.value))}>
            {bones.map((name, index) => <option key={index} value={index}>{name || `bone ${index}`}</option>)}
          </select>
          <input className="nif-in nif-num" type="number" min={0} max={1} step={0.01} value={minimum}
                 disabled={idle} aria-label="Skin tools minimum" title="Smallest weight the bone must carry"
                 onChange={e => setMinimum(Number(e.target.value))} />
          <button className="sidebar-action-btn" disabled={idle || !bonesOk}
                  onClick={() => void (async () => {
                    try {
                      const found = await readInfluencedBy(nif, nifPath, block, boneIndex, minimum);
                      setNotice(found.vertices.length === 0
                        ? `${found.boneName || `bone ${found.bone}`} moves no vertex at or above ${minimum}`
                        : `${found.boneName || `bone ${found.bone}`} moves ${found.vertices.length} vertices, ` +
                          `weight ${found.weight.toFixed(3)}, box ${found.min.map(v => v.toFixed(2)).join(', ')} .. ` +
                          found.max.map(v => v.toFixed(2)).join(', '));
                    } catch (e) {
                      setError(message(e));
                    }
                  })()}>
            Influenced by
          </button>
        </div>
        <div className="nif-editor-actions">
          <select className="nif-in" value={fromBone} disabled={idle || !bonesOk} aria-label="Copy weight from"
                  onChange={e => setFromBone(Number(e.target.value))}>
            {bones.map((name, index) => <option key={index} value={index}>{name || `bone ${index}`}</option>)}
          </select>
          <select className="nif-in" value={toBone} disabled={idle || !bonesOk} aria-label="Copy weight to"
                  onChange={e => setToBone(Number(e.target.value))}>
            {bones.map((name, index) => <option key={index} value={index}>{name || `bone ${index}`}</option>)}
          </select>
          <label className="nif-flabel">
            <input type="checkbox" checked={keepSource} disabled={idle}
                   onChange={e => setKeepSource(e.target.checked)} />
            keep source
          </label>
          <button className="sidebar-action-btn" disabled={idle || !copyOk}
                  onClick={() => void apply({ kind: 'copy_bone', block, fromBone, toBone, keepSource })}>
            Copy bone weight
          </button>
        </div>
      </EditorTabPanel>

      <EditorTabPanel id="mirror" idPrefix="skin" active={tab === 'mirror'}>
        <div className="nif-editor-actions">
          <select className="nif-in" value={lane} disabled={idle} aria-label="Mirror axis"
                  onChange={e => setLane(Number(e.target.value))}>
            <option value={0}>Mirror X</option>
            <option value={1}>Mirror Y</option>
            <option value={2}>Mirror Z</option>
          </select>
          <label className="nif-flabel">
            <input type="checkbox" checked={fromPositive} disabled={idle}
                   onChange={e => setFromPositive(e.target.checked)} />
            from positive side
          </label>
          <button className="sidebar-action-btn" disabled={idle}
                  onClick={() => void apply({ kind: 'mirror_weights', block, lane, fromPositive })}>
            Mirror weights
          </button>
        </div>
      </EditorTabPanel>

      <EditorTabPanel id="check" idPrefix="skin" active={tab === 'check'}>
        <div className="nif-editor-actions">
          <button className="sidebar-action-btn" disabled={idle} onClick={() => void check()}>
            Check weights
          </button>
        </div>
        {findings.length > 0 ? (
          <ul className="nif-flabel" role="list" aria-label="Skin findings">
            {findings.map((finding, at) => (
              <li key={at}>{finding.level === 'error' ? '✗' : '!'} {finding.where}: {finding.what}</li>
            ))}
          </ul>
        ) : null}
      </EditorTabPanel>

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}
