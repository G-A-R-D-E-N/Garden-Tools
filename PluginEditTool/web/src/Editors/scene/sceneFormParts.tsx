import { useId, useState, type ReactNode } from 'react';
import { Trash2 } from 'lucide-react';
import HelpTip from '../help/HelpTip';
import { hasBit, plural, withBit, type FlagName } from './sceneModel';

export function Field({ label, topic, children }: { label: string; topic: string; children: (id: string) => ReactNode }) {
  const id = useId();
  return (
    <div className="scene-field">
      <div className="scene-field-head">
        <label htmlFor={id}>{label}</label>
        <HelpTip topicId={topic} />
      </div>
      {children(id)}
    </div>
  );
}

export function FlagBoxes({ label, topic, names, value, onChange }: {
  label: string; topic: string; names: readonly FlagName[]; value: number; onChange: (value: number) => void;
}) {
  return (
    <fieldset className="scene-flags">
      <legend>
        <span>{label}</span>
        <HelpTip topicId={topic} />
      </legend>
      {names.map(flag => (
        <label key={flag.bit} className="scene-flag">
          <input type="checkbox" checked={hasBit(value, flag.bit)}
            onChange={e => onChange(withBit(value, flag.bit, e.target.checked))} />
          <span>{flag.label}</span>
        </label>
      ))}
    </fieldset>
  );
}

export function Notice({ tone, children }: { tone: 'warning' | 'info'; children: ReactNode }) {
  return <div className={`scene-notice scene-notice-${tone}`} role={tone === 'warning' ? 'alert' : 'note'}>{children}</div>;
}

export function ConditionsSummary({ label, count, unreadable, onEdit }: {
  label: string; count: number; unreadable: boolean; onEdit: () => void;
}) {
  return (
    <div className="scene-field">
      <div className="scene-field-head">
        <span>{label}</span>
        <HelpTip topicId="scene-conditions" />
      </div>
      {unreadable
        ? <Notice tone="warning">These conditions could not be read; saving here will not change them</Notice>
        : <span className="scene-count">{plural(count, 'condition', 'conditions')}</span>}
      <button type="button" className="sidebar-action-btn" disabled={unreadable} onClick={onEdit}>
        Edit {label.toLowerCase()}
      </button>
    </div>
  );
}

export interface RemoveTarget {
  value: number;
  label: string;
}

export function RemovePanel({ noun, usage, targets, blockedBy, onRepoint, onRemove }: {
  noun: string;
  usage: number;
  targets: RemoveTarget[];
  blockedBy: string | null;
  onRepoint?: (target: number) => void;
  onRemove: () => void;
}) {
  const [target, setTarget] = useState(targets[0]?.value ?? -1);
  return (
    <div className="scene-remove">
      {usage > 0 && (
        <Notice tone="info">
          {plural(usage, 'action uses', 'actions use')} this {noun}. Point {usage === 1 ? 'it' : 'them'} at another {noun} before you remove it.
        </Notice>
      )}
      {usage > 0 && targets.length > 0 && onRepoint && (
        <div className="scene-repoint">
          <Field label="Move them to" topic="scene-usage">
            {id => (
              <select id={id} value={target} onChange={e => setTarget(Number(e.target.value))}>
                {targets.map(entry => <option key={entry.value} value={entry.value}>{entry.label}</option>)}
              </select>
            )}
          </Field>
          <button type="button" className="sidebar-action-btn" onClick={() => onRepoint?.(target)}>
            Re-point {plural(usage, 'action', 'actions')}
          </button>
        </div>
      )}
      {blockedBy && <Notice tone="info">{blockedBy}</Notice>}
      <button type="button" className="sidebar-action-btn sidebar-action-btn--warn"
        disabled={usage > 0 || blockedBy !== null} onClick={onRemove}>
        <Trash2 size={14} /> Remove {noun}
      </button>
    </div>
  );
}
