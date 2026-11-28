import { getLod } from './backend';

export interface LodWorld {
  name: string;
  southwestX: number;
  southwestY: number;
  stride: number;
  levelMin: number;
  levelMax: number;
}

export interface LodTilePlan {
  world: string;
  kind: string;
  level: number;
  x: number;
  y: number;
  inputs: number;
  rebuild: boolean;
}

export interface LodPlan {
  plan: LodTilePlan[];
  ledger: string;
  commit: string;
}

export const isNumber = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value);

export const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);

/** Invariant: host failures arrive as host strings; the panel must never render them as data. */
export function hostFailure(text: string): string | null {
  return text.startsWith('Error:') ? text.slice('Error:'.length).trim() : null;
}

export interface LodCandidateSkip {
  reason: string;
  count: number;
}

export interface LodCandidates {
  world: string;
  level: number;
  tiles: number;
  referencesSeen: number;
  included: number;
  skipped: LodCandidateSkip[];
}

export function parseCandidates(text: string): LodCandidates {
  const failure = hostFailure(text);
  if (failure !== null) throw new Error(failure);
  const parsed: unknown = JSON.parse(text);
  if (!isRecord(parsed)) throw new Error('LOD candidates: expected an object');
  const counts = isRecord(parsed.counts) ? parsed.counts : {};
  const rawSkipped = isRecord(counts.skipped) ? counts.skipped : {};
  const skipped = Object.entries(rawSkipped)
    .filter((entry): entry is [string, number] => isNumber(entry[1]) && entry[1] > 0)
    .map(([reason, count]): LodCandidateSkip => ({ reason, count }));
  const raw = Array.isArray(parsed.candidates) ? parsed.candidates : [];
  for (const [index, entry] of raw.entries()) {
    if (!isRecord(entry) || typeof entry.reference !== 'string' || entry.reference.length === 0) {
      throw new Error('LOD candidates: entry ' + String(index) + ' is missing its reference');
    }
  }
  return {
    world: typeof parsed.world === 'string' ? parsed.world : '',
    level: isNumber(parsed.level) ? parsed.level : 0,
    tiles: isNumber(parsed.tiles) ? parsed.tiles : 0,
    referencesSeen: isNumber(counts.referencesSeen) ? counts.referencesSeen : 0,
    included: isNumber(counts.included) ? counts.included : 0,
    skipped,
  };
}

export async function listCandidates(
  pluginPaths: string,
  dataDir: string,
  archivePaths: string,
  world: string,
  level: number,
): Promise<LodCandidates> {
  return parseCandidates(await lodHost().Candidates(pluginPaths, dataDir, archivePaths, world, level));
}

export function describeCandidates(candidates: LodCandidates): string {
  return String(candidates.included) + ' of ' + String(candidates.referencesSeen) + ' reference(s) enter ' +
    String(candidates.tiles) + ' tile(s)';
}

/** Invariant: a world entry must carry every picker field before the panel can use it. */
export function parseWorlds(text: string): LodWorld[] {
  const failure = hostFailure(text);
  if (failure !== null) throw new Error(failure);
  const parsed: unknown = JSON.parse(text);
  if (!Array.isArray(parsed)) throw new Error('LOD worlds: expected an array');
  return parsed.map((entry: unknown, index: number) => {
    if (!isRecord(entry) || typeof entry.name !== 'string' || !isNumber(entry.stride)) {
      throw new Error('LOD worlds: entry ' + String(index) + ' is missing name or stride');
    }
    return {
      name: entry.name,
      southwestX: isNumber(entry.southwestX) ? entry.southwestX : 0,
      southwestY: isNumber(entry.southwestY) ? entry.southwestY : 0,
      stride: entry.stride,
      levelMin: isNumber(entry.levelMin) ? entry.levelMin : 0,
      levelMax: isNumber(entry.levelMax) ? entry.levelMax : 0,
    };
  });
}

