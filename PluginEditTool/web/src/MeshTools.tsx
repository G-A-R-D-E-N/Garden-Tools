import { forwardRef, useEffect, useImperativeHandle, useRef, useState } from 'react';
import { Wrench } from 'lucide-react';
import type { NifHost } from './backend';
import {
  MESH_OPS, describeShapeOpReport, replayMeshOps, runMeshOp, stagingPathFor, type MeshOp,
} from './meshOpsClient';

export interface MeshToolsHandle {
  hasPending(): boolean;
  busy(): boolean;
  commit(): Promise<{ ok: boolean; text: string }>;
  reset(): void;
}

interface Props {
  host: NifHost;
  nifPath: string;
  shapes: { id: number; name: string }[];
  appendLog: (line: string) => void;
  onDirtyChange: (dirty: boolean) => void;
  onBusyChange: (busy: boolean) => void;
}

// Invariant: every op is staged into its own file, so the open NIF stays untouched until commit.
const MeshTools = forwardRef<MeshToolsHandle, Props>(function MeshTools(
  { host, nifPath, shapes, appendLog, onDirtyChange, onBusyChange }, ref,
) {
  const [block, setBlock] = useState(-1);
  const [ratio, setRatio] = useState(0.5);
  const [maxInfluences, setMaxInfluences] = useState(4);
  const [pruneThreshold, setPruneThreshold] = useState(0.05);
  const [copySource, setCopySource] = useState(-1);
  const [ops, setOps] = useState<MeshOp[]>([]);
  // Invariant: the handle reads the queue from a ref, so a caller never sees a stale closure after commit.
  const opsRef = useRef<MeshOp[]>([]);
  const updateOps = (next: MeshOp[]) => {
    opsRef.current = next;
    setOps(next);
  };
  const [busy, setBusy] = useState(false);
  const pending = ops.length > 0;
  const idle = block < 0 || busy;
  const sources = shapes.filter(s => s.id !== block);
  const source = sources.some(s => s.id === copySource) ? copySource : -1;
  const influencesOk = Number.isInteger(maxInfluences) && maxInfluences >= 1 && maxInfluences <= 4;
  const pruneOk = pruneThreshold > 0 && pruneThreshold < 1;

  useEffect(() => { onBusyChange(busy); }, [busy, onBusyChange]);

  useImperativeHandle(ref, () => ({
    hasPending: () => opsRef.current.length > 0,
    busy: () => busy,
    reset: () => {
      updateOps([]);
      onDirtyChange(false);
    },
    commit: async () => {
      const queued = opsRef.current;
      if (busy) return { ok: false, text: 'a mesh op is still running' };
      if (queued.length === 0) return { ok: true, text: '' };
      setBusy(true);
      try {
        const replayed = await replayMeshOps(host, nifPath, block, queued, nifPath);
        if (!replayed.ok) {
          appendLog(`✗ mesh tools save refused -- ${replayed.text}`);
          return { ok: false, text: replayed.text };
        }
        appendLog(`✓ saved ${queued.length} mesh op(s)`);
        updateOps([]);
        onDirtyChange(false);
        return { ok: true, text: replayed.text };
      } catch (e) {
        const message = e instanceof Error ? e.message : String(e);
        appendLog(`✗ mesh tools save failed -- ${message}`);
        return { ok: false, text: message };
      } finally {
        setBusy(false);
      }
    },
  }), [appendLog, block, busy, host, nifPath, onDirtyChange, ops]);

  const run = async (op: MeshOp) => {
    if (idle) return;
    setBusy(true);
    try {
      const input = pending ? stagingPathFor(nifPath) : nifPath;
      const report = await runMeshOp(host, input, stagingPathFor(nifPath), block, op);
      if (!report.ok) {
        appendLog(`✗ ${op.label} refused -- ${report.refusal}`);
        return;
      }
      if (report.changed === 'none') {
        const skipped = report.skipped && report.skipped !== 'none' ? ` (skipped ${report.skipped})` : '';
        appendLog(`• ${op.label}: nothing to change on block ${block}${skipped}`);
        return;
      }
      appendLog(`✓ ${describeShapeOpReport(report)}`);
      updateOps([...opsRef.current, op]);
      onDirtyChange(true);
    } catch (e) {
      appendLog(`✗ ${op.label}: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setBusy(false);
    }
  };

  const undo = async () => {
    if (busy || !pending) return;
    const rest = ops.slice(0, -1);
    setBusy(true);
    try {
      if (rest.length === 0) {
        updateOps([]);
        onDirtyChange(false);
        appendLog('↶ mesh tools: back to the saved file');
        return;
      }
      const replayed = await replayMeshOps(host, nifPath, block, rest, stagingPathFor(nifPath));
      if (!replayed.ok) {
        appendLog(`✗ mesh tools undo refused -- ${replayed.text}`);
        return;
      }
      updateOps(rest);
      onDirtyChange(true);
      appendLog(`↶ mesh tools: ${rest.length} op(s) left to save`);
    } catch (e) {
      appendLog(`✗ mesh tools undo failed -- ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="nif-meshtools">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Wrench size={12} /> Mesh tools</span>
        <select className="nif-in" value={block} disabled={busy || pending} aria-label="Mesh tools shape"
                onChange={e => setBlock(Number(e.target.value))}>
          <option value={-1}>Select a shape…</option>
          {shapes.map(s => <option key={s.id} value={s.id}>{s.id}: {s.name || '(unnamed)'}</option>)}
        </select>
      </div>
      <div className="nif-editor-actions">
        <button className="sidebar-action-btn" disabled={idle} onClick={() => run(MESH_OPS.weld)}>
          Weld vertices
        </button>
        <button className="sidebar-action-btn" disabled={idle} onClick={() => run(MESH_OPS.degenerate)}>
          Remove degenerate
        </button>
        <button className="sidebar-action-btn" disabled={idle} onClick={() => run(MESH_OPS.unused)}>
          Remove unused
        </button>
        <button className="sidebar-action-btn" disabled={idle || !(ratio > 0 && ratio < 1)}
                onClick={() => run({ ...MESH_OPS.simplify, ratio })}>
          Simplify
        </button>
        <input className="nif-in nif-num" type="number" min={0.05} max={0.95} step={0.05} value={ratio}
               disabled={busy} title="Simplify: target fraction of the triangles"
               onChange={e => setRatio(Number(e.target.value))} />
        <button className="sidebar-action-btn" disabled={idle} onClick={() => run(MESH_OPS.sanitize)}>
          Sanitize normals
        </button>
        <button className="sidebar-action-btn" disabled={idle} onClick={() => run(MESH_OPS.normalize)}>
          Normalize weights
        </button>
        <button className="sidebar-action-btn" disabled={idle || !influencesOk}
                onClick={() => run({ ...MESH_OPS.limit, maxInfluences })}>
          Limit influences
        </button>
        <input className="nif-in nif-num" type="number" min={1} max={4} step={1} value={maxInfluences}
               disabled={busy} title="Limit influences: most bones a vertex keeps"
               onChange={e => setMaxInfluences(Number(e.target.value))} />
        <button className="sidebar-action-btn" disabled={idle || !pruneOk}
                onClick={() => run({ ...MESH_OPS.prune, pruneThreshold })}>
          Prune weights
        </button>
        <input className="nif-in nif-num" type="number" min={0} max={0.95} step={0.01} value={pruneThreshold}
               disabled={busy} title="Prune weights: weights below this are dropped"
               onChange={e => setPruneThreshold(Number(e.target.value))} />
        <select className="nif-in" value={source} disabled={busy} title="Copy weights: source shape"
                onChange={e => setCopySource(Number(e.target.value))}>
          <option value={-1}>Copy from…</option>
          {sources.map(s => <option key={s.id} value={s.id}>{s.id}: {s.name || '(unnamed)'}</option>)}
        </select>
        <button className="sidebar-action-btn" disabled={idle || source < 0}
                onClick={() => run({ ...MESH_OPS.copy, sourceBlock: source })}>
          Copy weights
        </button>
        <button className="sidebar-action-btn" disabled={!pending || busy} onClick={undo}
                title="Undo the last mesh op">
          Undo op
        </button>
      </div>
      <span className={`nif-dirty-count ${pending ? 'on' : ''}`}>
        {pending ? `${ops.length} mesh op${ops.length > 1 ? 's' : ''} pending save` : 'No mesh changes'}
      </span>
    </div>
  );
});

export default MeshTools;
