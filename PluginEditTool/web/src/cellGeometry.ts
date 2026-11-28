import type { CellHost, CellPlacedReference } from './backend';
import type { CellGeoMap } from './CellViewport';
import { errorText } from './errors';

export function referenceModelPaths(refs: readonly CellPlacedReference[]): string[] {
  const paths: string[] = [];
  for (const r of refs) {
    if (r.modelPath) paths.push(r.modelPath);
    for (const part of r.scolParts ?? []) paths.push(part.modelPath);
  }
  return Array.from(new Set(paths));
}

export function missingModelPaths(refs: readonly CellPlacedReference[], geometry: CellGeoMap): string[] {
  return referenceModelPaths(refs).filter(path => !(path in geometry));
}

export async function fetchMissingGeometry(
  cell: Pick<CellHost, 'GetGeometryBatch'> | null,
  refs: readonly CellPlacedReference[],
  geometry: CellGeoMap,
  live: () => boolean,
  log: (line: string) => void,
): Promise<CellGeoMap | null> {
  const missing = missingModelPaths(refs, geometry);
  if (!cell || missing.length === 0) return null;
  log(`• geometry: converting ${missing.length} mesh(es) for the new reference(s)`);
  try {
    const parsed = JSON.parse(await cell.GetGeometryBatch(JSON.stringify(missing))) as { geometry?: CellGeoMap; error?: string };
    if (!live()) return null;
    if (parsed.error) {
      log('✗ geometry batch: ' + parsed.error);
      return null;
    }
    const fetched: CellGeoMap = {};
    for (const path of missing) fetched[path] = parsed.geometry?.[path] ?? { error: 'no geometry returned' };
    const failed = missing.filter(path => 'error' in fetched[path]).length;
    log(`• geometry: ${missing.length - failed}/${missing.length} new mesh(es) resolved, ${failed} failed`);
    return fetched;
  } catch (e) {
    if (live()) log('✗ geometry batch: ' + errorText(e));
    return null;
  }
}