/** Invariant: plan entries must name a world, level and coordinates so a tile is addressable. */
export function parsePlan(text: string): LodPlan {
  const failure = hostFailure(text);
  if (failure !== null) throw new Error(failure);
  const parsed: unknown = JSON.parse(text);
  if (!isRecord(parsed)) throw new Error('LOD plan: expected an object');
  const rawPlan = Array.isArray(parsed.plan) ? parsed.plan : [];
  const plan = rawPlan.map((entry: unknown, index: number) => {
    if (!isRecord(entry) || typeof entry.world !== 'string' || !isNumber(entry.level)) {
      throw new Error('LOD plan: entry ' + String(index) + ' is missing world or level');
    }
    return {
      world: entry.world,
      kind: typeof entry.kind === 'string' ? entry.kind : 'terrain',
      level: entry.level,
      x: isNumber(entry.x) ? entry.x : 0,
      y: isNumber(entry.y) ? entry.y : 0,
      inputs: isNumber(entry.inputs) ? entry.inputs : 0,
      rebuild: entry.rebuild === true,
    };
  });
  return {
    plan,
    ledger: typeof parsed.ledger === 'string' ? parsed.ledger : '',
    commit: typeof parsed.commit === 'string' ? parsed.commit : '',
  };
}

export function lodHost() {
  const lod = getLod();
  if (!lod) throw new Error('LOD host is unavailable');
  return lod;
}

export async function loadWorlds(pluginPaths: string, dataDir: string, archivePaths: string): Promise<LodWorld[]> {
  return parseWorlds(await lodHost().Worlds(pluginPaths, dataDir, archivePaths));
}

export async function planRebakes(
  pluginPaths: string,
  dataDir: string,
  archivePaths: string,
  ledgerText: string,
): Promise<LodPlan> {
  return parsePlan(await lodHost().Plan(pluginPaths, dataDir, archivePaths, ledgerText));
}

export async function commitLedger(ledgerText: string, commitText: string): Promise<string> {
  const text = await lodHost().Commit(ledgerText, commitText);
  const failure = hostFailure(text);
  if (failure !== null) throw new Error(failure);
  return text;
}

export async function previewTile(
  pluginPaths: string,
  dataDir: string,
  archivePaths: string,
  world: string,
  level: number,
  x: number,
  y: number,
  object: boolean,
): Promise<string> {
  // Invariant: object selects the host's BTO path, never the terrain NIF.
  const text = await lodHost().Preview(pluginPaths, dataDir, archivePaths, world, level, x, y, object);
  const failure = hostFailure(text);
  if (failure !== null) throw new Error(failure);
  return text;
}

/** Invariant: plan worlds are lower-cased by the ledger key, so comparisons must fold case. */
export function sameWorld(a: string, b: string): boolean {
  return a.toLowerCase() === b.toLowerCase();
}

export function describeProgress(total: number, rebuilt: number): string {
  if (total <= 0) return 'no tiles';
  return String(rebuilt) + ' of ' + String(total) + ' tile(s) need a rebake';
}

/** Invariant: LOD levels double from 4, so a picker only offers levels the world actually covers. */
export function levelOptions(world: LodWorld | null): number[] {
  if (!world) return [];
  const highest = world.levelMax > 0 ? world.levelMax : Math.max(world.levelMin, 4);
  let level = 4;
  while (level < world.levelMin) level *= 2;
  const levels: number[] = [];
  for (; level <= highest; level *= 2) levels.push(level);
  return levels.length > 0 ? levels : [Math.max(world.levelMin, 4)];
}

/** Invariant: a plan world is matched case-insensitively, because the ledger key lower-cases it. */
export function tilesFor(plan: LodPlan | null, world: string, level: number): LodTilePlan[] {
  if (!plan || !world) return [];
  return plan.plan.filter(tile => sameWorld(tile.world, world) && tile.level === level);
}

/** Invariant: a tile is addressed by kind, level and coordinates, so the key carries all four. */
export function tileKey(tile: LodTilePlan): string {
  return `${tile.kind}:${tile.level}:${tile.x}:${tile.y}`;
}

export interface LodVerifyTile {
  world: string;
  kind: string;
  level: number;
  x: number;
  y: number;
  state: string;
  local: string;
  ledger: string;
  bytes: number;
  inArchive: boolean;
}

export interface LodVerifyTexture {
  world: string;
  kind: string;
  map: string;
  state: string;
  ledger: string;
  bytes: number;
}

