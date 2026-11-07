import { useEffect, useRef, useState } from 'react';
import type { CellPropertiesResult, CellPropertyChanges, CellPropertyDescriptor, CellPropertyValue } from './backend';
import CellCollapsible from './CellCollapsible';
import RecordPicker from './RecordPicker';
import { hexToRgb, parseTriplet, rgbToHex, tripletText, type Rgb } from './util/cellColour';
import { CELL_FLAGS, FLAGS_MAX, hasFlag, setFlag, unknownFlagBits, withUnknownBits } from './util/cellFlags';

const INTEGER_FIELDS = new Set(['LightingDirectionalRotationXY', 'LightingDirectionalRotationZ', 'OwnerRank', 'Flags']);

const textOf = (value: CellPropertyValue | undefined): string => {
  if (value === undefined || value === null) return '';
  return Array.isArray(value) ? value.join(' ') : String(value);
};

const linksOf = (value: CellPropertyValue | undefined, multiple: boolean): string[] => {
  if (multiple) return Array.isArray(value) ? (value as string[]) : [];
  return typeof value === 'string' ? [value] : [];
};

const numberAttributes = (d: CellPropertyDescriptor) => ({
  step: INTEGER_FIELDS.has(d.name) ? 1 : 'any',
  min: d.maximum > d.minimum && Math.abs(d.minimum) < 1e30 ? d.minimum : undefined,
  max: d.maximum > d.minimum && Math.abs(d.maximum) < 1e30 ? d.maximum : undefined,
});

const noop = () => {};

function ColourInput({ label, title, stored, onCommit, onInvalid }: {
  label: string;
  title: string;
  stored: string;
  onCommit: (rgb: Rgb, revert: () => void) => void;
  onInvalid: (message: string) => void;
}) {
  const [text, setText] = useState(stored);
  const swatchRef = useRef<HTMLInputElement>(null);
  const sentRef = useRef(stored);
  const swatch = parseTriplet(text);

  const send = (rgb: Rgb) => {
    const next = tripletText(rgb);
    if (next === sentRef.current) return;
    sentRef.current = next;
    onCommit(rgb, () => { setText(stored); sentRef.current = stored; });
  };
  const sendRef = useRef(send);
  useEffect(() => { sendRef.current = send; });

  useEffect(() => {
    const el = swatchRef.current;
    if (!el) return;
    const onChange = () => {
      const rgb = hexToRgb(el.value);
      setText(tripletText(rgb));
      sendRef.current(rgb);
    };
    el.addEventListener('change', onChange);
    return () => el.removeEventListener('change', onChange);
  }, []);

  const commit = () => {
    if (text.trim() === '') { setText(stored); return; }
    const rgb = parseTriplet(text);
    if (rgb === null) {
      onInvalid(`${label}: enter three values from 0 to 255`);
      setText(stored);
      return;
    }
    send(rgb);
  };

  return (
    <div className="cell-edit-field cell-edit-colour" title={title}>
      <label>
        <span>{label}</span>
        <input
          type="text"
          value={text}
          onChange={e => setText(e.target.value)}
          onBlur={commit}
          onKeyDown={e => { if (e.key === 'Enter') commit(); }}
        />
      </label>
      <input
        ref={swatchRef}
        type="color"
        className="cell-colour-swatch"
        aria-label={`${label} swatch`}
        value={rgbToHex(swatch ?? [0, 0, 0])}
        onChange={e => setText(tripletText(hexToRgb(e.target.value)))}
        onBlur={commit}
      />
    </div>
  );
}

function LinkRow({ label, title, multiple, items, onPick, onClear, onRemove }: {
  label: string;
  title: string;
  multiple: boolean;
  items: string[];
  onPick: () => void;
  onClear: () => void;
  onRemove: (formKey: string) => void;
}) {
  return (
    <>
      <div className={`cell-edit-field cell-edit-link${multiple ? ' cell-edit-link-multi' : ''}`}>
        <label title={title}>
          <span>{label}</span>
          <input type="text" readOnly value={items.join(' ')} placeholder="None" />
        </label>
        <button type="button" className="cell-link-btn" aria-label={`Pick ${label}`} title={`Pick a ${label} record`} onClick={onPick}>
          Pick
        </button>
        <button type="button" className="cell-link-btn" aria-label={`Clear ${label}`} title={`Clear ${label}`} disabled={items.length === 0} onClick={onClear}>
          ×
        </button>
      </div>
      {multiple && items.length > 0 && (
        <ul className="cell-link-list">
          {items.map(formKey => (
            <li key={formKey}>
              <span>{formKey}</span>
              <button type="button" className="cell-link-btn" aria-label={`Remove ${formKey} from ${label}`} onClick={() => onRemove(formKey)}>
                ×
              </button>
            </li>
          ))}
        </ul>
      )}
    </>
  );
}

