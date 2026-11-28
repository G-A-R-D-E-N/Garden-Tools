import { useEffect, useMemo, useRef, useState } from 'react';
import type { CellLandResult } from './backend';

const numberText = (value: number): string => String(Number(value.toFixed(3)));

const landText = (land: CellLandResult): string => {
  const rows: string[] = [];
  for (let row = 0; row < land.height; row += 1) {
    rows.push(land.heights.slice(row * land.width, (row + 1) * land.width).map(numberText).join(' '));
  }
  return rows.join('\n');
};

const parseLandText = (text: string): number[] | null => {
  const tokens = text.split(/[\s,]+/).filter(token => token !== '');
  if (tokens.length === 0) return null;
  const heights = tokens.map(Number);
  return heights.every(height => Number.isFinite(height)) ? heights : null;
};

export default function CellLandPanel({ land, onSave }: {
  land: CellLandResult;
  onSave: (heights: number[]) => Promise<string | null>;
}) {
  const cellKey = `${land.cellFormKey}:${land.landFormKey}`;
  const [prevCellKey, setPrevCellKey] = useState(cellKey);
  const [error, setError] = useState<string | null>(null);
  const [savingRequestId, setSavingRequestId] = useState<number | null>(null);
  const area = useRef<HTMLTextAreaElement>(null);
  const saveRequestRef = useRef(0);
  if (prevCellKey !== cellKey) {
    setPrevCellKey(cellKey);
    setError(null);
    setSavingRequestId(null);
  }
  const stored = useMemo(() => landText(land), [land]);
  const expected = land.width * land.height;
  const saving = savingRequestId !== null;

  useEffect(() => {
    saveRequestRef.current += 1;
  }, [cellKey]);

  const save = async () => {
    const input = area.current;
    if (input === null || saving) return;
    const heights = parseLandText(input.value);
    if (heights === null) {
      setError('Terrain heights must be numbers, one row of the grid per line');
      return;
    }
    if (heights.length !== expected) {
      setError(`Terrain needs ${expected} heights, not ${heights.length}`);
      return;
    }
    const request = ++saveRequestRef.current;
    setSavingRequestId(request);
    try {
      const failure = await onSave(heights);
      if (saveRequestRef.current === request) setError(failure);
    } catch (err) {
      if (saveRequestRef.current === request) {
        setError(err instanceof Error ? err.message : String(err));
      }
    } finally {
      if (saveRequestRef.current === request) {
        setSavingRequestId(current => (current === request ? null : current));
      }
    }
  };

  return (
    <div className="cell-edit">
      <div className="cell-layers-head"><span>Terrain</span></div>
      <div className="cell-edit-fields">
        <label className="cell-edit-field">
          <span>Height offset</span>
          <input type="text" readOnly value={numberText(land.offset)} />
        </label>
        <label className="cell-edit-field">
          <span>Grid</span>
          <input type="text" readOnly value={`${land.width} x ${land.height}`} />
        </label>
      </div>
      <label className="cell-edit-field cell-prop-wide">
        <span>Heights</span>
        <textarea ref={area} key={`${land.cellFormKey}:${land.landFormKey}:${stored}`} rows={12} defaultValue={stored} spellCheck={false} />
      </label>
      <button type="button" onClick={() => void save()} disabled={saving}>Save terrain</button>
      {error && <div className="papyrus-warn">{error}</div>}
    </div>
  );
}
