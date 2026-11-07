import { useRef, useState } from 'react';
import type { CellPropertiesResult, CellPropertyChanges, CellPropertyDescriptor, CellPropertyValue } from './backend';

const tokens = (text: string): string[] => text.split(/[\s,]+/).filter(t => t !== '');

const textOf = (value: CellPropertyValue | undefined): string => {
  if (value === undefined || value === null) return '';
  return Array.isArray(value) ? value.join(' ') : String(value);
};

function parseText(d: CellPropertyDescriptor, text: string): CellPropertyValue | undefined {
  if (d.kind === 'link') return d.multiple ? tokens(text) : (text.trim() || null);
  if (d.kind === 'colour') {
    const parts = tokens(text);
    return parts.length === 0 ? undefined : parts.map(Number);
  }
  return text.trim() === '' ? undefined : Number(text);
}

export default function CellPropertiesPanel({ properties, onEdit }: {
  properties: CellPropertiesResult;
  onEdit: (changes: CellPropertyChanges) => Promise<string | null>;
}) {
  const [error, setError] = useState<string | null>(null);
  const pending = useRef(new Set<string>());

  const commit = async (d: CellPropertyDescriptor, input: HTMLInputElement, stored: string) => {
    if (pending.current.has(d.name)) return;
    const next = parseText(d, input.value);
    if (next === undefined || input.value.trim() === stored) return;
    pending.current.add(d.name);
    try {
      const failure = await onEdit({ [d.name]: next });
      setError(failure);
      if (failure !== null) input.value = stored;
    } finally {
      pending.current.delete(d.name);
    }
  };

  return (
    <div className="cell-edit">
      <div className="cell-layers-head"><span>Cell properties</span></div>
      <div className="cell-edit-fields">
        {properties.descriptors.map(d => {
          const stored = textOf(properties.values[d.name]);
          const numeric = d.kind === 'scalar' || d.kind === 'flags';
          return (
            <label
              className={`cell-edit-field${numeric ? '' : ' cell-prop-wide'}`}
              key={d.name}
              title={d.targets.length > 0 ? `Accepts ${d.targets.join(', ')}` : undefined}
            >
              <span>{d.label}</span>
              <input
                key={`${d.name}:${stored}`}
                type={numeric ? 'number' : 'text'}
                step={numeric ? 'any' : undefined}
                defaultValue={stored}
                onBlur={e => void commit(d, e.currentTarget, stored)}
                onKeyDown={e => { if (e.key === 'Enter') void commit(d, e.currentTarget, stored); }}
              />
            </label>
          );
        })}
      </div>
      {error && <div className="papyrus-warn">{error}</div>}
    </div>
  );
}
