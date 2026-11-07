import { useCallback } from 'react';
import type { CellHost } from './backend';
import { buildPlacements } from './assetBrowser/buildPlacements';
import { parsePlaceResult, placementsJson } from './assetBrowser/baseObjects';
import { loadSnapSetting } from './assetBrowser/browserPrefs';
import type { BaseObjectDragItem } from './assetBrowser/dragPayload';
import type { Point3 } from './assetBrowser/placement';

export type EditSelected = (label: string, from: string | null,
  act: (c: CellHost, patch: string) => Promise<string | null>) => Promise<boolean>;

export function usePlaceObjects(cellFormKey: string | null | undefined, editSelected: EditSelected) {
  return useCallback(async (items: BaseObjectDragItem[], where: Point3): Promise<boolean> => {
    if (!cellFormKey || items.length === 0) return false;
    const key: string = cellFormKey;
    const placements = buildPlacements(items, where, loadSnapSetting());
    const label = `place ${items.length} object${items.length === 1 ? '' : 's'}`;
    return editSelected(label, null, async (c, patch) => {
      const parsed = parsePlaceResult(await c.PlaceReferences(patch, key, placementsJson(placements), {}));
      if (!parsed.ok) throw new Error(parsed.error);
      return parsed.reply.created[0] ?? null;
    });
  }, [cellFormKey, editSelected]);
}