export interface LodVerifyReport {
  missing: number;
  partial: number;
  ok: number;
  stale: number;
  unverified: number;
  empty: number;
  texturesOk: number;
  texturesMissing: number;
  texturesStale: number;
  texturesUnverified: number;
  tiles: LodVerifyTile[];
  textures: LodVerifyTexture[];
}

export function parseVerify(text: string): LodVerifyReport {
  const failure = hostFailure(text);
  if (failure !== null) throw new Error(failure);
  const parsed: unknown = JSON.parse(text);
  if (!isRecord(parsed)) throw new Error('LOD verify: expected an object');
  const count = (value: unknown) => (isNumber(value) ? value : 0);
  const rawTiles = Array.isArray(parsed.tiles) ? parsed.tiles : [];
  const tiles = rawTiles.filter(isRecord).map((tile, index): LodVerifyTile => {
    if (typeof tile.world !== 'string' || typeof tile.state !== 'string') {
      throw new Error('LOD verify: tile ' + String(index) + ' is missing world or state');
    }
    return {
      world: tile.world,
      kind: typeof tile.kind === 'string' ? tile.kind : 'terrain',
      level: count(tile.level),
      x: count(tile.x),
      y: count(tile.y),
      state: tile.state,
      local: typeof tile.local === 'string' ? tile.local : 'unknown',
      ledger: typeof tile.ledger === 'string' ? tile.ledger : 'stale',
      bytes: count(tile.bytes),
      inArchive: tile.inArchive === true,
    };
  });
  const rawTextures = Array.isArray(parsed.textures) ? parsed.textures : [];
  const textures = rawTextures.filter(isRecord).map((texture, index): LodVerifyTexture => {
    if (typeof texture.world !== 'string' || typeof texture.map !== 'string') {
      throw new Error('LOD verify: texture ' + String(index) + ' is missing world or map');
    }
    return {
      world: texture.world,
      kind: typeof texture.kind === 'string' ? texture.kind : 'texture',
      map: texture.map,
      state: typeof texture.state === 'string' ? texture.state : 'unknown',
      ledger: typeof texture.ledger === 'string' ? texture.ledger : 'stale',
      bytes: count(texture.bytes),
    };
  });
  return {
    missing: count(parsed.missing),
    partial: count(parsed.partial),
    ok: count(parsed.ok),
    stale: count(parsed.stale),
    unverified: count(parsed.unverified),
    empty: count(parsed.empty),
    texturesOk: count(parsed.texturesOk),
    texturesMissing: count(parsed.texturesMissing),
    texturesStale: count(parsed.texturesStale),
    texturesUnverified: count(parsed.texturesUnverified),
    tiles,
    textures,
  };
}

export async function verifyTiles(
  pluginPaths: string,
  dataDir: string,
  archivePaths: string,
  ledgerText: string,
): Promise<LodVerifyReport> {
  return parseVerify(await lodHost().Verify(pluginPaths, dataDir, archivePaths, ledgerText));
}

export async function verifyWorldTiles(pluginPaths: string, dataDir: string, archivePaths: string, world: string,
                                       ledgerText: string): Promise<LodVerifyReport> {
  return parseVerify(await lodHost().VerifyWorld(pluginPaths, dataDir, archivePaths, world, ledgerText));
}

export function describeVerify(report: LodVerifyReport): string {
  const parts = [String(report.ok) + ' ok'];
  if (report.missing > 0) parts.push(String(report.missing) + ' missing');
  if (report.partial > 0) parts.push(String(report.partial) + ' partial');
  if (report.stale > 0) parts.push(String(report.stale) + ' stale');
  if (report.unverified > 0) parts.push(String(report.unverified) + ' unverified');
  if (report.empty > 0) parts.push(String(report.empty) + ' empty');
  if (report.texturesMissing > 0) parts.push(String(report.texturesMissing) + ' atlas map(s) missing');
  if (report.texturesStale > 0) parts.push(String(report.texturesStale) + ' atlas map(s) stale');
  return parts.join(', ');
}

export function verifyProblems(report: LodVerifyReport): LodVerifyTile[] {
  return report.tiles.filter(tile => tile.state !== 'ok' && tile.state !== 'empty' && tile.state !== 'shipped');
}
