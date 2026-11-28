import type { HkxHost } from './backend';
import type { HkxChainSources } from './hkxChainClient';
import './hkxSweepHost';

export interface HkxWeaponGap {
  leaf: string;
  message: string;
  crash: boolean;
}

export interface HkxSweepFailure {
  id: string;
  behavior: string;
  gap_total: number;
  crash_gaps: number;
  gaps: HkxWeaponGap[];
}

export interface HkxSubgraphSweep {
  ok: boolean;
  complete: boolean;
  manifests: number;
  duplicate_ids: number;
  weapon_checked: number;
  behaviors_read: number;
  behaviors_missing: number;
  behaviors_unreadable: number;
  manifests_unread: number;
  unchecked: number;
  failures_total: number;
  failures: HkxSweepFailure[];
  behaviors_skipped?: number;
  truncated_manifests?: boolean;
  truncated_behaviors?: boolean;
  truncated_failures?: boolean;
  incomplete?: boolean;
  weapon_sets: number;
  weapon_sets_reason: string;
  notes: string[];
}

export interface HkxSubgraphDiff {
  complete: boolean;
  vanilla_manifests: number;
  modded_manifests: number;
  new_ids_total: number;
  gone_ids_total: number;
  new_behaviors_total: number;
  new_weapon_total: number;
  clusters_total?: number;
  weapon_unread?: number;
  weapon_missing?: number;
  weapon_unreadable?: number;
  weapon_skipped?: number;
  manifests_unread?: number;
  truncated_manifests?: boolean;
  truncated_lists?: boolean;
  truncated_weapon_reads?: boolean;
  incomplete?: boolean;
  new_ids: string[];
  gone_ids: string[];
  new_behaviors: string[];
  clusters: { behavior: string; new_manifests: number }[];
  new_weapon_behaviors: string[];
  notes: string[];
}

export interface HkxCrashSubgraph {
  refusal: string;
  id_found: boolean;
  id: string | null;
  matched: string;
  confirmed: boolean;
  manifest_found: boolean;
  manifest: string;
  source: string;
  behavior_paths: string[];
  animation_total: number;
  animation_present: number;
  missing_total: number;
  missing: string[];
  offsets_found: boolean;
  offsets_hint: string;
  weapon_subgraph: boolean;
  gap_total: number;
  gaps: HkxWeaponGap[];
  unchecked_behaviors?: string[];
  incomplete?: boolean;
  notes: string[];
}

export interface HkxModlistFinding {
  kind: string;
  path: string;
  source: string;
  details: string[];
  detail_total: number;
}

export interface HkxModlistScan {
  mode: string;
  ok: boolean;
  complete: boolean;
  mod_roots: number;
  candidates: number;
  scanned: number;
  checked: number;
  broken: number;
  missing: number;
  unreadable: number;
  round_trip_losses: number;
  findings_total: number;
  findings: HkxModlistFinding[];
  truncated_files?: boolean;
  truncated_findings?: boolean;
  truncated_animations?: boolean;
  incomplete?: boolean;
  notes: string[];
}

const parsed = <T extends { error?: string }>(raw: string | object): T => {
  const value = (typeof raw === 'string' ? JSON.parse(raw) : raw) as T | null;
  if (value === null || typeof value !== 'object') throw new Error('the host replied with no result');
  if (value.error) throw new Error(value.error);
  return value;
};

const needLists = (what: string, lists: unknown[]) => {
  if (!lists.every(Array.isArray)) throw new Error(`${what} reply is missing a list`);
};

// Requires the host to carry the sweep RPC; an older host build reports why instead of showing a clean sweep.
export async function readSubgraphSweep(host: HkxHost, sources: HkxChainSources): Promise<HkxSubgraphSweep> {
  if (!host.SubgraphSweep) throw new Error('this host build cannot sweep subgraphs');
  const sweep = parsed<HkxSubgraphSweep & { error?: string }>(
    await host.SubgraphSweep(sources.dataFolder, sources.mo2Folder, sources.profile));
  needLists('subgraph sweep', [sweep.failures, sweep.notes]);
  return sweep;
}

