import type { ScalarPresentation } from './backend';
import { flagIsSet, scalarLabel, setFlag } from './scalarPresentation';
import './ScalarEditor.css';

type EnumPresentation = Extract<ScalarPresentation, { kind: 'enum' }>;
type FlagNames = Extract<ScalarPresentation, { kind: 'flags' }>['names'];

function enumOptions(presentation: EnumPresentation, raw: string) {
  const known = presentation.names.some(e => String(e.value) === raw);
  return (
    <>
      {!known && <option value={raw}>{scalarLabel(raw, presentation)}</option>}
      {presentation.names.map(e => <option key={e.value} value={String(e.value)}>{e.name}</option>)}
    </>
  );
}

function FlagChecks({ names, value, disabled, onChange }: {
  names: FlagNames; value: string; disabled?: boolean; onChange: (next: string) => void;
}) {
  const raw = Number(value);
  return (
    <div className="scalar-flags">
      {names.map(e => (
        <label key={e.bit} className="scalar-flag">
          <input
            type="checkbox"
            checked={flagIsSet(raw, e.bit)}
            disabled={disabled}
            onChange={ev => onChange(String(setFlag(raw, e.bit, ev.target.checked)))}
          />
          {e.name}
        </label>
      ))}
    </div>
  );
}

export function ScalarText({ raw, presentation }: { raw: string; presentation?: ScalarPresentation }) {
  if (!presentation) return <>{raw}</>;
  return <span title={raw}>{scalarLabel(raw, presentation)}</span>;
}

export function ScalarEditor({ presentation, value, onValue, onCommit, onCancel }: {
  presentation: ScalarPresentation; value: string;
  onValue: (next: string) => void; onCommit: (next: string) => void; onCancel: () => void;
}) {
  if (presentation.kind === 'enum') {
    return (
      <select className="rv-edit-input" autoFocus value={value}
        onChange={e => onCommit(e.target.value)} onBlur={onCancel}>
        {enumOptions(presentation, value)}
      </select>
    );
  }
  return (
    <div className="scalar-flags-edit">
      <FlagChecks names={presentation.names} value={value} onChange={onValue} />
      <button onClick={() => onCommit(value)}>Save</button>
      <button onClick={onCancel}>Cancel</button>
    </div>
  );
}

export function ScalarCell({ presentation, value, disabled, onValue }: {
  presentation: ScalarPresentation; value: string; disabled: boolean; onValue: (next: string) => void;
}) {
  if (presentation.kind === 'enum') {
    return (
      <select className="spreadsheet-cell-input" value={value} disabled={disabled} title={value}
        onChange={e => onValue(e.target.value)}>
        {enumOptions(presentation, value)}
      </select>
    );
  }
  return (
    <details className="scalar-flags-cell" title={value}>
      <summary>{scalarLabel(value, presentation)}</summary>
      <FlagChecks names={presentation.names} value={value} disabled={disabled} onChange={onValue} />
    </details>
  );
}
