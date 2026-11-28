import { useEffect, useState } from 'react';
import { Activity } from 'lucide-react';
import type { HkxHost } from './backend';
import {
  blockedReason, changedLimits, describeJoint, limitFieldsOf, loadRagdoll, NO_SHAPE_EDITS, previewLimitsOf, runPreview, saveRagdoll,
  storedLimits, type LimitField, type LimitValues, type PoseBone, type PreviewReply, type RagdollView, type StagedShapeEdits,
} from './hkxRagdollClient';
import HkxRagdollShapeTools from './HkxRagdollShapeTools';
import { buildRagdollScene, type RagdollOverlays, type RagdollScene, type RagdollSelection } from './hkxRagdollScene';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

interface Props {
  host?: HkxHost;
  disabled: boolean;
  onScene?: (scene: RagdollScene | null) => void;
  poseBones?: PoseBone[] | null;
}

const NO_SELECTION: RagdollSelection = { body: null, constraint: null };

export default function HkxRagdollTools({ host, disabled, onScene, poseBones }: Props) {
  const [path, setPath] = useState('');
  const [loadedPath, setLoadedPath] = useState('');
  const [outPath, setOutPath] = useState('');
  const [view, setView] = useState<RagdollView | null>(null);
  const [selection, setSelection] = useState<RagdollSelection>(NO_SELECTION);
  const [draft, setDraft] = useState<Partial<Record<LimitField, string>>>({});
  const [previewOverrides, setPreviewOverrides] = useState<Record<number, LimitValues>>({});
  const [staged, setStaged] = useState<Record<number, LimitValues>>({});
  const [stagedShapes, setStagedShapes] = useState<StagedShapeEdits>(NO_SHAPE_EDITS);
  const [loadCount, setLoadCount] = useState(0);
  const [overlays, setOverlays] = useState<RagdollOverlays>({ skeleton: true, mesh: false, ragdoll: true });
  const [usePose, setUsePose] = useState(false);
  const [preview, setPreview] = useState<PreviewReply | null>(null);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const guarded = async (work: () => Promise<void>) => {
    setBusy(true);
    setError('');
    try {
      await work();
    } catch (e) {
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  useEffect(() => {
    onScene?.(buildRagdollScene(view, preview, overlays, selection, loadedPath));
  }, [view, preview, overlays, selection, loadedPath, onScene]);

  const load = () => guarded(async () => {
    if (!host) return;
    const loaded = await loadRagdoll(host, path.trim());
    setView(loaded);
    setLoadedPath(path.trim());
    setSelection(NO_SELECTION);
    setPreview(null);
    setPreviewOverrides({});
    setStaged({});
    setStagedShapes(NO_SHAPE_EDITS);
    setLoadCount(count => count + 1);
    setNotice(`${loaded.bodies.length} bodies, ${loaded.constraints.length} constraints`);
  });

  const selectConstraint = (id: number) => {
    const row = view?.constraints.find(c => c.id === id);
    if (!row) return;
    // Invariant: the draft starts from stored and staged limits only, so a preview override is never re-staged by selecting again.
    const shown = { ...storedLimits(row), ...staged[id] };
    const next: Partial<Record<LimitField, string>> = {};
    for (const field of limitFieldsOf(row)) next[field] = shown[field] === undefined ? '' : String(shown[field]);
    setSelection({ body: null, constraint: id });
    setDraft(next);
  };

  const draftValues = (): LimitValues => {
    const values: LimitValues = {};
    for (const [field, text] of Object.entries(draft) as [LimitField, string][]) {
      if (row && blockedReason(row, field)) continue;
      if (text.trim() !== '' && Number.isFinite(Number(text))) values[field] = Number(text);
    }
    return values;
  };

  const toggle = (key: keyof RagdollOverlays) => setOverlays({ ...overlays, [key]: !overlays[key] });

  const row = view?.constraints.find(c => c.id === selection.constraint) ?? null;
  const rowFields = row ? limitFieldsOf(row) : [];
  const poseCount = poseBones?.length ?? 0;
  const poseMismatch = poseCount > 0 && view?.poseBoneCount != null && view.poseBoneCount !== poseCount;
  const canPose = poseCount > 0 && !poseMismatch;
  const rowOverride = row ? previewOverrides[row.id] : undefined;
  const stagedCount = Object.keys(staged).length + Object.keys(stagedShapes.capsules).length +
    Object.keys(stagedShapes.materials).length;
  const overrideCount = Object.keys(previewOverrides).length;

  return (
    <div className="nif-flags">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Activity size={12} /> Ragdoll limits and preview</span>
        <input className="nif-in" value={path} disabled={disabled || busy} aria-label="Ragdoll path"
               placeholder="Path to a ragdoll .hkx" onChange={e => setPath(e.target.value)} />
        <button className="sidebar-action-btn" disabled={disabled || busy || !path.trim()} onClick={() => void load()}>
          Load ragdoll
        </button>
      </div>

      <div className="nif-flabel" role="group" aria-label="Overlays">
        {(['skeleton', 'mesh', 'ragdoll'] as const).map(key => (
          <label key={key} title={key === 'mesh' ? 'The HKX viewport has no mesh data to draw' : undefined}>
            <input type="checkbox" checked={overlays[key]} disabled={key === 'mesh'}
                   onChange={() => toggle(key)} /> {key} overlay
          </label>
        ))}
      </div>

      {view ? (
        <>
          <ul className="nif-flabel" role="list" aria-label="Ragdoll bodies">
            {view.bodies.map(body => (
              <li key={body.id}>
                <button className="sidebar-action-btn" aria-pressed={selection.body === body.id}
                        onClick={() => setSelection({ body: body.id, constraint: null })}>
                  body {body.id} {body.name.trim()}
                </button>
              </li>
            ))}
          </ul>
          <ul className="nif-flabel" role="list" aria-label="Ragdoll constraints">
            {view.constraints.map(c => (
              <li key={c.id}>
                <button className="sidebar-action-btn" aria-pressed={selection.constraint === c.id}
                        onClick={() => selectConstraint(c.id)}>
                  constraint {c.id} ({c.kind}) {c.body_a} to {c.body_b}
                </button>
              </li>
            ))}
          </ul>
        </>
      ) : null}

      {view ? <HkxRagdollShapeTools key={loadCount} view={view} staged={stagedShapes} busy={busy} onStage={setStagedShapes} /> : null}

      {row ? (
        <div className="nif-flabel" role="group" aria-label="Limit editing">
          {rowFields.length === 0 ? <span>constraint {row.id} has no editable limits</span> : null}
          {rowOverride ? (
            <span role="note" aria-label="Preview override values">
              preview override: {Object.entries(rowOverride).map(([field, value]) => `${field} ${value}`).join(', ')}
            </span>
          ) : null}
          {rowFields.map(field => {
            const blocked = blockedReason(row, field);
            return (
              <label key={field} title={field === 'friction' ? 'angular friction maximum torque; its unit is not confirmed' : undefined}>
                {field}
                <input className="nif-in" type="number" step="0.01" aria-label={field} value={draft[field] ?? ''}
                       disabled={blocked !== null} title={blocked ?? undefined}
                       onChange={e => setDraft({ ...draft, [field]: e.target.value })} />
                {blocked ? <span className="papyrus-warn" role="note" aria-label={`${field} read only`}>{field} is read only: {blocked}</span> : null}
              </label>
            );
          })}
          <button className="sidebar-action-btn" disabled={busy || rowFields.length === 0}
                  onClick={() => {
                    const override = previewLimitsOf(draftValues());
                    if (Object.keys(override).length > 0) setPreviewOverrides({ ...previewOverrides, [row.id]: override });
                  }}>
            Preview limit
          </button>
          <button className="sidebar-action-btn" disabled={busy || rowFields.length === 0}
                  onClick={() => {
                    const changed = changedLimits(row, draftValues(), staged[row.id]);
                    if (Object.keys(changed).length > 0) setStaged({ ...staged, [row.id]: changed });
                  }}>
            Stage edit
          </button>
        </div>
      ) : null}

      {view ? (
        <div className="nif-flabel" role="group" aria-label="Preview and save">
          <span role="note" aria-label="Edit counts">{overrideCount} preview override(s), {stagedCount} staged edit(s)</span>
          <button className="sidebar-action-btn" disabled={disabled || busy}
                  onClick={() => void guarded(async () => {
                    if (!host) return;
                    const result = await runPreview(host, path.trim(), previewOverrides, 120, 24,
                                                    usePose && canPose ? poseBones ?? undefined : undefined);
                    setPreview(result);
                    setNotice(`preview ran ${result.steps} steps; nothing was saved`);
                  })}>
            Run preview
          </button>
          <label>
            <input type="checkbox" checked={usePose && canPose} disabled={!canPose} aria-label="Pose from loaded clip"
                   onChange={() => setUsePose(!usePose)} /> pose from loaded clip
          </label>
          {poseMismatch ? (
            <span className="papyrus-warn" role="note" aria-label="Pose mismatch">
              the loaded clip has {poseCount} bones but this ragdoll expects {view?.poseBoneCount}, so it cannot pose it
            </span>
          ) : null}
          <button className="sidebar-action-btn" disabled={busy}
                  onClick={() => { setPreviewOverrides({}); setPreview(null); }}>
            Recover
          </button>
          <input className="nif-in" value={outPath} disabled={disabled || busy} aria-label="Save as path"
                 placeholder="Save the staged edits to a new .hkx" onChange={e => setOutPath(e.target.value)} />
          <button className="sidebar-action-btn" disabled={disabled || busy || !outPath.trim() || stagedCount === 0}
                  onClick={() => void guarded(async () => {
                    if (!host) return;
                    const saved = await saveRagdoll(host, path.trim(), outPath.trim(), staged, false, stagedShapes);
                    setNotice(`saved ${saved.path}`);
                  })}>
            Save as
          </button>
        </div>
      ) : null}

      {preview ? (
        <ul className="nif-flabel" role="list" aria-label="Preview joints">
          {preview.constraints.map(joint => (
            <li key={joint.constraint} className={joint.violation > 1e-3 ? 'papyrus-warn' : undefined}>
              {describeJoint(joint)}{joint.overridden ? ' (preview override)' : ''}
            </li>
          ))}
        </ul>
      ) : null}

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}
