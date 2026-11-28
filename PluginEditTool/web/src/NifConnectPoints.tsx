import { useEffect, useRef, useState } from 'react';
import { Plus, RefreshCw, Trash2 } from 'lucide-react';
import type { NifConnectBlock, NifConnectChildrenBlock, NifConnectPoint, NifHost } from './backend';
import { errorText } from './errors';
import { CHILDREN, PARENTS, connectOps } from './NifConnectOps';
import { useDialogs } from './useDialogs';
import './NifConnectPoints.css';

export interface ConnectFocus { block: number; index: number; }

interface Props {
  nif: NifHost;
  nifPath: string;
  dirty: boolean;
  reloadKey?: number;
  onSaved?: () => void;
  onFocusPoint?: (focus: ConnectFocus | null) => void;
  onDirtyChange?: (dirty: boolean) => void;
}

function newPoint(): NifConnectPoint {
  return { parent: '', name: '', rotation: [1, 0, 0, 0], translation: [0, 0, 0], scale: 1 };
}

function replaceAt<T>(values: T[], index: number, value: T): T[] {
  return values.map((item, i) => (i === index ? value : item));
}

function clampIndex(index: number, count: number): number {
  return Math.max(0, Math.min(index, count - 1));
}

function allFinite(block: NifConnectBlock): boolean {
  if (block.type === CHILDREN) return true;
  return block.points.every(p => Number.isFinite(p.scale) && p.rotation.every(Number.isFinite) && p.translation.every(Number.isFinite));
}

function NumberField({ value, onChange, disabled }: { value: number; onChange: (value: number) => void; disabled: boolean }) {
  return (
    <input type="number" step="any" disabled={disabled} value={Number.isFinite(value) ? value : ''}
           onChange={e => onChange(e.target.value === '' ? NaN : Number(e.target.value))} />
  );
}

function PointRow({ point, index, disabled, onChange, onRemove, onFocus }: {
  point: NifConnectPoint;
  index: number;
  disabled: boolean;
  onChange: (patch: Partial<NifConnectPoint>) => void;
  onRemove: () => void;
  onFocus: () => void;
}) {
  return (
    <div className="nif-cp-point" onFocus={onFocus}>
      <div className="nif-cp-row">
        <label>Parent <input value={point.parent} disabled={disabled} placeholder="empty or WorkshopConnectPoints"
                             onChange={e => onChange({ parent: e.target.value })} /></label>
        <label>Name <input value={point.name} disabled={disabled} onChange={e => onChange({ name: e.target.value })} /></label>
        <button className="sidebar-action-btn" aria-label={`Remove point ${index + 1}`} disabled={disabled} onClick={onRemove}>
          <Trash2 size={12} />
        </button>
      </div>
      <div className="nif-cp-row">
        <span>Rotation w x y z</span>
        {point.rotation.map((v, k) => (
          <NumberField key={k} value={v} disabled={disabled} onChange={x => onChange({ rotation: replaceAt(point.rotation, k, x) })} />
        ))}
        {point.rotation.every(v => v === 0) && <span>connect point rotation must not be zero</span>}
      </div>
      <div className="nif-cp-row">
        <span>Translation x y z</span>
        {point.translation.map((v, k) => (
          <NumberField key={k} value={v} disabled={disabled} onChange={x => onChange({ translation: replaceAt(point.translation, k, x) })} />
        ))}
        <span>Scale</span>
        <NumberField value={point.scale} disabled={disabled} onChange={x => onChange({ scale: x })} />
      </div>
    </div>
  );
}

