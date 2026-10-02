export function parseCellGridCoordinate(value: string): number | null {
  if (!/^[+-]?\d+$/.test(value)) return null;
  const parsed = Number(value);
  return parsed >= -0x80000000 && parsed <= 0x7fffffff ? parsed : null;
}
