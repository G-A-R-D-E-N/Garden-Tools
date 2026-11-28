import './CellPatchPicker.css';

export default function CellPatchPicker({ patch, error, onChoose }: { patch: string; error?: string | null; onChoose: () => void }) {
  return (
    <div className="cell-patch">
      <span className="cell-patch-label">Patch plugin</span>
      <span className={`cell-patch-name${patch ? '' : ' cell-patch-none'}`} title={patch || undefined}>{patch || 'None chosen'}</span>
      <button className="papyrus-copy" onClick={onChoose}>{patch ? 'Change' : 'Choose'}</button>
      {error && <span className="cell-patch-error" role="alert">{error}</span>}
    </div>
  );
}

export function PatchChooserHint({ onChoose }: { onChoose?: () => void }) {
  return (
    <div className="cell-patch-hint">
      <span>Choose a patch plugin to edit</span>
      {onChoose && <button className="papyrus-copy" onClick={onChoose}>Choose patch plugin</button>}
    </div>
  );
}