// Requires the host to carry the diff RPC, and an MO2 instance among the sources.
export async function readSubgraphDiff(host: HkxHost, sources: HkxChainSources): Promise<HkxSubgraphDiff> {
  if (!host.SubgraphDiff) throw new Error('this host build cannot compare subgraphs');
  const diff = parsed<HkxSubgraphDiff & { error?: string }>(
    await host.SubgraphDiff(sources.dataFolder, sources.mo2Folder, sources.profile));
  needLists('subgraph diff', [diff.new_ids, diff.gone_ids, diff.new_behaviors, diff.clusters, diff.new_weapon_behaviors, diff.notes]);
  return diff;
}

// Requires a log path or its text; the host refuses a log over its size limit and names that in refusal.
export async function readCrashSubgraph(host: HkxHost, logPath: string, text: string, sources: HkxChainSources): Promise<HkxCrashSubgraph> {
  if (!host.CrashSubgraph) throw new Error('this host build cannot read a crash log');
  const crash = parsed<HkxCrashSubgraph & { error?: string }>(
    await host.CrashSubgraph(logPath, text, sources.dataFolder, sources.mo2Folder, sources.profile));
  needLists('crash subgraph', [crash.behavior_paths, crash.missing, crash.gaps, crash.notes]);
  return crash;
}

// Requires the host to carry the scan RPC and an MO2 instance among the sources.
export async function readModlistScan(host: HkxHost, mode: 'characters' | 'clips', sources: HkxChainSources): Promise<HkxModlistScan> {
  if (!host.ModlistScan) throw new Error('this host build cannot scan a modlist');
  const scan = parsed<HkxModlistScan & { error?: string }>(
    await host.ModlistScan(mode, sources.dataFolder, sources.mo2Folder, sources.profile));
  needLists('modlist scan', [scan.findings, scan.notes]);
  return scan;
}

// Invariant: a clean verdict needs ok and complete both true, so a reply missing a field never reads as clean.
export const isCleanSweep = (sweep: HkxSubgraphSweep): boolean => sweep.ok === true && sweep.complete === true;

export const describeSweep = (sweep: HkxSubgraphSweep): string => {
  const scope = `${sweep.manifests} manifest(s), ${sweep.weapon_checked} weapon subgraph(s) checked, ${sweep.failures_total} with gaps`;
  if (sweep.complete !== true) return `Incomplete sweep, not clean: ${scope}`;
  return sweep.ok === true ? `Sweep clean: ${scope}` : `Sweep found gaps: ${scope}`;
};

export const describeDiff = (diff: HkxSubgraphDiff): string => {
  const scope = `${diff.new_ids_total} new id(s), ${diff.gone_ids_total} gone, ${diff.new_behaviors_total} new behaviour(s), ` +
    `${diff.new_weapon_total} new weapon behaviour(s)`;
  return diff.complete === true ? `Diff complete: ${scope}` : `Incomplete diff, lists may be cut: ${scope}`;
};

export const describeCrash = (crash: HkxCrashSubgraph): string => {
  if (crash.refusal) return `Refused: ${crash.refusal}`;
  if (!crash.id_found || crash.id === null) return 'No subgraph id found in the crash input';
  const how = crash.confirmed === true ? 'confirmed by an AnimationOffsets path' : 'unconfirmed, the first long number in the input';
  return crash.manifest_found
    ? `Subgraph ${crash.id} (${how}): ${crash.animation_present} of ${crash.animation_total} animations present, ${crash.missing_total} missing`
    : `Subgraph ${crash.id} (${how}): no manifest found`;
};

export const describeModlist = (scan: HkxModlistScan): string => {
  const scope = `${scan.scanned} of ${scan.candidates} file(s) scanned, ${scan.missing} missing animation(s), ` +
    `${scan.unreadable} unreadable, ${scan.round_trip_losses} round-trip issue(s)`;
  if (scan.complete !== true) return `Incomplete scan, not clean: ${scope}`;
  return scan.ok === true ? `Scan clean: ${scope}` : `Scan found problems: ${scope}`;
};

