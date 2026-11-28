import { hostFailure, isNumber, isRecord, lodHost, sameWorld, type LodPlan } from './lodClient';

export interface LodGeneratedTile {
  world: string;
  kind: string;
  level: number;
  x: number;
  y: number;
  status: string;
  bytes: number;
  error: string;
}

// Invariant: one entry per skip reason.
export interface LodObjectDrop {
  reason: string;
  count: number;
}

export interface LodGeneratedTexture {
  world: string;
  kind: string;
  map: string;
  side: number;
  status: string;
  bytes: number;
  error: string;
}

export interface LodGenerateReport {
  apply: boolean;
  // Invariant: a cancelled run says so, so a short report is never read as a finished bake.
  cancelled: boolean;
  written: number;
  wouldWrite: number;
  unsupportedCandidates: number;
  // Constraint: only reasons with a nonzero count appear.
  objectDrops: LodObjectDrop[];
  failed: number;
  unreadableWorlds: number;
  empty: number;
  atlasEntries: number;
  atlasMissingTextures: number;
  atlasClampedShapes: number;
  objectMissingMeshes: number;
  objectDroppedTriangles: number;
  texturesWritten: number;
  texturesSkipped: number;
  terrainTexturesWritten: number;
  terrainTexturesSkipped: number;
  terrainTexturesEmpty: number;
  // Invariant: a partial level scope bakes no object tile, so the reason is shown rather than a zero count.
  objectsSkipped: string;
  textures: LodGeneratedTexture[];
  tiles: LodGeneratedTile[];
}

export interface LodGenerateResult {
  report: LodGenerateReport;
  ledger: string;
}

/** Invariant: report and committed ledger arrive together, so no ledger is adopted for tiles not written. */
export function parseGenerate(text: string): LodGenerateResult {
  const failure = hostFailure(text);
  if (failure !== null) throw new Error(failure);
  const parsed: unknown = JSON.parse(text);
  if (!isRecord(parsed) || !isRecord(parsed.report)) throw new Error('LOD generate: expected a report');
  // Invariant: a reply without a ledger is malformed, never an empty ledger.
  if (typeof parsed.ledger !== 'string') throw new Error('LOD generate: expected a ledger');
  const raw = parsed.report;
  const count = (value: unknown) => (isNumber(value) ? value : 0);
  // Constraint: entry order is the writer's reason order.
  const objectDrops = (isRecord(raw.objectDrops) ? Object.entries(raw.objectDrops) : [])
    .filter((entry): entry is [string, number] => isNumber(entry[1]) && entry[1] > 0)
    .map(([reason, value]): LodObjectDrop => ({ reason, count: value }));
  const rawTextures = Array.isArray(raw.textures) ? raw.textures : [];
  const textures = rawTextures.filter(isRecord).map((texture): LodGeneratedTexture => ({
    world: typeof texture.world === 'string' ? texture.world : '',
    kind: typeof texture.kind === 'string' ? texture.kind : 'texture',
    map: typeof texture.map === 'string' ? texture.map : 'atlas',
    side: count(texture.side),
    status: typeof texture.status === 'string' ? texture.status : 'unknown',
    bytes: count(texture.bytes),
    error: typeof texture.error === 'string' ? texture.error : '',
  }));
  const rawTiles = Array.isArray(raw.tiles) ? raw.tiles : [];
  const tiles = rawTiles.filter(isRecord).map((tile): LodGeneratedTile => ({
    world: typeof tile.world === 'string' ? tile.world : '',
    kind: typeof tile.kind === 'string' ? tile.kind : 'terrain',
    level: count(tile.level),
    x: count(tile.x),
    y: count(tile.y),
    status: typeof tile.status === 'string' ? tile.status : 'unknown',
    bytes: count(tile.bytes),
    error: typeof tile.error === 'string' ? tile.error : '',
  }));
  return {
    report: {
      apply: raw.apply === true,
      cancelled: raw.cancelled === true,
      written: count(raw.written),
      wouldWrite: count(raw.wouldWrite),
      unsupportedCandidates: count(raw.unsupportedCandidates),
      objectDrops,
      failed: count(raw.failed),
      unreadableWorlds: count(raw.unreadableWorlds),
      empty: count(raw.empty),
      atlasEntries: count(raw.atlasEntries),
      atlasMissingTextures: count(raw.atlasMissingTextures),
      atlasClampedShapes: count(raw.atlasClampedShapes),
      objectMissingMeshes: count(raw.objectMissingMeshes),
      objectDroppedTriangles: count(raw.objectDroppedTriangles),
      texturesWritten: count(raw.texturesWritten),
      texturesSkipped: count(raw.texturesSkipped),
      terrainTexturesWritten: count(raw.terrainTexturesWritten),
      terrainTexturesSkipped: count(raw.terrainTexturesSkipped),
      terrainTexturesEmpty: count(raw.terrainTexturesEmpty),
      objectsSkipped: typeof raw.objectsSkipped === 'string' ? raw.objectsSkipped : '',
      textures,
      tiles,
    },
    ledger: parsed.ledger,
  };
}

