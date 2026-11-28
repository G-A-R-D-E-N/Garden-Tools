import { useState } from 'react';
import type { ReferenceDetails } from './referenceDetails';
import type { ReferenceEditProps } from './ReferencePanel';
import { primitiveTypeName } from './cellPrimitive';
import { parseFiniteNumber, primitiveChange } from './util/referenceOwnershipPrimitive';

type Primitive = NonNullable<ReferenceDetails['primitive']>;

const BOUNDS_LABELS = ['X', 'Y', 'Z'];
const COLOUR_LABELS = ['R', 'G', 'B', 'A'];

const isNumber = (value: number | null): value is number => value !== null;

function numbersOf(texts: string[]): number[] | null {
  const values = texts.map(parseFiniteNumber);
  return values.every(isNumber) ? values : null;
}

export default function ReferencePrimitiveEditor({ primitive, edit }: { primitive: Primitive; edit: ReferenceEditProps }) {
  const storedBounds = [primitive.bounds.x, primitive.bounds.y, primitive.bounds.z];
  const storedColor = primitive.color;
  const [boundsText, setBoundsText] = useState(storedBounds.map(String));
  const [colorText, setColorText] = useState(storedColor.map(String));
  const [busy, setBusy] = useState(false);

  const bounds = numbersOf(boundsText);
  const color = numbersOf(colorText);
  const boundsInvalid = bounds === null || bounds.some((value) => value < 0);
  const colorInvalid = color === null || color.length !== 4;
  const change = !boundsInvalid && !colorInvalid
    ? primitiveChange({ bounds: storedBounds, color: storedColor }, { bounds, color })
    : undefined;
  const canApply = change !== undefined && !busy;

  const reset = () => {
    setBoundsText(storedBounds.map(String));
    setColorText(storedColor.map(String));
  };

  const apply = async () => {
    if (change === undefined || busy) return;
    setBusy(true);
    try {
      if (!(await edit.onEdit({ primitive: change }))) reset();
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="ref-edit">
      <div className="ref-edit-field">
        <span>Shape</span>
        <span>{primitiveTypeName(primitive.type)}</span>
      </div>
      {BOUNDS_LABELS.map((label, index) => (
        <label key={label} className="ref-edit-field">
          <span>{`Half-extent ${label}`}</span>
          <input
            type="text"
            inputMode="decimal"
            value={boundsText[index]}
            disabled={busy}
            spellCheck={false}
            onChange={(e) => setBoundsText(boundsText.map((text, i) => (i === index ? e.currentTarget.value : text)))}
          />
        </label>
      ))}
      {COLOUR_LABELS.map((label, index) => (
        <label key={label} className="ref-edit-field">
          <span>{`Colour ${label}`}</span>
          <input
            type="text"
            inputMode="decimal"
            value={colorText[index]}
            disabled={busy}
            spellCheck={false}
            onChange={(e) => setColorText(colorText.map((text, i) => (i === index ? e.currentTarget.value : text)))}
          />
        </label>
      ))}
      {boundsInvalid && <div className="ref-error">Half-extents must be non-negative numbers.</div>}
      {!boundsInvalid && colorInvalid && <div className="ref-error">Colour must be four numbers.</div>}
      <div className="ref-actions">
        <button className="papyrus-copy" disabled={!canApply} onClick={() => void apply()}>Apply primitive</button>
      </div>
    </div>
  );
}
