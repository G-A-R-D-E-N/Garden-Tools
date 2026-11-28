import { zeroTransform, type PlacementItem } from './baseObjects';
import type { BaseObjectDragItem } from './dragPayload';
import { placementPoints, type Point3 } from './placement';

export function buildPlacements(items: BaseObjectDragItem[], origin: Point3, grid: number | null): PlacementItem[] {
  const points = placementPoints(origin, items.length, grid);
  return items.map((item, i) => ({
    baseFormKey: item.formKey,
    transform: { ...zeroTransform(), x: points[i].x, y: points[i].y, z: points[i].z },
  }));
}