function FlagsField({ d, value, onEdit, onInvalid }: {
  d: CellPropertyDescriptor;
  value: number | null;
  onEdit: (next: number, revert: () => void) => void;
  onInvalid: (message: string) => void;
}) {
  const current = value ?? 0;
  const unknown = unknownFlagBits(current);
  const disabled = value === null;

  const commitOther = (input: HTMLInputElement) => {
    const text = input.value.trim();
    const parsed = Number(text);
    if (text === '' || !Number.isInteger(parsed) || parsed < 0 || parsed > FLAGS_MAX) {
      onInvalid(`${d.label}: unknown bits must be a whole number from 0 to ${FLAGS_MAX}`);
      input.value = String(unknown);
      return;
    }
    if (unknownFlagBits(parsed) !== parsed) {
      onInvalid(`${d.label}: unknown bits cannot include named flags`);
      input.value = String(unknown);
      return;
    }
    if (value === null) return;
    const next = withUnknownBits(value, parsed);
    if (next === value) return;
    onEdit(next, () => { input.value = String(unknown); });
  };

  return (
    <fieldset className="cell-flags" title={d.label}>
      <legend>{d.label}</legend>
      {CELL_FLAGS.map(f => (
        <label className="cell-flag" key={f.bit} title={f.locked ? 'Cannot be changed here' : `0x${f.bit.toString(16).toUpperCase().padStart(4, '0')}`}>
          <input
            type="checkbox"
            checked={hasFlag(current, f.bit)}
            disabled={disabled || f.locked}
            onChange={() => {
              if (value === null) return;
              onEdit(setFlag(value, f.bit, !hasFlag(value, f.bit)), noop);
            }}
          />
          <span>{f.label}</span>
        </label>
      ))}
      <label className="cell-flag cell-flag-unknown">
        <span>Unknown bits</span>
        <input
          key={`${d.name}:${unknown}`}
          type="number"
          min={0}
          max={FLAGS_MAX}
          step={1}
          disabled={disabled}
          defaultValue={unknown}
          onBlur={e => commitOther(e.currentTarget)}
          onKeyDown={e => { if (e.key === 'Enter') commitOther(e.currentTarget); }}
        />
      </label>
    </fieldset>
  );
}

type Queued = { changes: CellPropertyChanges; revert: () => void };

