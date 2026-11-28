import { getDebugTools } from './backend';
import { errorText } from './errors';
import { EVIDENCE_KINDS, parseDiagResults, type DiagContext, type DiagResult, type EvidenceKind } from './diagnosticModel';

export type ParamKind = 'plugin' | 'asset' | 'dir' | 'text';
export type InventoryStatus = 'integrated' | 'available' | 'gap';

export interface ToolParam {
  name: string;
  label: string;
  kind: ParamKind;
  required: boolean;
}

export interface ToolInfo {
  id: string;
  title: string;
  description: string;
  readOnly: boolean;
  writes: boolean;
  needsOutputDir: boolean;
  expensive: boolean;
  cancellable: boolean;
  available: boolean;
  unavailableReason: string;
  evidence: EvidenceKind[];
  params: ToolParam[];
}

export interface ToolGroup {
  id: string;
  title: string;
  tools: ToolInfo[];
}

export interface InventoryRow {
  area: string;
  capability: string;
  status: InventoryStatus;
  toolId: string;
  note: string;
}

export interface ToolList {
  groups: ToolGroup[];
  inventory: InventoryRow[];
}

export interface ToolProgress {
  done: number;
  total: number;
  text: string;
}

export interface ToolStatus {
  active: boolean;
  cancelled: boolean;
  runId: string;
  toolId: string;
  progress: ToolProgress;
  results: DiagResult[];
  error: string;
  // Invariant: ISO time the host stored the results, empty while the run is active or before any run.
  finishedAt?: string;
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);
const text = (value: unknown): string => (typeof value === 'string' ? value : '');
// Invariant: the host sends runId as a JSON number; reading it as text only would drop a started run.
const idText = (value: unknown): string => (typeof value === 'number' && Number.isFinite(value) ? String(value) : text(value));
const num = (value: unknown): number => (typeof value === 'number' && Number.isFinite(value) ? value : 0);
const strings = (value: unknown): string[] =>
  Array.isArray(value) ? value.map(item => (typeof item === 'string' ? item : JSON.stringify(item))) : [];
const records = (value: unknown): Record<string, unknown>[] => (Array.isArray(value) ? value.filter(isRecord) : []);

const PARAM_KINDS: readonly string[] = ['plugin', 'asset', 'dir', 'text'];
const INVENTORY_STATUSES: readonly string[] = ['integrated', 'available', 'gap'];

function parseJson(raw: string, what: string): Record<string, unknown> {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch {
    throw new Error(`${what}: the host did not return JSON`);
  }
  if (!isRecord(parsed)) throw new Error(`${what}: expected an object`);
  return parsed;
}

export function parseContext(raw: string): DiagContext {
  const parsed = parseJson(raw, 'Debug tools context');
  const versions = isRecord(parsed.versions) ? parsed.versions : {};
  const settings: Record<string, string> = {};
  if (isRecord(parsed.settings)) {
    for (const [key, value] of Object.entries(parsed.settings)) settings[key] = typeof value === 'string' ? value : JSON.stringify(value);
  }
  return {
    versions: { fo4ide: text(versions.fo4ide), radicleAbi: text(versions.radicleAbi), commit: text(versions.commit) },
    dataDir: text(parsed.dataDir),
    mo2Profile: text(parsed.mo2Profile),
    loadOrder: strings(parsed.loadOrder),
    assetRootsLowToHigh: strings(parsed.assetRootsLowToHigh),
    settings,
    gameBuild: text(parsed.gameBuild),
  };
}

function parseTool(row: Record<string, unknown>): ToolInfo {
  const writes = row.writes === true;
  return {
    id: text(row.id),
    title: text(row.title) || text(row.id),
    description: text(row.description),
    // Invariant: a tool that writes is never shown as read-only, whatever readOnly says.
    readOnly: row.readOnly === true && !writes,
    writes,
    needsOutputDir: row.needsOutputDir === true,
    expensive: row.expensive === true,
    cancellable: row.cancellable === true,
    // Invariant: a tool is available only when the host says true.
    available: row.available === true,
    unavailableReason: text(row.unavailableReason),
    evidence: strings(row.evidence).filter((kind): kind is EvidenceKind => (EVIDENCE_KINDS as readonly string[]).includes(kind)),
    params: records(row.params).map(param => ({
      name: text(param.name),
      label: text(param.label) || text(param.name),
      kind: (PARAM_KINDS.includes(text(param.kind)) ? text(param.kind) : 'text') as ParamKind,
      required: param.required === true,
    })).filter(param => param.name.length > 0),
  };
}