/** Constraint: ledgerText is the plan's ledger, not its commit lines, or the host skips tiles that need a bake. */
export async function generateTiles(
  pluginPaths: string,
  dataDir: string,
  archivePaths: string,
  ledgerText: string,
  apply: boolean,
): Promise<LodGenerateResult> {
  return parseGenerate(await lodHost().Generate(pluginPaths, dataDir, archivePaths, ledgerText, apply));
}

export async function generateWorldTiles(
  pluginPaths: string,
  dataDir: string,
  archivePaths: string,
  world: string,
  ledgerText: string,
  apply: boolean,
): Promise<LodGenerateResult> {
  return parseGenerate(await lodHost().GenerateWorld(pluginPaths, dataDir, archivePaths, world, ledgerText, apply));
}

/** Invariant: progress is polled from the host, so a bake reports tiles before the whole run answers. */
export interface LodScopedProgress {
  done: number;
  total: number;
  kind: string;
  level: number;
}

export function parseProgress(text: string): LodScopedProgress {
  const failure = hostFailure(text);
  if (failure !== null) throw new Error(failure);
  const parsed: unknown = JSON.parse(text);
  if (!isRecord(parsed)) throw new Error('LOD progress: expected an object');
  const count = (value: unknown) => (isNumber(value) ? value : 0);
  return {
    done: count(parsed.done),
    total: count(parsed.total),
    kind: typeof parsed.kind === 'string' ? parsed.kind : '',
    level: count(parsed.level),
  };
}

export function describeScopedProgress(progress: LodScopedProgress): string {
  const kind = progress.kind.length > 0 ? ' (' + progress.kind + ')' : '';
  return String(progress.done) + ' of ' + String(progress.total) + ' tile(s) processed' + kind;
}

/** Constraint: the order must match LodTarget's (world, ledger, apply, levels), or the host reads a write as a dry run. */
export async function generateScopedTiles(
  pluginPaths: string,
  dataDir: string,
  archivePaths: string,
  world: string,
  ledgerText: string,
  apply: boolean,
  levelMin: number,
  levelMax: number,
): Promise<LodGenerateResult> {
  return parseGenerate(await lodHost().GenerateScoped(pluginPaths, dataDir, archivePaths, world, ledgerText, apply,
                                                      levelMin, levelMax));
}

/** Invariant: only a tile the writer reported as written turns current, so a failed tile stays up for a retry. */
export function adoptGenerated(plan: LodPlan, result: LodGenerateResult): LodPlan {
  const written = result.report.tiles.filter(tile => tile.status === 'written');
  return {
    plan: plan.plan.map(tile => (written.some(done =>
      sameWorld(done.world, tile.world) && done.kind === tile.kind && done.level === tile.level &&
      done.x === tile.x && done.y === tile.y) ? { ...tile, rebuild: false } : tile)),
    ledger: result.ledger,
    // Constraint: the writer already recorded what it wrote, so commit stays empty.
    commit: '',
  };
}

/** Invariant: a dry run reads differently from a write. */
export function describeGenerate(report: LodGenerateReport): string {
  const unsupported = String(report.unsupportedCandidates) + ' unsupported object(s)';
  const parts = report.apply
    ? [String(report.written) + ' tile(s) written', String(report.failed) + ' failed (' + unsupported + ')']
    : [String(report.wouldWrite) + ' tile(s) would be written (' + unsupported + ')'];
  if (!report.apply || report.unreadableWorlds > 0) {
    parts.push(String(report.unreadableWorlds) + ' unreadable world(s)');
  }
  if (report.empty > 0) parts.push(String(report.empty) + ' empty tile(s)');
  if (report.objectsSkipped.length > 0) parts.push('object tiles skipped (' + report.objectsSkipped + ')');
  // Invariant: a cancelled run must read as cancelled, so a partial bake is never shown as a finished one.
  if (report.cancelled) parts.push('cancelled after ' + String(report.written) + ' tile(s)');
  const mapsWritten = report.texturesWritten + report.terrainTexturesWritten;
  const mapsSkipped = report.texturesSkipped + report.terrainTexturesSkipped;
  if (mapsWritten > 0) parts.push(String(mapsWritten) + ' texture map(s) written');
  if (mapsSkipped > 0) parts.push(String(mapsSkipped) + ' texture map(s) skipped');
  if (report.objectMissingMeshes > 0) parts.push(String(report.objectMissingMeshes) + ' missing mesh(es)');
  if (report.atlasMissingTextures > 0) parts.push(String(report.atlasMissingTextures) + ' missing atlas source(s)');
  if (report.atlasClampedShapes > 0) parts.push(String(report.atlasClampedShapes) + ' clamped shape(s)');
  if (report.objectDroppedTriangles > 0) parts.push(String(report.objectDroppedTriangles) + ' triangle(s) dropped');
  return parts.join(', ');
}