export default function CellPropertiesPanel({ properties, onEdit }: {
  properties: CellPropertiesResult;
  onEdit: (changes: CellPropertyChanges) => Promise<string | null>;
}) {
  const [error, setError] = useState<string | null>(null);
  const [picking, setPicking] = useState<CellPropertyDescriptor | null>(null);
  const [, setRevision] = useState(0);
  const values = properties.values;
  const inFlight = useRef(new Set<string>());
  const queued = useRef(new Map<string, Queued>());
  const optimistic = useRef(new Map<string, CellPropertyValue>());
  const repaint = () => setRevision(r => r + 1);

  useEffect(() => {
    let pruned = false;
    for (const name of [...optimistic.current.keys()]) {
      if (!inFlight.current.has(name) && !queued.current.has(name)) {
        optimistic.current.delete(name);
        pruned = true;
      }
    }
    if (pruned) setRevision(r => r + 1);
  }, [properties]);

  const baseOf = (name: string): CellPropertyValue | undefined =>
    optimistic.current.has(name) ? optimistic.current.get(name) : values[name];

  const run = async (name: string, changes: CellPropertyChanges, revert: () => void): Promise<void> => {
    inFlight.current.add(name);
    let failure: string | null = null;
    try {
      failure = await onEdit(changes);
    } catch (e) {
      failure = e instanceof Error ? e.message : String(e);
    } finally {
      inFlight.current.delete(name);
    }
    setError(failure);
    const next = queued.current.get(name);
    queued.current.delete(name);
    if (failure !== null) {
      revert();
      next?.revert();
      optimistic.current.delete(name);
      repaint();
      return;
    }
    if (next) { await run(name, next.changes, next.revert); return; }
    optimistic.current.delete(name);
    repaint();
  };

  const send = (name: string, changes: CellPropertyChanges, revert: () => void = noop) => {
    optimistic.current.set(name, changes[name] as CellPropertyValue);
    repaint();
    if (inFlight.current.has(name)) { queued.current.set(name, { changes, revert }); return; }
    void run(name, changes, revert);
  };

  const commitScalar = (d: CellPropertyDescriptor, input: HTMLInputElement, stored: string) => {
    if (input.value.trim() === '' || input.value.trim() === stored) return;
    const target = Number(input.value);
    if (optimistic.current.get(d.name) === target) return;
    send(d.name, { [d.name]: target }, () => { input.value = stored; });
  };

  const linkItems = (d: CellPropertyDescriptor): string[] => linksOf(baseOf(d.name), d.multiple);

  const pickLink = (d: CellPropertyDescriptor, formKey: string) => {
    const items = linkItems(d);
    if (!d.multiple) { send(d.name, { [d.name]: formKey }); return; }
    if (items.includes(formKey)) return;
    send(d.name, { [d.name]: [...items, formKey] });
  };

  const clearLink = (d: CellPropertyDescriptor) => {
    send(d.name, { [d.name]: d.multiple ? [] : null });
  };

  const removeLink = (d: CellPropertyDescriptor, formKey: string) => {
    send(d.name, { [d.name]: linkItems(d).filter(k => k !== formKey) });
  };

  const renderField = (d: CellPropertyDescriptor) => {
    const value = values[d.name];
    const stored = textOf(value);
    const title = d.targets.length > 0 ? `${d.label} -- accepts ${d.targets.join(', ')}` : d.label;

    if (d.kind === 'colour') {
      return (
        <ColourInput
          key={`${d.name}:${stored}`}
          label={d.label}
          title={title}
          stored={stored}
          onCommit={(rgb, revert) => send(d.name, { [d.name]: rgb }, revert)}
          onInvalid={setError}
        />
      );
    }
    if (d.kind === 'flags') {
      const pending = baseOf(d.name);
      return (
        <FlagsField
          key={`${d.name}:${stored}`}
          d={d}
          value={typeof pending === 'number' ? pending : null}
          onEdit={(next, revert) => send(d.name, { [d.name]: next }, revert)}
          onInvalid={setError}
        />
      );
    }
    if (d.kind === 'link') {
      return (
        <LinkRow
          key={d.name}
          label={d.label}
          title={title}
          multiple={d.multiple}
          items={linksOf(value, d.multiple)}
          onPick={() => setPicking(d)}
          onClear={() => clearLink(d)}
          onRemove={formKey => removeLink(d, formKey)}
        />
      );
    }
    return (
      <label className="cell-edit-field" key={d.name} title={title}>
        <span>{d.label}</span>
        <input
          key={`${d.name}:${stored}`}
          type="number"
          {...numberAttributes(d)}
          defaultValue={stored}
          onBlur={e => commitScalar(d, e.currentTarget, stored)}
          onKeyDown={e => { if (e.key === 'Enter') commitScalar(d, e.currentTarget, stored); }}
        />
      </label>
    );
  };

  return (
    <div className="cell-edit">
      <CellCollapsible storageKey="cell.section.props" title="Cell properties">
        <div className="cell-edit-fields cell-props-fields">
          {properties.descriptors.map(renderField)}
        </div>
      </CellCollapsible>
      {error && <div className="papyrus-warn">{error}</div>}
      {picking && (
        <RecordPicker
          title={picking.label}
          refType={picking.targets.join(' / ') || null}
          refTypes={picking.targets.join(',') || null}
          onPick={formKey => {
            const d = picking;
            if (!d.multiple) setPicking(null);
            pickLink(d, formKey);
          }}
          onClose={() => setPicking(null)}
          onClear={picking.multiple ? undefined : () => { const d = picking; setPicking(null); clearLink(d); }}
        />
      )}
    </div>
  );
}
