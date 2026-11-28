import type { NifHost } from './backend';
import './skinHost';

export interface SkinInfluence {
  bone: number;
  name: string;
  weight: number;
}

export interface VertexInfluences {
  block: number;
  name: string;
  vertex: number;
  bones: string[];
  influences: SkinInfluence[];
  error: string;
}

export interface BoneInfluence {
  block: number;
  name: string;
  bone: number;
  boneName: string;
  vertices: number[];
  weight: number;
  minimum: number;
  min: number[];
  max: number[];
  error: string;
}

export interface SkinFinding {
  level: 'error' | 'warning';
  where: string;
  what: string;
}

export interface SkinReport {
  block: number;
  name: string;
  bones: number;
  bindTransforms: number;
  usedBones: number;
  ok: boolean;
  errors: number;
  warnings: number;
  findings: SkinFinding[];
}

export interface SkinEditReport {
  ok: boolean;
  kind: string;
  saved: string;
  block: number;
  changed: number;
  largest: number;
  mirrored: number | null;
  noPartnerVertex: number;
  ambiguousVertex: number;
  noPartnerBone: number;
  skipped: string;
  refusal: string;
}

export type SkinEditRequest = {
  kind: 'set_influences' | 'copy_bone' | 'mirror_weights';
  block: number;
  vertex?: number | null;
  influences?: { bone: number; weight: number }[] | null;
  fromBone?: number | null;
  toBone?: number | null;
  keepSource?: boolean;
  lane?: number;
  fromPositive?: boolean;
};

const firstErrorLine = (text: string): string => {
  const lines = text.split('\n').map(l => l.trim()).filter(l => l.length > 0);
  return (lines[0] ?? '').replace(/^Error:\s*/i, '') +
    (lines.length > 1 ? ` (${lines.slice(1).join('; ')})` : '');
};

export function parseVertexInfluences(text: string): VertexInfluences {
  const parsed = JSON.parse(text) as Partial<VertexInfluences>;
  if (parsed.error) throw new Error(parsed.error);
  return {
    block: parsed.block ?? 0,
    name: parsed.name ?? '',
    vertex: parsed.vertex ?? 0,
    bones: parsed.bones ?? [],
    influences: (parsed.influences ?? []).map(value => ({
      bone: value.bone, name: value.name ?? '', weight: value.weight,
    })),
    error: '',
  };
}

export function parseBoneInfluence(text: string): BoneInfluence {
  const parsed = JSON.parse(text) as Partial<BoneInfluence> & { bone_name?: string };
  if (parsed.error) throw new Error(parsed.error);
  return {
    block: parsed.block ?? 0,
    name: parsed.name ?? '',
    bone: parsed.bone ?? 0,
    boneName: parsed.bone_name ?? '',
    vertices: parsed.vertices ?? [],
    weight: parsed.weight ?? 0,
    minimum: parsed.minimum ?? 0,
    min: parsed.min ?? [],
    max: parsed.max ?? [],
    error: '',
  };
}

export function parseSkinReport(text: string): SkinReport {
  const parsed = JSON.parse(text) as Partial<SkinReport> & {
    bind_transforms?: number; used_bones?: number; error?: string;
  };
  if (parsed.error) throw new Error(parsed.error);
  return {
    block: parsed.block ?? 0,
    name: parsed.name ?? '',
    bones: parsed.bones ?? 0,
    bindTransforms: parsed.bind_transforms ?? 0,
    usedBones: parsed.used_bones ?? 0,
    ok: parsed.ok ?? false,
    errors: parsed.errors ?? 0,
    warnings: parsed.warnings ?? 0,
    findings: (parsed.findings ?? []).map(finding => ({
      level: finding.level === 'error' ? 'error' : 'warning',
      where: finding.where ?? '',
      what: finding.what ?? '',
    })),
  };
}

// Invariant: the summary names the error and warning counts, so a shape with warnings is not reported as clean.
export function describeSkinReport(report: SkinReport): string {
  const verdict = report.ok ? 'skin ok' : 'skin has errors';
  return `${verdict} (${report.name}): ${report.errors} error(s), ${report.warnings} warning(s), ` +
    `${report.usedBones}/${report.bones} bones used, ${report.bindTransforms} bind transform(s)`;
}

export function parseSkinEditReport(text: string): SkinEditReport {
  const ok = /RESULT: skin edit \w+ OK/.test(text);
  const kind = text.match(/RESULT: skin edit (\w+) (?:OK|failed)/);
  const saved = text.match(/Saved: ([^\n]*)/);
  const block = text.match(/Block: (\d+)/);
  const changed = text.match(/Vertices changed: (\d+), largest weight change ([0-9.eE+-]+)/);
  const mirrored = text.match(/Mirrored: (\d+), no partner vertex (\d+), ambiguous (\d+), no partner bone (\d+)/);
  const skipped = text.match(/Skipped bones: ([^\n]*)/);
  return {
    ok,
    kind: kind ? kind[1] : '',
    saved: saved ? saved[1].trim() : '',
    block: block ? Number(block[1]) : 0,
    changed: changed ? Number(changed[1]) : 0,
    largest: changed ? Number(changed[2]) : 0,
    mirrored: mirrored ? Number(mirrored[1]) : null,
    noPartnerVertex: mirrored ? Number(mirrored[2]) : 0,
    ambiguousVertex: mirrored ? Number(mirrored[3]) : 0,
    noPartnerBone: mirrored ? Number(mirrored[4]) : 0,
    skipped: skipped ? skipped[1].trim() : '',
    refusal: ok ? '' : firstErrorLine(text),
  };
}

