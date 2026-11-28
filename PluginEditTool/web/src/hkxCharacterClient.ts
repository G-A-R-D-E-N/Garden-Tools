import type { HkxHost } from './backend';
import './hkxCharacterHost';

export interface HkxCharacterPaths {
  skeleton: string;
  ragdoll: string;
  mapper: string;
  bodyNif: string;
  skeletonNif: string;
  projectHkx: string;
  dataFolder: string;
  mo2Instance: string;
  mo2Profile: string;
}

export interface HkxCharacterSaveTargets {
  skeletonOut: string;
  ragdollOut: string;
  mapperOut: string;
  skin: string;
  skinOut: string;
  edits: string;
  ragdollEdits: string;
  mapperEdits: string;
  mapperIndex: string;
  skinEdits: string;
  overwrite: boolean;
}

export interface HkxCharacterChain {
  checked: boolean;
  root: string;
  links: number;
  animations: number;
  missing_animations: number;
  problems: string[];
  warnings: string[];
}

export interface HkxCharacterFinding {
  level: string;
  area: string;
  where: string;
  message: string;
}

export interface HkxCharacterRepair {
  kind: string;
  block: number;
  vertices: number[];
  total: number;
  reason: string;
}

export interface HkxCharacterShape {
  binding: { block: number; name: string; vertices: number; unmatched: number; complete: boolean; warning: string };
  skin_errors: number;
  skin_warnings: number;
  unbound_vertices: number;
  repairs: HkxCharacterRepair[];
}

export interface HkxCharacterReport {
  ok: boolean;
  errors: number;
  warnings: number;
  skeleton: { name: string; bones: number; mirror: { pairs: number; lane: number } };
  ragdoll: { present: boolean; status: string; headline: string; bodies: number; constraints: number; bone_bindings: number };
  mappers: { object_index: number; rows: number; chains: number }[];
  chain?: HkxCharacterChain;
  skin: { status: string; shapes: HkxCharacterShape[]; refused: { block: number; name: string; reason: string }[]; unskinned: number };
  findings: HkxCharacterFinding[];
}

export interface HkxCharacterSaveStep {
  document: number;
  kind: string;
  reads: string;
  writes: string;
  reads_previous_output: boolean;
  source_sha256?: string;
}

export interface HkxCharacterSavePlan {
  ok: boolean;
  steps: HkxCharacterSaveStep[];
  findings: HkxCharacterFinding[];
  shifted_references: number;
}

export interface HkxCharacterSavedStep {
  stage: number;
  kind: string;
  reads: string;
  writes: string;
  changed: boolean;
  written: boolean;
  bytes: number;
}

export interface HkxCharacterSaveResult {
  saved: boolean;
  refusal: string;
  partial: boolean;
  failed_stage: number;
  written: string[];
  backups?: string[];
  steps: HkxCharacterSavedStep[];
  findings: HkxCharacterFinding[];
  shifted_references: number;
}

const parsed = <T extends { error?: string }>(raw: string | object): T => {
  const value = (typeof raw === 'string' ? JSON.parse(raw) : raw) as T | null;
  if (value === null || typeof value !== 'object') throw new Error('the host replied with no result');
  if (value.error) throw new Error(value.error);
  return value;
};

// Requires the host to carry the check RPC; an older host build reports why instead of showing a clean character.
export async function readCharacterCheck(host: HkxHost, paths: HkxCharacterPaths): Promise<HkxCharacterReport> {
  if (!host.CharacterCheck) throw new Error('this host build cannot check a character');
  const report = parsed<HkxCharacterReport & { error?: string }>(
    await host.CharacterCheck(paths.skeleton, paths.ragdoll, paths.mapper, paths.bodyNif, paths.skeletonNif,
                              paths.projectHkx, paths.dataFolder, paths.mo2Instance, paths.mo2Profile));
  if (!Array.isArray(report.findings) || !Array.isArray(report.mappers) || !Array.isArray(report.skin?.shapes)) {
    throw new Error('character report is missing a list');
  }
  return report;
}