export function parseToolList(raw: string): ToolList {
  const parsed = parseJson(raw, 'Debug tools list');
  return {
    groups: records(parsed.groups).map(group => ({
      id: text(group.id),
      title: text(group.title) || text(group.id),
      tools: records(group.tools).map(parseTool).filter(tool => tool.id.length > 0),
    })),
    inventory: records(parsed.inventory).map(row => ({
      area: text(row.area),
      capability: text(row.capability),
      // Invariant: an unknown inventory status reads as gap, never as integrated.
      status: (INVENTORY_STATUSES.includes(text(row.status)) ? text(row.status) : 'gap') as InventoryStatus,
      toolId: text(row.toolId),
      note: text(row.note),
    })),
  };
}

export function parseToolStatus(raw: string): ToolStatus {
  const parsed = parseJson(raw, 'Debug tools status');
  const progress = isRecord(parsed.progress) ? parsed.progress : {};
  return {
    active: parsed.active === true,
    runId: idText(parsed.runId),
    cancelled: parsed.cancelled === true,
    toolId: text(parsed.toolId),
    progress: { done: num(progress.done), total: num(progress.total), text: text(progress.text) },
    results: parseDiagResults(parsed.results),
    error: text(parsed.error),
    finishedAt: text(parsed.finishedAt),
  };
}

export function parseRunId(raw: string): string {
  const runId = idText(parseJson(raw, 'Debug tools start').runId);
  if (!runId) throw new Error('Debug tools start: the host returned no runId');
  return runId;
}

export function debugToolsHost() {
  const host = getDebugTools();
  if (!host) throw new Error('Debug tools are not available: this FO4IDE host has no debugTools object');
  return host;
}

// Invariant: the HTTP bridge proxies every host object name, so only a failing Status call proves the target is missing.
export async function debugToolsProblem(): Promise<string> {
  try {
    const host = getDebugTools();
    if (!host) return 'this FO4IDE host has no debugTools object';
    await host.Status();
    return '';
  } catch (e) {
    return errorText(e);
  }
}

export const loadContext = async () => parseContext(await debugToolsHost().Context());
export const loadList = async () => parseToolList(await debugToolsHost().List());
export const loadStatus = async () => parseToolStatus(await debugToolsHost().Status());
export const startRun = async (toolId: string, params: Record<string, string>) =>
  parseRunId(await debugToolsHost().Start(toolId, JSON.stringify(params)));
export const cancelRun = async (): Promise<boolean> =>
  parseJson(await debugToolsHost().Cancel(), 'Debug tools cancel').cancelled === true;

const normPath = (value: string) => value.trim().replace(/\\/g, '/').replace(/^\/\/[?.]\/UNC\//i, '//').replace(/^\/\/[?.]\//, '').replace(/\/+/g, '/').replace(/\/$/, '').toLowerCase();

export function insideDir(path: string, dir: string): boolean {
  const target = normPath(path);
  const root = normPath(dir);
  return root.length > 0 && (target === root || target.startsWith(`${root}/`));
}

const isAbsolutePath = (path: string) => /^[A-Za-z]:[\\/]/.test(path) || /^\\\\[^\\]/.test(path) || path.startsWith('/');

// Constraint: a convenience guard only; the host's validateScratchParent is the enforcement.
export function outputDirProblem(path: string, dataDir: string): string {
  if (!isAbsolutePath(path)) return 'the output folder must be an absolute path (drive letter, UNC or leading /)';
  if (path.split(/[\\/]+/).includes('..')) return "the output folder must not contain '..'";
  if (!dataDir.trim()) return 'the Fallout 4 Data folder is unknown, so the output folder cannot be checked';
  if (insideDir(path, dataDir)) return 'the output folder must not be the Fallout 4 Data folder';
  return '';
}
