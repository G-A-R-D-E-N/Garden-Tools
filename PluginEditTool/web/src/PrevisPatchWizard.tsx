import { useEffect, useMemo, useRef, useState } from 'react';
import { Wand2 } from 'lucide-react';
import { getPrevis, type PrevisPatchStep } from './backend';
import { ocTemplateCandidates, type OcTemplate } from './previsOcTemplates';

const SCAN_DEBOUNCE_MS = 300;

type Props = {
  selectedCell?: string;
  selectedPlugin?: string;
  dataDir?: string;
  onLog: (msg: string) => void;
  onRescan?: () => Promise<void>;
};

export default function PrevisPatchWizard({ selectedCell, selectedPlugin, dataDir, onLog, onRescan }: Props) {
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
  const [planJson, setPlanJson] = useState<Record<string, unknown> | null>(null);
  const [planGroup, setPlanGroup] = useState('0');
  const [busy, setBusy] = useState(false);
  const [templates, setTemplates] = useState<OcTemplate[]>([]);
  const [templatesTruncated, setTemplatesTruncated] = useState(false);
  const autoFilledNif = useRef('');
  const userClearedTemplate = useRef(false);
  const sourceNifRef = useRef(sourceNif);
  useEffect(() => { sourceNifRef.current = sourceNif; }, [sourceNif]);
  const candidates = useMemo(
    () => ocTemplateCandidates(templates, patchPlugin, outputOc),
    [templates, patchPlugin, outputOc],
  );

  useEffect(() => {
    const previs = getPrevis();
    const cell = cellKey.trim();
    if (!previs?.ResolveOcTemplate || !cell) return;
    let cancelled = false;
    const timer = setTimeout(() => {
      void previs
        .ResolveOcTemplate!(cell, sourcePlugin.trim() || undefined, patchPlugin.trim() || undefined)
        .then(result => {
          if (cancelled) return;
          setTemplates(result.templates);
          setTemplatesTruncated(result.truncated);
          // Constraint: a patch's own _OC output must not be a template, and several candidates must leave the choice to the user.
          const foreign = ocTemplateCandidates(result.templates, patchPlugin, '');
          const pick = foreign.length === 1 ? foreign[0].path : '';
          const current = sourceNifRef.current;
          if (!userClearedTemplate.current && (!current.trim() || current === autoFilledNif.current)) {
            autoFilledNif.current = pick;
            setSourceNif(pick);
          }
          if (result.suggestedOutputOc) {
            setOutputOc(prev => (prev.trim() ? prev : result.suggestedOutputOc));
          }
          if (foreign.length > 0) {
            onLog(`Found ${foreign.length} _OC template(s) for cell prefix ${result.cellPrefix}.`);
          }
        })
        .catch(() => undefined);
    }, SCAN_DEBOUNCE_MS);
    return () => {
      cancelled = true;
      clearTimeout(timer);
    };
  }, [cellKey, sourcePlugin, patchPlugin, onLog]);

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

  const chooseTemplate = (path: string) => {
    userClearedTemplate.current = !path.trim();
    setSourceNif(path);
  };

  const planGroupIndex = () => {
    const group = Number.parseInt(planGroup, 10);
    return Number.isFinite(group) ? group : 0;
  };

  const modelPathFromPlan = (plan: Record<string, unknown> | null, groupIndex: number): string => {
    if (!plan || !Array.isArray(plan.groups)) return '';
    const group = plan.groups[groupIndex];
    if (!group || typeof group !== 'object') return '';
    const path = (group as { modelPath?: string }).modelPath;
    return typeof path === 'string' ? path.trim() : '';
  };

  const resolveStatMeshPath = (modelPath: string): string => {
    if (!modelPath || !dataDir?.trim()) return '';
    const root = dataDir.trim().replace(/[/\\]+$/, '');
    const relative = modelPath.replace(/^[/\\]+/, '').replace(/\//g, '\\');
    return `${root}\\${relative}`;
  };

  const patchBakeFromPlan = async (
    templateOc: string,
    plan: Record<string, unknown>,
    groupIndex: number,
  ) => {
    const modelPath = modelPathFromPlan(plan, groupIndex);
    const statMesh = resolveStatMeshPath(modelPath);
    if (statMesh && templateOc.trim()) {
      return previs!.PatchBake!(statMesh, outputOc, plan, groupIndex, templateOc.trim());
    }
    return previs!.PatchBake!(templateOc, outputOc, plan, groupIndex);
  };

  return (
    <div className="previs-wizard">
      <div className="previs-section-head"><Wand2 size={14} /> Patch selected cell (CK-free)</div>
      <p className="previs-hint">
        Generate previs runs the whole CK-free path in one call: plan, bake _OC, XCRI, PCMB/VISI and the
        patch's CDX row. The template is optional: leave it empty and generate uses the cell's own _OC from
        Data. Use the single-step buttons only to inspect one step; a manual stamp with an empty
        XCRI marks a cell precombined without pointing at any combined mesh.
      </p>
      <div className="previs-wizard-grid">
        <label>Cell</label>
        <input className="previs-path-input" value={cellKey} onChange={e => {
          userClearedTemplate.current = false;
          setCellKey(e.target.value);
        }} />
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
        <label>Template _OC.NIF</label>
        <input className="previs-path-input" value={sourceNif} onChange={e => chooseTemplate(e.target.value)}
          placeholder="Measured vanilla _OC with matching instance count" />
        {candidates.length > 0 && (
          <>
            <label htmlFor="previs-template-candidates">Template candidates</label>
            <select id="previs-template-candidates" className="previs-path-input"
              value={candidates.some(c => c.path === sourceNif) ? sourceNif : ''}
              onChange={e => chooseTemplate(e.target.value)}>
              <option value="">Generate picks the cell's own _OC from Data</option>
              {candidates.map(c => <option key={c.path} value={c.path}>{c.relative}</option>)}
            </select>
          </>
        )}
        <label>Plan group #</label>
        <input className="previs-path-input" value={planGroup} onChange={e => setPlanGroup(e.target.value)} />
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
      {templatesTruncated && <div className="previs-hint">The template list may be truncated; set the source plugin to narrow it.</div>}
      <div className="previs-wizard-actions">
        <button className="papyrus-run" disabled={busy || !previs?.PatchDisable || !cellKey || !patchPlugin}
          onClick={() => run('Disable', async () => {
            const msg = await previs!.PatchDisable!(cellKey, patchPlugin, sourcePlugin, true);
            onLog(typeof msg === 'string' ? msg : JSON.stringify(msg));
          })}>
          Disable stale
        </button>
        <button className="papyrus-run" disabled={busy || !previs?.PatchPlan || !cellKey || !sourcePlugin}
          onClick={() => run('Plan', async () => {
            const msg = await previs!.PatchPlan!(cellKey, sourcePlugin, 2);
            if (msg && typeof msg === 'object') setPlanJson(msg as Record<string, unknown>);
            onLog(typeof msg === 'string' ? msg : JSON.stringify(msg, null, 2));
          })}>
          Plan groups
        </button>
        <button className="papyrus-run" disabled={busy || !previs?.PatchBake || !sourceNif || !outputOc}
          onClick={() => run('Bake', async () => {
            const msg = await previs!.PatchBake!(sourceNif, outputOc);
            onLog(typeof msg === 'string' ? msg : JSON.stringify(msg));
          })}>
          Template copy only
        </button>
        <button className="papyrus-run"
          disabled={busy || !previs?.PatchBake || !sourceNif || !outputOc || !planJson}
          onClick={() => run('Bake plan', async () => {
            if (!planJson) throw new Error('Run Plan groups first.');
            const msg = await patchBakeFromPlan(sourceNif, planJson, planGroupIndex());
            onLog(typeof msg === 'string' ? msg : JSON.stringify(msg));
          })}>
          Bake placements
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
        <button className="papyrus-run" type="button" disabled={busy || !onRescan}
          onClick={() => run('Verify', async () => {
            await onRescan!();
            onLog('Re-scanned load order after patch (save plugins on disk first).');
          })}>
          Re-scan
        </button>
        <button className="papyrus-run" type="button"
          disabled={busy || !previs?.Generate || !cellKey || !patchPlugin}
          onClick={() => run('Generate', async () => {
            const result = await previs!.Generate!(
              patchPlugin.trim(), cellKey.trim(), sourcePlugin.trim() || undefined, sourceNif.trim(), 2, true);
            onLog(`Generated previs for ${result.cell} in ${result.plugin}: ${result.groups.length} _OC NIF(s), `
              + `${result.meshIds} mesh id(s), ${result.references} reference pair(s), stamp 0x${result.pcmbStamp.toString(16)}. `
              + 'Save patch to persist the CELL override.');
            if (result.cdx) onLog(`CDX row written to ${result.cdx.path} (${result.cdx.rows} row(s)).`);
            for (const warning of result.warnings ?? []) onLog(`Previs warning: ${warning}`);
            if (onRescan) await onRescan();
          })}>
          Generate previs
        </button>
        <button className="papyrus-run" type="button"
          disabled={busy || !previs?.Generate || !previs?.PatchSave || !cellKey || !patchPlugin}
          onClick={() => run('Run patch', async () => {
            // Constraint: only the one-call path pairs XCRI with the _OC files it wrote.
            const result = await previs!.Generate!(
              patchPlugin.trim(), cellKey.trim(), sourcePlugin.trim() || undefined, sourceNif.trim(), 2, true);
            onLog(`Generated previs for ${result.cell} in ${result.plugin}: ${result.groups.length} _OC NIF(s), `
              + `${result.meshIds} mesh id(s), ${result.references} reference pair(s), stamp 0x${result.pcmbStamp.toString(16)}.`);
            if (result.cdx) onLog(`CDX row written to ${result.cdx.path} (${result.cdx.rows} row(s)).`);
            if (result.shell) onLog(`_OC template ${result.shell.autoResolved ? 'resolved' : 'named'}: ${result.shell.path}`);
            for (const warning of result.warnings ?? []) onLog(`Previs warning: ${warning}`);
            const writeContainer = previs!.PatchWriteContainer;
            if (writeContainer && containerSource.trim() && containerOutput.trim()) {
              const containerMsg = await writeContainer(containerKind, containerSource, containerOutput);
              onLog(typeof containerMsg === 'string' ? containerMsg : JSON.stringify(containerMsg));
            }
            const saveMsg = await previs!.PatchSave!(patchPlugin);
            onLog(typeof saveMsg === 'string' ? saveMsg : JSON.stringify(saveMsg));
            if (onRescan) await onRescan();
          })}>
          Run patch
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
