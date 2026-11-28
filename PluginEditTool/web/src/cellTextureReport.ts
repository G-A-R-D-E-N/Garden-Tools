export interface CellTextureReport {
  materialNotFound: number;
  noPath: Map<string, number>;
  missing: Map<string, number>;
}

export const TEXTURE_REPORT_TOP = 10;

export function createTextureReport(): CellTextureReport {
  return { materialNotFound: 0, noPath: new Map(), missing: new Map() };
}

export function snapshotTextureReport(report: CellTextureReport): CellTextureReport {
  return {
    materialNotFound: report.materialNotFound,
    noPath: new Map(report.noPath),
    missing: new Map(report.missing),
  };
}

function bump(counts: Map<string, number>, key: string): void {
  counts.set(key, (counts.get(key) ?? 0) + 1);
}

export function countMaterialNotFound(report: CellTextureReport): void {
  report.materialNotFound++;
}

export function countNoPath(report: CellTextureReport, shaderType: string): void {
  bump(report.noPath, shaderType);
}

export function countMissing(report: CellTextureReport, path: string): void {
  bump(report.missing, path);
}

export function topMissing(report: CellTextureReport, limit = TEXTURE_REPORT_TOP): [string, number][] {
  return [...report.missing.entries()]
    .sort(([pathA, countA], [pathB, countB]) => countB - countA || pathA.localeCompare(pathB))
    .slice(0, limit);
}

export function formatTextureReport(report: CellTextureReport): string[] {
  const lines: string[] = [];
  if (report.materialNotFound > 0) {
    const n = report.materialNotFound;
    lines.push(`${n} material${n === 1 ? '' : 's'} not found (bgsm path set, no material resolved)`);
  }
  const shaders = [...report.noPath.entries()].sort(([a], [b]) => a.localeCompare(b));
  for (const [shaderType, n] of shaders) {
    lines.push(`${n} shape${n === 1 ? '' : 's'} with no diffuse path, shader ${shaderType}`);
  }
  const top = topMissing(report);
  if (top.length > 0) {
    lines.push(`Missing textures, top ${top.length} of ${report.missing.size}:`);
    for (const [path, n] of top) lines.push(`  ${path} (x${n})`);
  }
  return lines;
}