// Requires the host to carry the plan RPC; the plan only reads, the saves run through their own tools.
export async function readCharacterSavePlan(host: HkxHost, paths: HkxCharacterPaths,
                                            targets: HkxCharacterSaveTargets): Promise<HkxCharacterSavePlan> {
  if (!host.CharacterSavePlan) throw new Error('this host build cannot plan a character save');
  const plan = parsed<HkxCharacterSavePlan & { error?: string }>(
    await host.CharacterSavePlan(paths.skeleton, targets.skeletonOut, paths.ragdoll, targets.ragdollOut, paths.mapper,
                                 targets.mapperOut, targets.skin, targets.skinOut, targets.edits.trim() || undefined, targets.overwrite));
  if (!Array.isArray(plan.steps) || !Array.isArray(plan.findings)) throw new Error('save plan reply is missing a list');
  return plan;
}

// Constraint: the host (Radicle) plans, edits, checks and writes the whole chain; this only passes the fields and reads the result.
// Invariant: a result is saved only when the host says so; a refusal or partial failure is returned as data so the written files stay visible.
// Invariant: the hash the plan reported for a document's source goes back as its expected hash; a document the plan gave none for is not checked.
export async function readCharacterSave(host: HkxHost, paths: HkxCharacterPaths, targets: HkxCharacterSaveTargets,
                                        plan?: HkxCharacterSavePlan | null): Promise<HkxCharacterSaveResult> {
  if (!host.CharacterSave) throw new Error('this host build cannot save a character');
  const rawIndex = targets.mapperIndex.trim() || '0';
  if (!/^\d+$/.test(rawIndex) || Number(rawIndex) > 2147483647) throw new Error('mapper index must be a whole number, zero or more');
  const index = Number(rawIndex);
  const expected = (kind: string) => plan?.steps.find(s => s.kind === kind && !s.reads_previous_output)?.source_sha256 ?? '';
  const result = parsed<HkxCharacterSaveResult & { error?: string }>(
    await host.CharacterSave(paths.skeleton, targets.skeletonOut, targets.edits.trim(), expected('skeleton'),
                             paths.ragdoll, targets.ragdollOut, targets.ragdollEdits.trim(), expected('ragdoll'),
                             paths.mapper, targets.mapperOut, targets.mapperEdits.trim(), expected('mapper'), index,
                             targets.skin, targets.skinOut, targets.skinEdits.trim(), expected('skin'), targets.overwrite));
  if (typeof result.saved !== 'boolean' || !Array.isArray(result.written) || !Array.isArray(result.steps)) {
    throw new Error('save reply is missing a field');
  }
  return result;
}

export const describeSave = (r: HkxCharacterSaveResult): string =>
  r.saved ? `Saved ${r.written.length} file(s)` : r.partial ? `Save stopped after writing ${r.written.length} file(s): ${r.refusal}` : `Not saved: ${r.refusal}`;

// Invariant: a report is clean only when the host counted no error and listed no error finding.
export const isCleanCharacter = (report: HkxCharacterReport): boolean =>
  report.ok && report.errors === 0 && !report.findings.some(f => f.level === 'error');

export const describeCharacter = (report: HkxCharacterReport): string =>
  `${isCleanCharacter(report) ? 'Character OK' : 'Character has errors'}: ${report.skeleton.bones} bone(s), ` +
  `${report.errors} error(s), ${report.warnings} warning(s)`;

export const describeFinding = (f: HkxCharacterFinding): string => `${f.level} ${f.area} ${f.where}: ${f.message}`;

export const describeStep = (s: HkxCharacterSaveStep): string =>
  `${s.document}: ${s.reads} -> ${s.writes || '(in place)'}` + (s.reads_previous_output ? ' (take the previous output hash again)' : '');

export const describeRepair = (shape: HkxCharacterShape, r: HkxCharacterRepair): string =>
  `${shape.binding.name || `block ${shape.binding.block}`}: ${r.kind} on ${r.total} vertex(es), first ${r.vertices.join(', ')}: ${r.reason}`;