const shown = (what: string, list: unknown[] | undefined, total: number | undefined): string[] =>
  Array.isArray(list) && typeof total === 'number' && total > list.length ? [`${what}: ${list.length} shown of ${total}`] : [];

const flagged = (on: boolean | undefined, text: string): string[] => (on === true ? [text] : []);

const counted = (count: number | undefined, text: string): string[] => (typeof count === 'number' && count > 0 ? [`${count} ${text}`] : []);

// Invariant: only restates totals and flags the host sent; a reply without a total adds no line.
export const sweepCuts = (sweep: HkxSubgraphSweep): string[] => [
  ...shown('failures', sweep.failures, sweep.failures_total),
  ...sweep.failures.flatMap(failure => shown(`gaps of ${failure.id}`, failure.gaps, failure.gap_total)),
  ...flagged(sweep.truncated_manifests, 'the manifest list was cut at the host limit'),
  ...flagged(sweep.truncated_behaviors, 'the behaviour limit was reached'),
  ...counted(sweep.manifests_unread, 'manifest(s) could not be read'),
  ...counted(sweep.behaviors_missing, 'behaviour file(s) named by a manifest are missing'),
  ...counted(sweep.behaviors_unreadable, 'behaviour file(s) could not be read'),
  ...flagged(sweep.truncated_failures, 'the failure list was cut'),
  ...flagged(sweep.incomplete, 'the file listing was incomplete, see the notes'),
  ...counted(sweep.unchecked, 'subgraph(s) not checked'),
  ...counted(sweep.behaviors_skipped, 'behaviour path(s) not under a Behaviors folder were skipped'),
];

export const diffCuts = (diff: HkxSubgraphDiff): string[] => [
  ...shown('new ids', diff.new_ids, diff.new_ids_total),
  ...shown('gone ids', diff.gone_ids, diff.gone_ids_total),
  ...shown('new behaviours', diff.new_behaviors, diff.new_behaviors_total),
  ...shown('clusters', diff.clusters, diff.clusters_total),
  ...shown('new weapon behaviours', diff.new_weapon_behaviors, diff.new_weapon_total),
  ...flagged(diff.truncated_manifests, 'the manifest list was cut at the host limit'),
  ...flagged(diff.truncated_weapon_reads, 'the weapon read limit was reached'),
  ...counted(diff.manifests_unread, 'manifest(s) could not be read'),
  ...counted(diff.weapon_missing, 'new behaviour file(s) are missing'),
  ...counted(diff.weapon_unreadable, 'new behaviour file(s) could not be read'),
  ...flagged(diff.incomplete, 'the file listing was incomplete, see the notes'),
  ...counted(diff.weapon_unread, 'new behaviour(s) not read for weapon clips'),
  ...counted(diff.weapon_skipped, 'new behaviour path(s) not under a Behaviors folder were skipped'),
];

export const crashCuts = (crash: HkxCrashSubgraph): string[] => [
  ...shown('missing animations', crash.missing, crash.missing_total),
  ...shown('weapon gaps', crash.gaps, crash.gap_total),
  ...(crash.unchecked_behaviors ?? []).map(line => `not checked: ${line}`),
  ...flagged(crash.incomplete, 'the file listing was incomplete, see the notes'),
];

export const scanCuts = (scan: HkxModlistScan): string[] => [
  ...shown('findings', scan.findings, scan.findings_total),
  ...scan.findings.flatMap(finding => shown(`details of ${finding.path}`, finding.details, finding.detail_total)),
  ...(scan.truncated_files === true ? [`files: ${scan.scanned} scanned of ${scan.candidates}`] : []),
  ...flagged(scan.truncated_animations, 'the animation check limit was reached'),
  ...flagged(scan.truncated_findings, 'the findings list was cut at the host limit'),
  ...counted(scan.unreadable, 'file(s) could not be read'),
  ...flagged(scan.incomplete, 'the file listing was incomplete, see the notes'),
];
