import { useEffect, useState } from 'react';
import { SlidersHorizontal } from 'lucide-react';
import type { NifHost } from './backend';
import { EditorTabPanel, EditorTabs } from './Editors/EditorTabs';
import { readInfluences } from './skinToolsClient';
import {
  BRUSH_MODES, describeBrushReport, describeOsdApply, describeRemapRead, describeRemapValidation, describeTransferReport,
  createRemap, plural, readOsd, readOsp, readRemap, remapAction, runBrush, runOsdApply, runTransfer, transferArgs, validateRemap,
  type BrushFalloff, type BrushMode, type OsdSet, type OspSet, type RemapData, type TransferMethod,
} from './weightToolsClient';

const message = (e: unknown) => (e instanceof Error ? e.message : String(e));
const NIF_FILTER = 'NIF model (*.nif)|*.nif|All files|*.*';
const OSD_FILTER = 'Outfit Studio slider data (*.osd)|*.osd|All files|*.*';
const OSP_FILTER = 'Outfit Studio project (*.osp)|*.osp|All files|*.*';

type WeightTab = 'brush' | 'transfer' | 'remap' | 'sliders';

const TABS = [
  { id: 'brush', label: 'Brush' },
  { id: 'transfer', label: 'Transfer' },
  { id: 'remap', label: 'Remap' },
  { id: 'sliders', label: 'Sliders' },
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

export default function NifWeightTools(
  { nif, nifPath, shapes, disabled, expectedSource = '', appendLog, onSaved, onBusyChange }: Props,
) {
  const [tab, setTab] = useState<WeightTab>('brush');
  const [block, setBlock] = useState(-1);
  const [bones, setBones] = useState<string[]>([]);
  const [mode, setMode] = useState<BrushMode>('add');
  const [falloff, setFalloff] = useState<BrushFalloff>('smoothstep');
  const [centre, setCentre] = useState<[number, number, number]>([0, 0, 0]);
  const [radius, setRadius] = useState(5);
  const [strength, setStrength] = useState(0.5);
  const [target, setTarget] = useState(1);
  const [bone, setBone] = useState(0);
  const [locked, setLocked] = useState<number[]>([]);
  const [sourcePath, setSourcePath] = useState('');
  const [sourceShapes, setSourceShapes] = useState<{ id: number; name: string }[]>([]);
  const [sourceBlock, setSourceBlock] = useState(-1);
  const [method, setMethod] = useState<TransferMethod>('barycentric');
  const [maxDistance, setMaxDistance] = useState(0);
  const [remap, setRemap] = useState<RemapData | null>(null);
  const [problems, setProblems] = useState<string[]>([]);
  const [osdPath, setOsdPath] = useState('');
  const [osdSets, setOsdSets] = useState<OsdSet[]>([]);
  const [dataSet, setDataSet] = useState('');
  const [ospPath, setOspPath] = useState('');
  const [ospSets, setOspSets] = useState<OspSet[]>([]);
  const [setName, setSetName] = useState('');
  const [slider, setSlider] = useState('');
  const [weight, setWeight] = useState(1);
  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');

  useEffect(() => { onBusyChange?.(busy); }, [busy, onBusyChange]);

  const run = async (work: () => Promise<void>) => {
    if (busy) return;
    setBusy(true);
    setError('');
    try {
      await work();
    } catch (e) {
      setError(message(e));
      appendLog(`✗ weight tools -- ${message(e)}`);
    } finally {
      setBusy(false);
    }
  };

  const pickShape = (next: number) => {
    setBlock(next);
    setRemap(null);
    setProblems([]);
    setBones([]);
    if (next < 0) return;
    void run(async () => {
      const read = await readInfluences(nif, nifPath, next, 0);
      setBones(read.bones);
      setBone(value => (value < read.bones.length ? value : 0));
      setLocked(current => current.filter(index => index < read.bones.length));
    });
  };

  const finish = (saved: string, text: string) => {
    setNotice(text);
    appendLog(`${saved === '' ? '•' : '✓'} ${text}`);
    if (saved !== '') onSaved(saved);
  };

  const stroke = () => run(async () => {
    const settings = { mode, falloff, center: centre, radius, strength, bone, target, locked };
    const report = await runBrush(nif, nifPath, block, settings, expectedSource);
    finish(report.saved, describeBrushReport(report));
  });

  const browseSource = () => run(async () => {
    const picked = await nif.BrowseForFile('Select the NIF to copy weights from', NIF_FILTER);
    if (!picked) return;
    const tree = JSON.parse(await nif.Tree(picked)) as {
      blocks?: { id: number; name: string; group: string }[]; error?: string;
    };
    if (tree.error) throw new Error(tree.error);
    const found = (tree.blocks ?? []).filter(b => b.group === 'Shapes').map(b => ({ id: b.id, name: b.name }));
    setSourcePath(picked);
    setSourceShapes(found);
    setSourceBlock(found.length > 0 ? found[0].id : -1);
    setNotice(found.length === 0 ? 'the source NIF has no shapes' : '');
  });

  const transfer = () => run(async () => {
    const args = transferArgs(block, sourcePath, sourceBlock, method, maxDistance);
    const report = await runTransfer(nif, nifPath, args, expectedSource);
    finish(report.saved, describeTransferReport(report));
  });

  const loadRemap = () => run(async () => {
    const report = await readRemap(nif, nifPath, block);
    setRemap(report.remap);
    setProblems([]);
    setNotice(describeRemapRead(report.remap));
  });

  const checkRemap = () => run(async () => {
    const report = await validateRemap(nif, nifPath, block);
    setProblems(report.messages);
    setNotice(describeRemapValidation(report.messages));
  });

  const action = remapAction(remap);
  const saveRemapNow = () => run(async () => {
    if (action === null) return;
    const report = await createRemap(nif, nifPath, block, expectedSource);
    setRemap(null);
    setProblems([]);
    finish(report.saved, `remap created: ${report.records} records, ${report.newBones} new bones`);
  });

  const browseOsd = () => run(async () => {
    const picked = await nif.BrowseForFile('Select the Outfit Studio .osd slider data', OSD_FILTER);
    if (!picked) return;
    const report = await readOsd(nif, nifPath, picked);
    setOsdPath(picked);
    setOsdSets(report.osdSets);
    setDataSet(report.osdSets.length > 0 ? report.osdSets[0].name : '');
    setNotice(`${plural(report.osdSets.length, 'data set', 'data sets')} in the .osd`);
  });

  const browseOsp = () => run(async () => {
    const picked = await nif.BrowseForFile('Select the Outfit Studio .osp project', OSP_FILTER);
    if (!picked) return;
    const report = await readOsp(nif, nifPath, picked);
    setOspPath(picked);
    setOspSets(report.ospSets);
    setSetName(report.ospSets.length > 0 ? report.ospSets[0].name : '');
    setSlider('');
    setNotice(`${plural(report.ospSets.reduce((sum, set) => sum + set.sliders.length, 0), 'slider', 'sliders')} in the .osp`);
  });

  const applySlider = () => run(async () => {
    const choice = { osdPath, ospPath, setName, slider, dataSet, weight };
    const report = await runOsdApply(nif, nifPath, block, choice, expectedSource);
    finish(report.saved, describeOsdApply(report));
  });

  const sliders = ospSets.find(set => set.name === setName)?.sliders ?? [];
  const idle = disabled || busy || block < 0;
  const boneOptions = bones.map((name, index) => <option key={index} value={index}>{name || `bone ${index}`}</option>);
  const setAxis = (axis: number, value: number) => setCentre(current => {
    const next: [number, number, number] = [current[0], current[1], current[2]];
    next[axis] = value;
    return next;
  });

  return (
    <div className="nif-meshtools">
      <div className="nif-flags-head">
        <span className="nif-flabel"><SlidersHorizontal size={12} /> Weight tools</span>
        <select className="nif-in" value={block} disabled={disabled || busy} aria-label="Weight tools shape"
                onChange={e => pickShape(Number(e.target.value))}>
          <option value={-1}>Select a shape…</option>
          {shapes.map(s => <option key={s.id} value={s.id}>{s.id}: {s.name || '(unnamed)'}</option>)}
        </select>
      </div>
      <EditorTabs ariaLabel="Weight tools" idPrefix="weight" tabs={TABS} active={tab}
                  onChange={id => setTab(id as WeightTab)} />

      <EditorTabPanel id="brush" idPrefix="weight" active={tab === 'brush'}>
        <div className="nif-editor-actions">
          <select className="nif-in" value={mode} disabled={idle} aria-label="Brush mode"
                  onChange={e => setMode(e.target.value as BrushMode)}>
            {BRUSH_MODES.map(m => <option key={m.id} value={m.id}>{m.label}</option>)}
          </select>
          <select className="nif-in" value={falloff} disabled={idle} aria-label="Brush falloff"
                  onChange={e => setFalloff(e.target.value as BrushFalloff)}>
            <option value="smoothstep">Smooth falloff</option>
            <option value="linear">Linear falloff</option>
            <option value="constant">No falloff</option>
          </select>
          <select className="nif-in" value={bone} disabled={idle || mode === 'smooth' || bones.length === 0}
                  aria-label="Brush bone" onChange={e => setBone(Number(e.target.value))}>
            {boneOptions}
          </select>
        </div>
        <div className="nif-editor-actions">
          {['X', 'Y', 'Z'].map((axis, at) => (
            <input key={axis} className="nif-in nif-num" type="number" step={0.1} value={centre[at]} disabled={idle}
                   aria-label={`Brush centre ${axis}`} title={`Brush centre ${axis}, global NIF units`}
                   onChange={e => setAxis(at, Number(e.target.value))} />
          ))}
          <input className="nif-in nif-num" type="number" min={0} step={0.5} value={radius} disabled={idle}
                 aria-label="Brush radius" title="Brush radius, NIF units"
                 onChange={e => setRadius(Number(e.target.value))} />
          <input className="nif-in nif-num" type="number" min={0} step={0.05} value={strength} disabled={idle}
                 aria-label="Brush strength" title="Brush strength"
                 onChange={e => setStrength(Number(e.target.value))} />
          <input className="nif-in nif-num" type="number" min={0} max={1} step={0.05} value={target}
                 disabled={idle || mode !== 'replace'} aria-label="Replace target"
                 title="Weight Replace paints toward" onChange={e => setTarget(Number(e.target.value))} />
        </div>
        <div className="nif-editor-actions">
          {bones.map((name, index) => (
            <label key={index} className="nif-flabel">
              <input type="checkbox" checked={locked.includes(index)} disabled={idle}
                     aria-label={`Lock ${name || `bone ${index}`}`}
                     onChange={e => setLocked(current => (e.target.checked
                       ? [...current, index] : current.filter(value => value !== index)))} />
              {name || `bone ${index}`}
            </label>
          ))}
        </div>
        <div className="nif-editor-actions">
          <button className="sidebar-action-btn" disabled={idle} onClick={() => void stroke()}>Paint stroke</button>
        </div>
      </EditorTabPanel>

      <EditorTabPanel id="transfer" idPrefix="weight" active={tab === 'transfer'}>
        <div className="nif-editor-actions">
          <button className="sidebar-action-btn" disabled={disabled || busy} onClick={() => void browseSource()}>
            Choose source NIF
          </button>
          <span className="nif-flabel">{sourcePath || 'no source chosen'}</span>
          <select className="nif-in" value={sourceBlock} disabled={idle || sourceShapes.length === 0}
                  aria-label="Source shape" onChange={e => setSourceBlock(Number(e.target.value))}>
            {sourceShapes.map(s => <option key={s.id} value={s.id}>{s.id}: {s.name || '(unnamed)'}</option>)}
          </select>
        </div>
        <div className="nif-editor-actions">
          <select className="nif-in" value={method} disabled={idle} aria-label="Transfer method"
                  onChange={e => setMethod(e.target.value as TransferMethod)}>
            <option value="barycentric">Barycentric</option>
            <option value="nearest_vertex">Nearest vertex</option>
          </select>
          <input className="nif-in nif-num" type="number" min={0} step={0.5} value={maxDistance} disabled={idle}
                 aria-label="Transfer maximum distance" title="Farthest a target vertex may be from the source (0 = no limit)"
                 onChange={e => setMaxDistance(Number(e.target.value))} />
          <button className="sidebar-action-btn" disabled={idle || sourcePath === ''} onClick={() => void transfer()}>
            Transfer weights
          </button>
        </div>
      </EditorTabPanel>

      <EditorTabPanel id="remap" idPrefix="weight" active={tab === 'remap'}>
        <div className="nif-editor-actions">
          <button className="sidebar-action-btn" disabled={idle} onClick={() => void loadRemap()}>Read remap</button>
          <button className="sidebar-action-btn" disabled={idle || remap === null} onClick={() => void checkRemap()}>
            Validate remap
          </button>
          {action === null ? null : (
            <button className="sidebar-action-btn" disabled={idle} onClick={() => void saveRemapNow()}>
              Create remap from skin weights
            </button>
          )}
        </div>
        {problems.length > 0 ? (
          <ul className="nif-flabel" role="list" aria-label="Remap problems">
            {problems.map((problem, at) => <li key={at}>✗ {problem}</li>)}
          </ul>
        ) : null}
      </EditorTabPanel>

      <EditorTabPanel id="sliders" idPrefix="weight" active={tab === 'sliders'}>
        <div className="nif-editor-actions">
          <button className="sidebar-action-btn" disabled={disabled || busy} onClick={() => void browseOsd()}>
            Choose .osd
          </button>
          <span className="nif-flabel">{osdPath || 'no .osd chosen'}</span>
          <select className="nif-in" value={dataSet} disabled={idle || osdSets.length === 0 || slider !== ''}
                  aria-label="Slider data set" onChange={e => setDataSet(e.target.value)}>
            {osdSets.map(set => <option key={set.name} value={set.name}>{set.name} ({set.offsets})</option>)}
          </select>
        </div>
        <div className="nif-editor-actions">
          <button className="sidebar-action-btn" disabled={disabled || busy} onClick={() => void browseOsp()}>
            Choose .osp
          </button>
          <span className="nif-flabel">{ospPath || 'no .osp chosen (optional)'}</span>
          <select className="nif-in" value={setName} disabled={idle || ospSets.length === 0} aria-label="Outfit Studio set"
                  onChange={e => { setSetName(e.target.value); setSlider(''); }}>
            {ospSets.map(set => <option key={set.name} value={set.name}>{set.name}</option>)}
          </select>
          <select className="nif-in" value={slider} disabled={idle || sliders.length === 0} aria-label="Slider"
                  onChange={e => setSlider(e.target.value)}>
            <option value="">Apply a data set</option>
            {sliders.map(s => {
              const flagged = s.invert || s.zap || s.uv;
              return (
                <option key={s.name} value={s.name} disabled={flagged}>
                  {s.name}{flagged ? ' (invert, zap or uv: not applied)' : ''}
                </option>
              );
            })}
          </select>
        </div>
        <div className="nif-editor-actions">
          <input className="nif-in nif-num" type="number" min={0} max={1} step={0.05} value={weight} disabled={idle}
                 aria-label="Slider weight" title="Slider weight, 0 to 1; the diff is added to the shape positions"
                 onChange={e => setWeight(Number(e.target.value))} />
          <button className="sidebar-action-btn" disabled={idle || osdPath === ''} onClick={() => void applySlider()}>
            Apply slider
          </button>
        </div>
      </EditorTabPanel>

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}
