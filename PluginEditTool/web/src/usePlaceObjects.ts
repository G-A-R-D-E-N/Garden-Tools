import { useCallback } from 'react';
import type { CellHost } from './backend';
import { buildPlacements } from './assetBrowser/buildPlacements';
import { parsePlaceResult, placementsJson } from './assetBrowser/baseObjects';
import { loadSnapSetting } from './assetBrowser/browserPrefs';
import type { BaseObjectDragItem, BaseObjectDragPayload } from './assetBrowser/dragPayload';
import type { Point3 } from './assetBrowser/placement';
import { DROP_IGNORED_BUSY_LINE, DROP_IGNORED_NO_CELL_LINE, placeErrorLine, placeOkLine, placeRequestLine } from './dropDiagnostics';

export type EditSelected = (label: string, from: string | null,
  act: (c: CellHost, patch: string) => Promise<string | null>) => Promise<boolean>;

export function usePlaceObjects(cellFormKey: string | null | undefined, editSelected: EditSelected,
  log: (line: string) => void, isBusy: () => boolean) {
  const place = useCallback(async (items: BaseObjectDragItem[], where: Point3): Promise<boolean> => {
    if (!cellFormKey || items.length === 0) {
      log('x place not saved -- ' + (cellFormKey ? 'nothing to place.' : 'load a cell first.'));
      return false;
    }
    const key: string = cellFormKey;
    const placements = buildPlacements(items, where, loadSnapSetting());
    const label = `place ${items.length} object${items.length === 1 ? '' : 's'}`;
    return editSelected(label, null, async (c, patch) => {
      log(placeRequestLine(placements.length));
      const parsed = parsePlaceResult(await c.PlaceReferences(patch, key, placementsJson(placements), {}));
      if (!parsed.ok) {
        log(placeErrorLine(parsed.error));
        throw new Error(parsed.error);
      }
      log(placeOkLine(parsed.reply.created));
      return parsed.reply.created[0] ?? null;
    });
  }, [cellFormKey, editSelected, log]);

  const placeFromDrop = useCallback((payload: BaseObjectDragPayload, point: Point3) => {
    if (!cellFormKey) {
      log(DROP_IGNORED_NO_CELL_LINE);
      return;
    }
    if (isBusy()) {
      log(DROP_IGNORED_BUSY_LINE);
      return;
    }
    void place(payload.items, point);
  }, [cellFormKey, isBusy, log, place]);

  return { place, placeFromDrop };
}