export function describeSkinEditReport(report: SkinEditReport): string {
  if (!report.ok) return `refused -- ${report.refusal}`;
  const vertices = `${report.changed} vert${report.changed === 1 ? 'ex' : 'ices'} changed`;
  const mirrored = report.mirrored === null
    ? ''
    : `, ${report.mirrored} mirrored (${report.noPartnerVertex} with no partner vertex, ` +
      `${report.ambiguousVertex} with more than one, ${report.noPartnerBone} with no opposite bone)`;
  const skipped = report.skipped ? `, skipped bones ${report.skipped}` : '';
  return `${report.kind}: ${vertices}${mirrored}${skipped}`;
}

export interface SkinBindingShape {
  block: number;
  name: string;
  vertices: number;
  fullyBound: number;
  exact: number;
  nodeChain: number;
  skinParent: number;
  unmatched: number;
  ambiguous: number;
  complete: boolean;
  warning: string;
}

export function parseSkinBinding(text: string): SkinBindingShape[] {
  if (/^Error:/i.test(text)) throw new Error(firstErrorLine(text));
  const parsed = JSON.parse(text) as {
    error?: string;
    shapes?: Record<string, unknown>[];
    refused?: { block: number; name: string; reason: string }[];
  };
  if (parsed.error) throw new Error(parsed.error);
  const refused = (parsed.refused ?? []).map(item => ({
    block: item.block, name: item.name ?? '', vertices: 0, fullyBound: 0, exact: 0, nodeChain: 0,
    skinParent: 0, unmatched: 0, ambiguous: 0, complete: false, warning: `refused: ${item.reason}`,
  }));
  const bound = (parsed.shapes ?? []).map(shape => ({
    block: Number(shape.block ?? 0),
    name: String(shape.name ?? ''),
    vertices: Number(shape.vertices ?? 0),
    fullyBound: Number(shape.fully_bound_vertices ?? 0),
    exact: Number(shape.exact ?? 0),
    nodeChain: Number(shape.node_chain ?? 0),
    skinParent: Number(shape.skin_parent ?? 0),
    unmatched: Number(shape.unmatched ?? 0),
    ambiguous: Number(shape.ambiguous ?? 0),
    complete: shape.complete === true,
    warning: String(shape.warning ?? ''),
  }));
  return [...bound, ...refused];
}

// Invariant: a shape is called bound only when no bone is unmatched; matches by node chain or skin suffix are counted apart.
export function describeSkinBinding(shape: SkinBindingShape): string {
  if (shape.warning.startsWith('refused: ')) return `${shape.name || shape.block}: ${shape.warning}`;
  const verdict = shape.complete ? 'bound' : 'not fully bound';
  const approximate = shape.nodeChain + shape.skinParent;
  return `${shape.name}: ${verdict}, ${shape.exact} exact, ${approximate} approximate, ${shape.unmatched} unmatched, ` +
    `${shape.fullyBound}/${shape.vertices} vertices on exact bones` +
    (shape.warning ? ` -- ${shape.warning}` : '');
}

export async function readSkinBinding(host: NifHost, nifPath: string, block: number | null,
                                      skeletonPath: string, skeletonNifPath = ''): Promise<SkinBindingShape[]> {
  return parseSkinBinding(await host.SkinBinding(nifPath, block, skeletonPath, skeletonNifPath));
}

export async function readInfluences(host: NifHost, nifPath: string, block: number,
                                     vertex: number): Promise<VertexInfluences> {
  return parseVertexInfluences(await host.SkinInfluences(nifPath, block, vertex));
}

export async function readInfluencedBy(host: NifHost, nifPath: string, block: number, bone: number,
                                       minimum: number): Promise<BoneInfluence> {
  return parseBoneInfluence(await host.SkinInfluencedBy(nifPath, block, bone, minimum));
}

export async function readSkinReport(host: NifHost, nifPath: string, block: number): Promise<SkinReport> {
  return parseSkinReport(await host.SkinValidate(nifPath, block));
}

// Invariant: an empty output path saves in place, so the caller reloads the file it already has open.
export async function runSkinEdit(host: NifHost, nifPath: string, request: SkinEditRequest,
                                  expectedSource = ''): Promise<SkinEditReport> {
  const text = await host.SkinEdit(nifPath, '', request.kind, request.block, request.vertex ?? null,
    request.influences ?? null, request.fromBone ?? null, request.toBone ?? null,
    request.keepSource ?? false, request.lane ?? 0, request.fromPositive ?? true, false, expectedSource);
  return parseSkinEditReport(text);
}
