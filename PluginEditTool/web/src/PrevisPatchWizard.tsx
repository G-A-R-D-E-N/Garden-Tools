import { useEffect, useState } from 'react';
import { Wand2 } from 'lucide-react';
import { getPrevis, type PrevisPatchStep } from './backend';

type Props = {
  selectedCell?: string;
  selectedPlugin?: string;
  onLog: (msg: string) => void;
};

export default function PrevisPatchWizard({ selectedCell, selectedPlugin, onLog }: Props) {
  const [steps, setSteps] = useState<PrevisPatchStep[]>([]);
  const [patchPlugin, setPatchPlugin] = useState('xPrevisPatch.esp');
  const [cellKey, setCellKey] = useState(selectedCell ?? '');
  const [sourcePlugin, setSourcePlugin] = useState(selectedPlugin ?? '');
  const [pcmb, setPcmb] = useState('0');
  const [visi, setVisi] = useState('0');
  const [xcriJson, setXcriJson] = useState('{"mesh_ids":[],"refs":[]}');
  const [sourceNif, setSourceNif] = useState('');
  const [outputOc, setOutputOc] = useState('');
  const [containerKind, setContainerKind] = useState('cdx');
  const [containerSource, setContainerSource] = useState('');
  const [containerOutput, setContainerOutput] = useState('');
  const [busy, setBusy] = useState(false);

  useEffect(() => { setCellKey(selectedCell ?? ''); }, [selectedCell]);
  useEffect(() => { setSourcePlugin(selectedPlugin ?? ''); }, [selectedPlugin]);

  useEffect(() => {
    const previs = getPrevis();
    if (!previs?.PatchWorkflow) return;
    void previs.PatchWorkflow().then(setSteps).catch(() => setSteps([]));
  }, []);

  const run = async (label: string, fn: () => Promise<void>) => {
    setBusy(true);
    try {
      await fn();
    } catch (e) {
      onLog(`${label}: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setBusy(false);
    }
  };

  const previs = getPrevis();

  return (
    <div className="previs-wizard">
      <div className="previs-section-head"><Wand2 size={14} /> Patch selected cell (CK-free)</div>
      <p className="previs-hint">
        One patch plugin in the journal: copy override, stamp, bake measured _OC assets, save. Shared-geom encoding from placements is still in progress.
      </p>
      <div className="previs-wizard-grid">
        <label>Cell</label>
        <input className="previs-path-input" value={cellKey} onChange={e => setCellKey(e.target.value)} />
        <label>Patch plugin</label>
        <input className="previs-path-input" value={patchPlugin} onChange={e => setPatchPlugin(e.target.value)} />
        <label>Source plugin (optional)</label>
        <input className="previs-path-input" value={sourcePlugin} onChange={e => setSourcePlugin(e.target.value)} />
        <label>PCMB stamp</label>
        <input className="previs-path-input" value={pcmb} onChange={e => setPcmb(e.target.value)} placeholder="hex or decimal" />
        <label>VISI stamp</label>
        <input className="previs-path-input" value={visi} onChange={e => setVisi(e.target.value)} />
        <label>XCRI JSON</label>
        <input className="previs-path-input" value={xcriJson} onChange={e => setXcriJson(e.target.value)} />
        <label>Source _OC.NIF</label>
        <input className="previs-path-input" value={sourceNif} onChange={e => setSourceNif(e.target.value)} />
        <label>Output _OC path</label>
        <input className="previs-path-input" value={outputOc} onChange={e => setOutputOc(e.target.value)} />
        <label>Container kind</label>
        <select className="previs-path-input" value={containerKind} onChange={e => setContainerKind(e.target.value)}>
          <option value="cdx">cdx</option>
          <option value="csg">csg</option>
          <option value="uvd">uvd</option>
        </select>
        <label>Container source</label>
        <input className="previs-path-input" value={containerSource} onChange={e => setContainerSource(e.target.value)} />
        <label>Container output</label>
        <input className="previs-path-input" value={containerOutput} onChange={e => setContainerOutput(e.target.value)} />
      </div>
      <div className="previs-wizard-actions">
        <button className="papyrus-run" disabled={busy || !previs?.PatchDisable || !cellKey || !patchPlugin}
          onClick={() => run('Disable', async () => {
            const msg = await previs!.PatchDisable!(cellKey, patchPlugin, sourcePlugin, true);
            onLog(typeof msg === 'string' ? msg : JSON.stringify(msg));
          })}>
          Disable stale
        </button>
        <button className="papyrus-run" disabled={busy || !previs?.PatchBake || !sourceNif || !outputOc}
          onClick={() => run('Bake', async () => {
            const msg = await previs!.PatchBake!(sourceNif, outputOc);
            onLog(typeof msg === 'string' ? msg : JSON.stringify(msg));
          })}>
          Bake _OC
        </button>
        <button className="papyrus-run" disabled={busy || !previs?.PatchStamp || !cellKey || !patchPlugin}
          onClick={() => run('Stamp', async () => {
            const msg = await previs!.PatchStamp!(
              patchPlugin, cellKey, sourcePlugin, pcmb, visi, xcriJson, true);
            onLog(typeof msg === 'string' ? msg : JSON.stringify(msg));
          })}>
          Stamp CELL
        </button>
        <button className="papyrus-run" disabled={busy || !previs?.PatchWriteContainer || !containerSource || !containerOutput}
          onClick={() => run('Container', async () => {
            const msg = await previs!.PatchWriteContainer!(containerKind, containerSource, containerOutput);
            onLog(typeof msg === 'string' ? msg : JSON.stringify(msg));
          })}>
          Rewrite container
        </button>
        <button className="papyrus-run" disabled={busy || !previs?.PatchSave || !patchPlugin}
          onClick={() => run('Save', async () => {
            const msg = await previs!.PatchSave!(patchPlugin);
            onLog(typeof msg === 'string' ? msg : JSON.stringify(msg));
          })}>
          Save patch
        </button>
      </div>
      {steps.length > 0 && (
        <ol className="previs-wizard-steps">
          {steps.map(step => (
            <li key={step.id}>
              <strong>{step.title}</strong> — {step.detail}{' '}
              <span className={`previs-wizard-badge previs-wizard-badge-${step.status}`}>{step.status}</span>
            </li>
          ))}
        </ol>
      )}
    </div>
  );
}
