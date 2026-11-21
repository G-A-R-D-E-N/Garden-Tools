import type { CellLandResult, CellPropertiesResult, WorldPropertiesResult } from './backend';

const record = (raw: string, what: string): Record<string, unknown> => {
  const parsed = JSON.parse(raw) as unknown;
  if (typeof parsed !== 'object' || parsed === null) throw new Error(`unexpected ${what} reply`);
  return parsed as Record<string, unknown>;
};

const hasProperties = (r: Record<string, unknown>) =>
  Array.isArray(r.descriptors) && typeof r.values === 'object' && r.values !== null;

export const parseCellProperties = (raw: string): CellPropertiesResult => {
  const r = record(raw, 'cell properties');
  if (!hasProperties(r)) throw new Error('unexpected cell properties reply');
  return r as unknown as CellPropertiesResult;
};

export const parseWorldProperties = (raw: string): WorldPropertiesResult => {
  const r = record(raw, 'worldspace properties');
  if (!hasProperties(r)) throw new Error('unexpected worldspace properties reply');
  return r as unknown as WorldPropertiesResult;
};

export const parseCellLand = (raw: string): CellLandResult => {
  const r = record(raw, 'terrain');
  const sized = (n: unknown) => Number.isInteger(n) && (n as number) > 0;
  if (!Number.isFinite(r.offset) || !sized(r.width) || !sized(r.height)
    || !Array.isArray(r.heights) || !r.heights.every(Number.isFinite)) {
    throw new Error('unexpected terrain reply');
  }
  return r as unknown as CellLandResult;
};
