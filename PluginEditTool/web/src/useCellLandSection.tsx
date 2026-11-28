import { useRef, useState } from 'react';
import CellLandPanel from './CellLandPanel';
import { parseCellLand } from './cellValidate';
import { errorText } from './errors';
import type { CellHost, CellLandResult } from './backend';

type Section = {
  cell?: CellHost | null;
  appendLog: (line: string) => void;
  ensurePatch: () => Promise<string | null>;
  exclusive: (run: () => Promise<boolean>) => Promise<boolean>;
  mutate: (label: string, patch: string, from: string | null,
    act: (c: CellHost) => Promise<string | null>, reloadRefs?: boolean) => Promise<string | null>;
};

export default function useCellLandSection({ cell, appendLog, ensurePatch, exclusive, mutate }: Section) {
  const [land, setLand] = useState<CellLandResult | null>(null);
  const [loaded, setLoaded] = useState(false);
  const [loadError, setLoadError] = useState<string | null>(null);
  const keyRef = useRef<string | null>(null);

  const load = async (cellKey: string, live: () => boolean, keepLandOnError: boolean) => {
    if (!cell) return;
    try {
      const parsed = parseCellLand(await cell.GetCellLand(cellKey));
      if (live()) {
        setLand(parsed);
        setLoadError(null);
      }
    } catch (e) {
      if (!live()) return;
      const missing = /CellLandMissing/.test(errorText(e));
      if (missing) {
        setLand(null);
        setLoadError(null);
        return;
      }
      const message = errorText(e);
      if (!keepLandOnError) setLand(null);
      setLoadError(message);
      appendLog('✗ terrain: ' + message);
    }
  };

  const reset = () => {
    keyRef.current = null;
    setLand(null);
    setLoaded(false);
    setLoadError(null);
  };

  const loadFor = async (cellKey: string, live: () => boolean) => {
    keyRef.current = cellKey;
    if (live()) setLoadError(null);
    await load(cellKey, live, false);
    if (live()) setLoaded(true);
  };

  const reload = async (live: () => boolean) => {
    if (keyRef.current) await load(keyRef.current, live, true);
  };

  const save = async (heights: number[]): Promise<string | null> => {
    const cellKey = land?.cellFormKey;
    if (!cellKey) return 'Load a cell with terrain first.';
    let failure: string | null = 'Another edit is running.';
    await exclusive(async () => {
      const patch = await ensurePatch();
      if (!patch) {
        appendLog('x terrain not saved -- no patch plugin chosen.');
        failure = 'No patch plugin chosen.';
        return false;
      }
      failure = await mutate('set terrain', patch, null, async c => {
        await c.SetCellLand(cellKey, patch, heights);
        return null;
      }, false);
      return failure === null;
    });
    return failure;
  };

  const element = land ? <CellLandPanel land={land} onSave={save} /> : null;
  return { loadFor, reload, reset, loaded, hasLand: land !== null, loadError, element };
}
