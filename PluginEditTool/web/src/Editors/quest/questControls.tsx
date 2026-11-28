import { useState, type InputHTMLAttributes, type KeyboardEvent, type TextareaHTMLAttributes } from 'react';
import type { FriendlyMessage } from '../friendlyErrors';
import { toggleBit, type FlagOption } from './questModel';

type CommitInputProps = Omit<InputHTMLAttributes<HTMLInputElement>, 'value' | 'onChange' | 'onBlur' | 'defaultValue'> & {
  value: string;
  onCommit: (next: string) => void;
};

type CommitTextareaProps = Omit<TextareaHTMLAttributes<HTMLTextAreaElement>, 'value' | 'onChange' | 'onBlur' | 'defaultValue'> & {
  value: string;
  onCommit: (next: string) => void;
};

export function CommitTextarea({ value, onCommit, ...rest }: CommitTextareaProps) {
  const [draft, setDraft] = useState(value);
  const [seen, setSeen] = useState(value);
  if (value !== seen) {
    setSeen(value);
    setDraft(value);
  }
  return (
    <textarea {...rest} value={draft} onChange={e => setDraft(e.target.value)}
      onBlur={() => { if (draft !== value) onCommit(draft); }} />
  );
}

export function CommitInput({ value, onCommit, ...rest }: CommitInputProps) {
  const [draft, setDraft] = useState(value);
  const [seen, setSeen] = useState(value);
  if (value !== seen) {
    setSeen(value);
    setDraft(value);
  }
  return (
    <input {...rest} value={draft} onChange={e => setDraft(e.target.value)}
      onBlur={() => { if (draft !== value) onCommit(draft); }}
      onKeyDown={(e: KeyboardEvent<HTMLInputElement>) => { if (e.key === 'Enter') e.currentTarget.blur(); }} />
  );
}

export function FlagChecks({ flags, options, label, onChange }: {
  flags: number; options: readonly FlagOption[]; label: string; onChange: (next: number) => void;
}) {
  return (
    <div className="quest-flags" role="group" aria-label={label}>
      {options.map(option => (
        <label key={option.bit} className="quest-check">
          <input type="checkbox" checked={(flags & option.bit) !== 0}
            onChange={e => onChange(toggleBit(flags, option.bit, e.target.checked))} />
          {option.label}
        </label>
      ))}
    </div>
  );
}

export function ErrorNote({ error }: { error: FriendlyMessage }) {
  return (
    <div className="quest-error" role="alert">
      <p>{error.text}</p>
      {error.details && <p className="quest-details">Details: {error.details}</p>}
    </div>
  );
}