export default function NifConnectPoints({ nif, nifPath, dirty, reloadKey, onSaved, onFocusPoint, onDirtyChange }: Props) {
  const [blocks, setBlocks] = useState<NifConnectBlock[]>([]);
  const [selected, setSelected] = useState(0);
  const [draft, setDraft] = useState<NifConnectBlock | null>(null);
  const [status, setStatus] = useState('');
  const [busy, setBusy] = useState(false);
  const selectedRef = useRef(0);
  const reloadKeyRef = useRef(reloadKey);
  const loadSeq = useRef(0);
  const { confirm: askConfirm } = useDialogs();
  const isDraftDirty = () => {
    const orig = blocks[selectedRef.current];
    const d = draft;
    return !!(d && orig && connectOps(orig, d).length > 0);
  };
  const confirmDiscardDraft = async () => {
    if (!isDraftDirty()) return true;
    return askConfirm({
      title: 'Discard unsaved connect-point edits?',
      danger: true,
      confirmLabel: 'Discard',
      message: 'Your connect-point changes are not saved yet. Continuing will throw them away.',
    });
  };

  const load = async (index: number) => {
    const mySeq = ++loadSeq.current;
    setBusy(true);
    try {
      const raw = await nif.ConnectPoints(nifPath);
      if (mySeq !== loadSeq.current) return;
      const parsed = JSON.parse(raw) as NifConnectBlock[] | { error: string };
      if (!Array.isArray(parsed)) throw new Error(parsed.error);
      setBlocks(parsed);
      const picked = clampIndex(index, parsed.length);
      selectedRef.current = picked;
      setSelected(picked);
      setDraft(parsed[picked] ?? null);
      setStatus(parsed.length ? '' : 'No connect point blocks in this NIF.');
    } catch (e) {
      if (mySeq !== loadSeq.current) return;
      setBlocks([]);
      setDraft(null);
      setStatus('Could not read connect points: ' + errorText(e));
    } finally {
      if (mySeq === loadSeq.current) setBusy(false);
    }
  };

  const pathRef = useRef('');
  useEffect(() => {
    const pathChanged = nifPath !== pathRef.current;
    const keyChanged = reloadKey !== reloadKeyRef.current;
    if (!pathChanged && !keyChanged) return;
    let live = true;
    void (async () => {
      if (!await confirmDiscardDraft()) return;
      if (!live) return;
      pathRef.current = nifPath;
      reloadKeyRef.current = reloadKey;
      await load(selectedRef.current);
    })();
    return () => { live = false; };
  // eslint-disable-next-line react-hooks/exhaustive-deps -- reloadKey and nifPath trigger guarded reloads only
  }, [nif, nifPath, reloadKey]);

  const original = blocks[selected];
  const ops = draft && original ? connectOps(original, draft) : [];
  const draftDirty = ops.length > 0;

  useEffect(() => {
    onDirtyChange?.(draftDirty);
    return () => onDirtyChange?.(false);
  }, [draftDirty, onDirtyChange]);

  const pick = async (index: number) => {
    if (index === selected) return;
    if (!await confirmDiscardDraft()) return;
    selectedRef.current = index;
    setSelected(index);
    setDraft(blocks[index] ?? null);
    setStatus('');
    onFocusPoint?.(null);
  };

  const edit = (change: (block: NifConnectBlock) => NifConnectBlock) => setDraft(d => (d ? change(d) : d));

  const setPoint = (index: number, patch: Partial<NifConnectPoint>) => edit(b =>
    b.type === PARENTS ? { ...b, points: replaceAt(b.points, index, { ...b.points[index], ...patch }) } : b);

  const removePoint = (index: number) => {
    edit(b => (b.type === PARENTS ? { ...b, points: b.points.filter((_, k) => k !== index) } : b));
    onFocusPoint?.(null);
  };

  const addPoint = () => edit(b => (b.type === PARENTS ? { ...b, points: [...b.points, newPoint()] } : b));

  const setChildren = (patch: Partial<NifConnectChildrenBlock>) => edit(b =>
    (b.type === CHILDREN ? { ...b, ...patch } : b));

  const save = async () => {
    if (!draft || !original || ops.length === 0) return;
    if (!allFinite(draft)) {
      setStatus('Every number must be a finite value.');
      return;
    }
    setBusy(true);
    try {
      const out = await nif.ApplyConnectPoints(nifPath, JSON.stringify(ops), nifPath);
      if (!out.startsWith('connect points OK')) {
        setStatus(out);
        setBusy(false);
        return;
      }
      const message = `Saved ${ops.length} change(s).`;
      await load(selectedRef.current);
      onSaved?.();
      onFocusPoint?.(null);
      setStatus(message);
    } catch (e) {
      setStatus('Save failed: ' + errorText(e));
      setBusy(false);
    }
  };

  return (
    <section className="nif-cp">
      <div className="nif-cp-head">
        <span>Connect points</span>
        <button className="sidebar-action-btn" onClick={() => void (async () => {
          if (!await confirmDiscardDraft()) return;
          await load(selectedRef.current);
        })()} disabled={busy}>
          <RefreshCw size={12} /> Reload
        </button>
      </div>
      {blocks.length > 0 && (
        <div className="nif-cp-list">
          {blocks.map((b, i) => (
            <button key={b.block} className={`papyrus-mode ${i === selected ? 'active' : ''}`} disabled={busy} onClick={() => void pick(i)}>
              {b.name || `block ${b.block}`} · {b.type === PARENTS ? `${b.points.length} point(s)` : `${b.names.length} child(ren)`}
            </button>
          ))}
        </div>
      )}
      {draft?.type === PARENTS && (
        <div className="nif-cp-body">
          {draft.points.map((point, i) => (
            <PointRow key={i} point={point} index={i} disabled={busy}
                      onChange={patch => setPoint(i, patch)} onRemove={() => removePoint(i)}
                      onFocus={() => onFocusPoint?.({ block: draft.block, index: i })} />
          ))}
          <button className="sidebar-action-btn" disabled={busy} onClick={addPoint}><Plus size={12} /> Add point</button>
        </div>
      )}
      {draft?.type === CHILDREN && (
        <div className="nif-cp-body">
          <label><input type="checkbox" checked={draft.skinned} disabled={busy} onChange={e => setChildren({ skinned: e.target.checked })} /> Skinned</label>
          {draft.names.map((name, i) => (
            <div key={i} className="nif-cp-row">
              <input value={name} disabled={busy} onChange={e => setChildren({ names: replaceAt(draft.names, i, e.target.value) })} />
              <button className="sidebar-action-btn" aria-label={`Remove child ${i + 1}`} disabled={busy}
                      onClick={() => setChildren({ names: draft.names.filter((_, k) => k !== i) })}>
                <Trash2 size={12} />
              </button>
            </div>
          ))}
          <button className="sidebar-action-btn" disabled={busy} onClick={() => setChildren({ names: [...draft.names, ''] })}>
            <Plus size={12} /> Add child
          </button>
        </div>
      )}
      {draft && (
        <div className="nif-cp-actions">
          <button className="papyrus-run" onClick={() => void save()} disabled={busy || dirty || ops.length === 0 || (draft.type === PARENTS && draft.points.some(p => p.rotation.every(v => v === 0) || !p.rotation.every(Number.isFinite)))}>Save</button>
          {dirty && <span>Save or discard the tree edits first.</span>}
          {draftDirty && !dirty && <span>Unsaved connect-point edits.</span>}
        </div>
      )}
      {status && <div className="nif-cp-status">{status}</div>}
    </section>
  );
}
