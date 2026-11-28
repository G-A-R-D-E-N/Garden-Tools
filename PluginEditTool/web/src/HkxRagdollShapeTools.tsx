import { useState } from 'react';
import {
  capsuleLength, MATERIAL_FIELDS, resizedEndpoints,
  type MaterialField, type MaterialValues, type RagdollView, type StagedShapeEdits,
} from './hkxRagdollClient';

interface Props {
  view: RagdollView;
  staged: StagedShapeEdits;
  busy: boolean;
  onStage: (next: StagedShapeEdits) => void;
}

const numberOrNull = (text: string) => (text.trim() !== '' && Number.isFinite(Number(text)) ? Number(text) : null);

export default function HkxRagdollShapeTools({ view, staged, busy, onStage }: Props) {
  const [capsule, setCapsule] = useState<number | null>(null);
  const [length, setLength] = useState('');
  const [radius, setRadius] = useState('');
  const [material, setMaterial] = useState<number | null>(null);
  const [friction, setFriction] = useState<Partial<Record<MaterialField, string>>>({});
  const [allowShared, setAllowShared] = useState(false);

  const capsuleRow = view.capsules?.find(c => c.shape === capsule) ?? null;
  const materialRow = view.materials?.find(m => m.material === material) ?? null;

  const pickCapsule = (shape: number) => {
    const row = view.capsules?.find(c => c.shape === shape);
    if (!row) return;
    setCapsule(shape);
    setLength(String(capsuleLength(row)));
    setRadius(String(row.radius));
  };

  const pickMaterial = (index: number) => {
    const row = view.materials?.find(m => m.material === index);
    if (!row) return;
    setMaterial(index);
    setAllowShared(false);
    const next: Partial<Record<MaterialField, string>> = {};
    for (const field of MATERIAL_FIELDS) next[field] = String(row[field]);
    setFriction(next);
  };

  const stageCapsule = () => {
    if (!capsuleRow) return;
    const newLength = numberOrNull(length);
    const newRadius = numberOrNull(radius);
    const values: StagedShapeEdits['capsules'][number] = {};
    if (newLength !== null && newLength !== capsuleLength(capsuleRow)) Object.assign(values, resizedEndpoints(capsuleRow, newLength));
    if (newRadius !== null && newRadius !== capsuleRow.radius) values.radius = newRadius;
    const capsules = { ...staged.capsules };
    delete capsules[capsuleRow.shape];
    if (Object.keys(values).length > 0) capsules[capsuleRow.shape] = values;
    onStage({ ...staged, capsules });
  };

  const stageMaterial = () => {
    if (!materialRow) return;
    const values: MaterialValues = {};
    for (const field of MATERIAL_FIELDS) {
      const value = numberOrNull(friction[field] ?? '');
      if (value !== null && value !== materialRow[field]) values[field] = value;
    }
    const materials = { ...staged.materials };
    delete materials[materialRow.material];
    if (Object.keys(values).length > 0) materials[materialRow.material] = allowShared ? { ...values, allow_shared: true } : values;
    onStage({ ...staged, materials });
  };

  if (!view.capsules?.length && !view.materials?.length) return null;
  return (
    <div className="nif-flabel" role="group" aria-label="Shape and material editing">
      {view.capsules?.length ? (
        <ul role="list" aria-label="Ragdoll capsules">
          {view.capsules.map(c => (
            <li key={c.shape}>
              <button className="sidebar-action-btn" aria-pressed={capsule === c.shape} onClick={() => pickCapsule(c.shape)}>
                capsule {c.shape} length {capsuleLength(c).toFixed(3)} radius {c.radius.toFixed(3)}
              </button>
              {c.editable ? null : <span className="papyrus-warn" role="note"> not editable: {c.reason}</span>}
            </li>
          ))}
        </ul>
      ) : null}
      {capsuleRow?.editable ? (
        <span role="group" aria-label="Capsule editing">
          <label>
            capsule length
            <input className="nif-in" type="number" step="0.01" aria-label="capsule length" value={length}
                   onChange={e => setLength(e.target.value)} />
          </label>
          <label>
            capsule radius
            <input className="nif-in" type="number" step="0.01" aria-label="capsule radius" value={radius}
                   onChange={e => setRadius(e.target.value)} />
          </label>
          <button className="sidebar-action-btn" disabled={busy} onClick={stageCapsule}>Stage capsule</button>
        </span>
      ) : null}
      {view.materials?.length ? (
        <ul role="list" aria-label="Ragdoll materials">
          {view.materials.map(m => (
            <li key={m.material}>
              <button className="sidebar-action-btn" aria-pressed={material === m.material} onClick={() => pickMaterial(m.material)}>
                material {m.material}{m.shared ? ' (shared)' : ''}
              </button>
            </li>
          ))}
        </ul>
      ) : null}
      {materialRow ? (
        <span role="group" aria-label="Material editing">
          {MATERIAL_FIELDS.map(field => (
            <label key={field}>
              {field}
              <input className="nif-in" type="number" step="0.01" aria-label={field} value={friction[field] ?? ''}
                     onChange={e => setFriction({ ...friction, [field]: e.target.value })} />
            </label>
          ))}
          {materialRow.shared ? (
            <label>
              <input type="checkbox" checked={allowShared} aria-label="Change every body that uses this material"
                     onChange={() => setAllowShared(!allowShared)} /> change every body that uses this material
            </label>
          ) : null}
          <button className="sidebar-action-btn" disabled={busy} onClick={stageMaterial}>Stage material</button>
        </span>
      ) : null}
    </div>
  );
}
