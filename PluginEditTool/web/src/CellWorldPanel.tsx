import { useRef, useState } from 'react';
import type { CellPropertyValue, WorldPropertiesResult, WorldPropertyChanges, WorldPropertyDescriptor } from './backend';
import { parseNumberText, tokens, type ParsedValue } from './util/numberText';

const textOf = (value: CellPropertyValue | undefined): string => {
  if (value === undefined || value === null) return '';
  return Array.isArray(value) ? value.join(' ') : String(value);
};

function parseText(stored: CellPropertyValue | undefined, text: string): ParsedValue | undefined {
  if (text.trim() === '') return undefined;
  return parseNumberText(text, Array.isArray(stored) || tokens(text).length > 1);
}

export default function CellWorldPanel({ properties, onEdit }: {
  properties: WorldPropertiesResult;
  onEdit: (changes: WorldPropertyChanges) => Promise<string | null>;
}) {
  const [error, setError] = useState<string | null>(null);
  const pending = useRef(new Set<string>());

  const commit = async (d: WorldPropertyDescriptor, input: HTMLInputElement, stored: string) => {
    if (pending.current.has(d.name)) return;
    const storedValue = properties.values[d.name];
    const next: ParsedValue | undefined = d.kind === 'link'
      ? { value: input.value.trim() || null }
      : parseText(storedValue, input.value);
    if (next === undefined || input.value.trim() === stored) return;
    if ('error' in next) {
      setError(next.error);
      return;
    }
    pending.current.add(d.name);
    try {
      const failure = await onEdit({ [d.name]: next.value });
      setError(failure);
      if (failure !== null) input.value = stored;
    } finally {
      pending.current.delete(d.name);
    }
  };

  return (
    <div className="cell-edit cell-edit-stacked">
      <div className="cell-layers-head"><span>Worldspace properties</span></div>
      <div className="cell-edit-fields">
        {properties.descriptors.map(d => {
          const storedValue = properties.values[d.name];
          const stored = textOf(storedValue);
          const numeric = (d.kind === 'scalar' || d.kind === 'flags') && typeof storedValue === 'number';
          return (
            <label
              className={`cell-edit-field${numeric ? '' : ' cell-prop-wide'}`}
              key={d.name}
              title={d.targets.length > 0 ? `Accepts ${d.targets.join(', ')}` : undefined}
            >
              <span>{`World ${d.label}`}</span>
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
