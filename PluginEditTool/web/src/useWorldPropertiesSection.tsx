import { useRef, useState } from 'react';
import CellWorldPanel from './CellWorldPanel';
import { parseWorldProperties } from './cellValidate';
import { errorText } from './errors';
import type { CellHost, WorldPropertiesResult, WorldPropertyChanges } from './backend';

type Section = {
  cell?: CellHost | null;
  appendLog: (line: string) => void;
  ensurePatch: () => Promise<string | null>;
  exclusive: (run: () => Promise<boolean>) => Promise<boolean>;
  mutate: (label: string, patch: string, from: string | null,
    act: (c: CellHost) => Promise<string | null>, reloadRefs?: boolean) => Promise<string | null>;
  onWorldspace: (key: string | null) => void;
};

export default function useWorldPropertiesSection({ cell, appendLog, ensurePatch, exclusive, mutate, onWorldspace }: Section) {
  const [properties, setProperties] = useState<WorldPropertiesResult | null>(null);
  const [settled, setSettled] = useState(false);
  const keyRef = useRef<string | null>(null);

  const load = async (world: string, live: () => boolean) => {
    if (!cell) return;
    try {
      const parsed = parseWorldProperties(await cell.GetWorldProperties(world));
      if (live()) {
        setProperties(parsed);
        keyRef.current = world;
      }
    } catch (e) {
      if (live()) {
        setProperties(null);
        keyRef.current = null;
        appendLog('✗ worldspace properties: ' + errorText(e));
      }
    }
  };

  const reset = () => {
    keyRef.current = null;
    setProperties(null);
    setSettled(false);
  };

  const reload = async (live: () => boolean) => {
    if (keyRef.current) await load(keyRef.current, live);
  };

  const loadFor = async (cellFormKey: string, live: () => boolean) => {
    if (!cell) return;
    try {
      const reply = JSON.parse(await cell.ResolveWorldspace(cellFormKey)) as { worldspaceFormKey?: string | null };
      if (!live()) return;
      onWorldspace(reply.worldspaceFormKey ?? null);
      if (reply.worldspaceFormKey) await load(reply.worldspaceFormKey, live);
      else setProperties(null);
    } catch {
      if (live()) {
        onWorldspace(null);
        setProperties(null);
        keyRef.current = null;
      }
    }
    if (live()) setSettled(true);
  };

  const edit = async (changes: WorldPropertyChanges): Promise<string | null> => {
    const worldKey = keyRef.current;
    if (!worldKey) return 'Load a cell in a worldspace first.';
    let failure: string | null = 'Another edit is running.';
    await exclusive(async () => {
      const patch = await ensurePatch();
      if (!patch) {
        appendLog('x worldspace properties not saved -- no patch plugin chosen.');
        failure = 'No patch plugin chosen.';
        return false;
      }
      failure = await mutate(`set worldspace ${Object.keys(changes).join(', ')}`, patch, null, async c => {
        await c.SetWorldProperties(worldKey, patch, changes);
        return null;
      }, false);
      return failure === null;
    });
    return failure;
  };

  const element = properties ? <CellWorldPanel properties={properties} onEdit={edit} /> : null;
  return { loadFor, reload, reset, settled, element };
}
