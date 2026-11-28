import type { HkxHost } from './backend';
import './hkxChainHost';

export interface HkxChainSources {
  dataFolder: string;
  mo2Folder: string;
  profile: string;
}

export interface HkxChainLink {
  role: string;
  declared: string;
  resolved: string;
  source: string;
  exists: boolean;
  blocked: boolean;
  note: string;
}

export interface HkxProjectChain {
  root: string;
  links: HkxChainLink[];
  animations: HkxChainLink[];
  problems: string[];
  warnings: string[];
  ok: boolean;
  notes: string[];
}

export interface HkxCheckFinding {
  file: string;
  level: string;
  code: string;
  message: string;
}

export interface HkxRoundTripLoss {
  kind: string;
  object_id: number | null;
  member: string | null;
  message: string;
  blocks_save: boolean;
}

export interface HkxReportFinding {
  severity: string;
  code: string;
  message: string;
  file?: string;
}

export interface HkxReportFile {
  path: string;
  name: string;
  unreadable: string | null;
  errors: number;
  warnings: number;
  round_trip_losses: HkxRoundTripLoss[];
  findings: HkxReportFinding[];
}

export interface HkxProjectReport {
  project: string;
  summary: string;
  truncated: boolean;
  totals: {
    files: number;
    readable: number;
    unreadable: number;
    errors: number;
    warnings: number;
    round_trip_losses: number;
    files_with_round_trip_losses: number;
  };
  files: HkxReportFile[];
  project_findings: HkxReportFinding[];
}

export interface HkxProjectCheck {
  root: string;
  chain_problems: string[];
  chain_warnings: string[];
  files: number;
  read: number;
  truncated: boolean;
  complete: boolean;
  errors: number;
  warnings: number;
  findings: HkxCheckFinding[];
  unreadable: number;
  round_trip_losses: number;
  files_with_round_trip_losses: number;
  summary: string;
  report: HkxProjectReport;
  format?: string;
  rendered?: string;
  ok: boolean;
  notes: string[];
}

export interface HkxSubgraphIdManifest {
  source: string;
  path: string;
  behavior_paths: string[];
  animation_paths: string[];
  present: number;
  unresolved: string[];
}

export interface HkxSubgraphIdReply {
  id_source: string;
  manifest: HkxSubgraphIdManifest | null;
  notes: string[];
}

const parsed = <T extends { error?: string }>(raw: string | object): T => {
  const value = (typeof raw === 'string' ? JSON.parse(raw) : raw) as T | null;
  if (value === null || typeof value !== 'object') throw new Error('the host replied with no result');
  if (value.error) throw new Error(value.error);
  return value;
};

// Requires the host to carry the chain RPC; an older host build reports why instead of showing an empty chain.
export async function readProjectChain(host: HkxHost, hkxPath: string, sources: HkxChainSources): Promise<HkxProjectChain> {
  if (!host.ProjectChain) throw new Error('this host build cannot follow a project chain');
  const chain = parsed<HkxProjectChain & { error?: string }>(
    await host.ProjectChain(hkxPath, sources.dataFolder, sources.mo2Folder, sources.profile));
  if (![chain.links, chain.animations, chain.problems, chain.warnings, chain.notes].every(Array.isArray)) {
    throw new Error('project chain reply is missing a list');
  }
  return chain;
}

// Requires the host to carry the check RPC; an older host build reports why instead of showing a clean project.
export async function readProjectCheck(host: HkxHost, hkxPath: string, sources: HkxChainSources,
                                       limit: number, format?: 'csv' | 'html'): Promise<HkxProjectCheck> {
  if (!host.ProjectCheck) throw new Error('this host build cannot check a project');
  const raw = format
    ? await host.ProjectCheck(hkxPath, sources.dataFolder, sources.mo2Folder, sources.profile, limit, format)
    : await host.ProjectCheck(hkxPath, sources.dataFolder, sources.mo2Folder, sources.profile, limit);
  const check = parsed<HkxProjectCheck & { error?: string }>(raw);
  if (![check.findings, check.chain_problems, check.chain_warnings, check.notes].every(Array.isArray)) {
    throw new Error('project check reply is missing a list');
  }
  if (!Array.isArray(check.report?.files)) throw new Error('project check reply is missing the report');
  check.round_trip_losses ??= 0;
  check.files_with_round_trip_losses ??= 0;
  for (const file of check.report.files) file.round_trip_losses ??= [];
  return check;
}

// Requires an id that is a decimal string, because the host reads it as an unsigned 64-bit number.
export async function readSubgraphId(host: HkxHost, id: string, sources: HkxChainSources): Promise<HkxSubgraphIdReply> {
  if (!host.SubgraphManifest) throw new Error('this host build cannot read a subgraph by id');
  const reply = parsed<HkxSubgraphIdReply & { error?: string }>(
    await host.SubgraphManifest(id, '', null, sources.dataFolder, sources.mo2Folder, sources.profile));
  if (!Array.isArray(reply.notes)) throw new Error('subgraph reply has no notes list');
  return reply;
}

// Invariant: a clean verdict needs ok and complete true, truncated false and round_trip_losses the number 0, so a reply missing a field never reads as clean.
export const isCleanCheck = (check: HkxProjectCheck): boolean =>
  check.ok === true && check.complete === true && check.truncated === false && check.round_trip_losses === 0;

export const describeCheck = (check: HkxProjectCheck): string => {
  const scope = `${check.read} of ${check.files} behaviour file(s) read, ${check.errors} error(s), ${check.warnings} warning(s), ` +
    `${check.round_trip_losses} round-trip issue(s)`;
  if (check.truncated !== false || check.complete !== true) return `Incomplete check, not clean: ${scope}`;
  if (!isCleanCheck(check)) return `Check found problems: ${scope}`;
  return `Check clean: ${scope}`;
};

// Constraint: only files with an unreadable reason or a round-trip loss are listed; a clean file adds no line.
export const describeFileProblems = (report: HkxProjectReport): string[] =>
  report.files.flatMap(file => [
    ...(file.unreadable ? [`${file.name}: could not be unpacked, ${file.unreadable}`] : []),
    ...file.round_trip_losses.map(loss => `${file.name}: round-trip ${loss.kind}, ${loss.message}`),
  ]);

// Invariant: the chain verdict needs ok true and no problems, so a host that drops the flag cannot read as a good chain.
export const describeChain = (chain: HkxProjectChain): string => {
  const missing = [...chain.links, ...chain.animations].filter(link => !link.exists).length;
  const scope = `${chain.links.length} link(s), ${chain.animations.length} animation(s), ${missing} missing, ${chain.problems.length} problem(s)`;
  return chain.ok === true && chain.problems.length === 0 ? `Chain ok: ${scope}` : `Chain has problems: ${scope}`;
};

// Constraint: the line names unresolved animations by count, so a manifest with gaps never reads as complete.
export const describeSubgraphId = (reply: HkxSubgraphIdReply): string => {
  if (!reply.manifest) return 'No manifest read for this id';
  return `${reply.manifest.present} of ${reply.manifest.animation_paths.length} animations present, ` +
    `${reply.manifest.unresolved.length} unresolved`;
};
