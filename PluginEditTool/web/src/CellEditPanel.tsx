import { useRef } from 'react';
import type { CellEditChanges, CellPlacedReference, CellTransform } from './backend';

type TransformField = keyof CellTransform;
type Field = TransformField | 'scale' | 'editorId';

const TRANSFORM_FIELDS: { field: TransformField; label: string }[] = [
  { field: 'x', label: 'Pos X' }, { field: 'y', label: 'Pos Y' }, { field: 'z', label: 'Pos Z' },
  { field: 'rx', label: 'Rot X' }, { field: 'ry', label: 'Rot Y' }, { field: 'rz', label: 'Rot Z' },
];

const transformOf = (r: CellPlacedReference): CellTransform => ({
  x: r.position.x, y: r.position.y, z: r.position.z, rx: r.rotation.x, ry: r.rotation.y, rz: r.rotation.z,
});

function changesFor(reference: CellPlacedReference, field: Field, text: string): CellEditChanges | null {
  if (field === 'editorId') {
    const editorId = text.trim();
    return editorId === '' || editorId === (reference.editorId ?? '') ? null : { editorId };
  }
  const value = Number(text);
  if (text.trim() === '' || !Number.isFinite(value)) return null;
  if (field === 'scale') return value === reference.scale ? null : { scale: value };
  if (value === transformOf(reference)[field]) return null;
  return { transform: { ...transformOf(reference), [field]: value } };
}

export default function CellEditPanel({ reference, canUndo, canRedo, onEdit, onDelete, onDuplicate, onUndoEdit, onRedoEdit }: {
  reference: CellPlacedReference;
  canUndo: boolean;
  canRedo: boolean;
  onEdit: (changes: CellEditChanges) => Promise<boolean>;
  onDelete: () => void;
  onDuplicate: () => void;
  onUndoEdit: () => void;
  onRedoEdit: () => void;
}) {
  const pending = useRef(new Set<Field>());

  const commit = async (field: Field, input: HTMLInputElement, stored: string) => {
    if (pending.current.has(field)) return;
    const changes = changesFor(reference, field, input.value);
    if (!changes) return;
    pending.current.add(field);
    try {
      if (!(await onEdit(changes))) input.value = stored;
    } finally {
      pending.current.delete(field);
    }
  };

  const renderField = (field: Field, label: string, value: string, type: 'number' | 'text') => (
    <label className="cell-edit-field" key={field}>
      <span>{label}</span>
      <input
        key={`${reference.formKey}:${value}`}
        type={type}
        step={type === 'number' ? 'any' : undefined}
        defaultValue={value}
        onBlur={e => void commit(field, e.currentTarget, value)}
        onKeyDown={e => { if (e.key === 'Enter') void commit(field, e.currentTarget, value); }}
      />
    </label>
  );

  const transform = transformOf(reference);
  return (
    <div className="cell-edit">
      <div className="cell-edit-fields">
        {TRANSFORM_FIELDS.map(({ field, label }) => renderField(field, label, String(transform[field]), 'number'))}
        {renderField('scale', 'Scale', String(reference.scale), 'number')}
        {renderField('editorId', 'EditorID', reference.editorId ?? '', 'text')}
      </div>
      <div className="cell-hidden-actions">
        <button className="papyrus-copy" onClick={onDuplicate} title="Duplicate the reference (Ctrl+D)">
          Duplicate <kbd>Ctrl+D</kbd>
        </button>
        <button className="papyrus-copy" onClick={onDelete} title="Delete the reference (Delete)">
          Delete <kbd>Del</kbd>
        </button>
        <button className="papyrus-copy" onClick={onUndoEdit} disabled={!canUndo} title="Undo the last reference edit">
          Undo edit <kbd>Ctrl+Z</kbd>
        </button>
        <button className="papyrus-copy" onClick={onRedoEdit} disabled={!canRedo} title="Redo the last undone reference edit">
          Redo edit <kbd>Ctrl+Y</kbd>
        </button>
      </div>
    </div>
  );
}
