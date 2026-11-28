export const SNAP_GRID_SIZES = [1, 8, 16, 64] as const;
export const DEFAULT_SNAP_GRID = 64;
export const MULTI_DROP_SPACING = 64;

export interface Point3 {
  x: number;
  y: number;
  z: number;
}

export function snapValue(value: number, grid: number): number {
  if (!(grid > 0)) return value;
  return Math.round(value / grid) * grid;
}

export function snapPoint(point: Point3, grid: number | null): Point3 {
  if (grid === null) return point;
  return { x: snapValue(point.x, grid), y: snapValue(point.y, grid), z: point.z };
}

export function spiralOffsets(count: number, spacing: number = MULTI_DROP_SPACING): Array<{ x: number; y: number }> {
  const offsets: Array<{ x: number; y: number }> = [];
  if (count <= 0) return offsets;
  offsets.push({ x: 0, y: 0 });
  let x = 0;
  let y = 0;
  let dx = 1;
  let dy = 0;
  let leg = 1;
  while (offsets.length < count) {
    for (let turn = 0; turn < 2 && offsets.length < count; turn++) {
      for (let step = 0; step < leg && offsets.length < count; step++) {
        x += dx;
        y += dy;
        offsets.push({ x: x * spacing, y: y * spacing });
      }
      [dx, dy] = [-dy, dx];
    }
    leg += 1;
  }
  return offsets;
}

export function placementPoints(origin: Point3, count: number, grid: number | null,
  spacing: number = MULTI_DROP_SPACING): Point3[] {
  const base = snapPoint(origin, grid);
  return spiralOffsets(count, spacing).map(offset => ({
    x: base.x + offset.x,
    y: base.y + offset.y,
    z: base.z,
  }));
}

export function parseSnapSetting(raw: string | null): number | null {
  if (raw === 'off') return null;
  const value = Number(raw);
  return (SNAP_GRID_SIZES as readonly number[]).includes(value) ? value : DEFAULT_SNAP_GRID;
}
