import type { NifHost } from './backend';
import './skinHost';

export type BrushMode = 'add' | 'subtract' | 'replace' | 'smooth';
export type BrushFalloff = 'smoothstep' | 'linear' | 'constant';
export type TransferMethod = 'nearest_vertex' | 'barycentric';

export const BRUSH_MODES: { id: BrushMode; label: string }[] = [
  { id: 'add', label: 'Add' },
  { id: 'subtract', label: 'Remove' },
  { id: 'replace', label: 'Replace' },
  { id: 'smooth', label: 'Smooth' },
];

export interface BrushSettings {
  mode: BrushMode;
  falloff: BrushFalloff;
  center: [number, number, number];
  radius: number;
  strength: number;
  bone: number;
  target: number;
  locked: number[];
}

export interface RemapRecord {
  bones: number[];
  weights: number[];
}

export interface RemapNewBone {
  name: string;
  raw: string;
}

export interface RemapData {
  remapBlock: number;
  newBonesBlock: number;
  records: RemapRecord[];
  newBones: RemapNewBone[];
}

export interface OspData {
  name: string;
  target: string;
  file: string;
  nameInFile: string;
  legacyBsd: boolean;
}

export interface OspSlider {
  name: string;
  invert: boolean;
  hidden: boolean;
  zap: boolean;
  uv: boolean;
  data: OspData[];
}

export interface OspSet {
  name: string;
  shapes: { name: string; target: string }[];
  sliders: OspSlider[];
}

export interface OsdSet {
  name: string;
  offsets: number;
}

export interface OsdApplyChoice {
  osdPath: string;
  ospPath: string;
  setName: string;
  slider: string;
  dataSet: string;
  weight: number;
}

export interface SkinToolReport {
  op: string;
  block: number;
  saved: string;
  inReach: number;
  changed: number;
  skipped: number;
  maxWeightChange: number;
  transferred: number;
  outOfReach: number;
  unmapped: number;
  records: number;
  newBones: number;
  messages: string[];
  remap: RemapData | null;
  moved: number;
  dataSet: string;
  ospSets: OspSet[];
  osdSets: OsdSet[];
}

// Invariant: a bone index is one byte in the skin record, so the panel refuses 256 and above before the host does.
const MAX_BONE = 255;

const finite = (value: number): boolean => Number.isFinite(value);

// Invariant: Remove is the subtract mode, Smooth ignores the bone and the target, and only Replace sends a target.
export function brushArgs(block: number, settings: BrushSettings): Record<string, unknown> {
  if (block < 0) throw new Error('select a shape first');
  if (!settings.center.every(finite)) throw new Error('the brush centre needs three numbers');
  if (!finite(settings.radius) || settings.radius <= 0) throw new Error('the brush radius must be above 0');
  if (!finite(settings.strength) || settings.strength < 0) throw new Error('the brush strength must not be negative');
  const locked = Array.from(new Set(settings.locked)).sort((a, b) => a - b);
  if (locked.some(bone => !Number.isInteger(bone) || bone < 0 || bone > MAX_BONE)) {
    throw new Error('a locked bone must be a bone index from 0 to 255');
  }
  const args: Record<string, unknown> = {
    block, mode: settings.mode, falloff: settings.falloff, center: settings.center,
    radius: settings.radius, strength: settings.strength, locked,
  };
  if (settings.mode !== 'smooth') {
    if (!Number.isInteger(settings.bone) || settings.bone < 0 || settings.bone > MAX_BONE) {
      throw new Error('pick the bone to paint');
    }
    args.bone = settings.bone;
  }
  if (settings.mode === 'replace') {
    if (!finite(settings.target) || settings.target < 0 || settings.target > 1) {
      throw new Error('the replace target must be from 0 to 1');
    }
    args.target = settings.target;
  }
  return args;
}

export function transferArgs(block: number, sourcePath: string, sourceBlock: number, method: TransferMethod,
                             maxDistance: number): Record<string, unknown> {
  if (block < 0) throw new Error('select the target shape first');
  if (!sourcePath) throw new Error('choose the source NIF first');
  if (sourceBlock < 0) throw new Error('select the source shape first');
  if (!finite(maxDistance) || maxDistance < 0) throw new Error('the maximum distance must be 0 or more');
  return { block, source_path: sourcePath, source_block: sourceBlock, method, max_distance: maxDistance };
}

type Raw = Record<string, unknown> & { error?: string };

const num = (value: unknown): number => (typeof value === 'number' ? value : 0);

function parseRemap(raw: Raw): RemapData | null {
  if (!Array.isArray(raw.records)) return null;
  return {
    remapBlock: num(raw.remap_block ?? -1),
    newBonesBlock: num(raw.new_bones_block ?? -1),
    records: (raw.records as RemapRecord[]).map(record => ({ bones: record.bones, weights: record.weights })),
    newBones: ((raw.new_bones ?? []) as RemapNewBone[]).map(bone => ({ name: bone.name ?? '', raw: bone.raw })),
  };
}

const str = (value: unknown): string => (typeof value === 'string' ? value : '');

function parseOspSets(raw: Raw): OspSet[] {
  if (raw.op !== 'osp_read' || !Array.isArray(raw.sets)) return [];
  return (raw.sets as Record<string, unknown>[]).map(set => ({
    name: str(set.name),
    shapes: ((set.shapes ?? []) as Record<string, unknown>[]).map(shape => ({ name: str(shape.name), target: str(shape.target) })),
    sliders: ((set.sliders ?? []) as Record<string, unknown>[]).map(slider => ({
      name: str(slider.name),
      invert: slider.invert === true,
      hidden: slider.hidden === true,
      zap: slider.zap === true,
      uv: slider.uv === true,
      data: ((slider.data ?? []) as Record<string, unknown>[]).map(entry => ({
        name: str(entry.name), target: str(entry.target), file: str(entry.file), nameInFile: str(entry.name_in_file),
        legacyBsd: entry.legacy_bsd === true,
      })),
    })),
  }));
}

function parseOsdSets(raw: Raw): OsdSet[] {
  if (raw.op !== 'osd_read' || !Array.isArray(raw.sets)) return [];
  return (raw.sets as Record<string, unknown>[]).map(set => ({ name: str(set.name), offsets: num(set.offsets) }));
}

export function parseSkinToolReport(text: string): SkinToolReport {
  let raw: Raw;
  try {
    raw = JSON.parse(text) as Raw;
  } catch {
    throw new Error(text);
  }
  if (typeof raw.error === 'string') throw new Error(raw.error);
  return {
    op: typeof raw.op === 'string' ? raw.op : '',
    block: num(raw.block),
    saved: typeof raw.saved === 'string' ? raw.saved : '',
    inReach: num(raw.in_reach),
    changed: num(raw.changed),
    skipped: num(raw.skipped),
    maxWeightChange: num(raw.max_weight_change),
    transferred: num(raw.transferred),
    outOfReach: num(raw.out_of_reach),
    unmapped: num(raw.unmapped),
    records: Array.isArray(raw.records) ? raw.records.length : num(raw.records),
    newBones: Array.isArray(raw.new_bones) ? raw.new_bones.length : num(raw.new_bones),
    messages: Array.isArray(raw.messages) ? (raw.messages as string[]) : [],
    remap: parseRemap(raw),
    moved: num(raw.moved),
    dataSet: str(raw.data_set),
    ospSets: parseOspSets(raw),
    osdSets: parseOsdSets(raw),
  };
}

export const plural = (count: number, one: string, many: string): string => `${count} ${count === 1 ? one : many}`;

// Invariant: an empty saved path means the host wrote nothing, so the summary says so instead of claiming a save.
export function describeBrushReport(report: SkinToolReport): string {
  if (report.saved === '') {
    return `brush changed no vertex (${plural(report.inReach, 'vertex', 'vertices')} in reach, ${report.skipped} skipped); nothing saved`;
  }
  return `brush changed ${plural(report.changed, 'vertex', 'vertices')} of ${report.inReach} in reach, ` +
    `${report.skipped} skipped, largest weight change ${report.maxWeightChange.toFixed(3)}`;
}

export function describeTransferReport(report: SkinToolReport): string {
  const counts = `${report.transferred} transferred, ${report.outOfReach} out of reach, ${report.unmapped} unmapped`;
  return report.saved === '' ? `transfer wrote nothing (${counts}); nothing saved` : `transfer: ${counts}`;
}

export function describeRemapRead(remap: RemapData | null): string {
  if (remap === null) return 'the host sent no remap data';
  const bones = remap.newBones.length === 0
    ? 'no new bones'
    : plural(remap.newBones.length, 'new bone', 'new bones') + ` (${remap.newBones.map(b => b.name || '(unnamed)').join(', ')})`;
  return remap.remapBlock < 0
    ? 'this shape has no customization remap data'
    : `customization remap: ${plural(remap.records.length, 'record', 'records')}, ${bones}`;
}

export function describeRemapValidation(messages: string[]): string {
  return messages.length === 0 ? 'remap data is sound' : `${plural(messages.length, 'problem', 'problems')} in the remap data`;
}

// Invariant: a slider is applied from the .osp only through its own name, and a data set only from the .osd; the host decides which diff fits the shape.
export function osdApplyArgs(block: number, choice: OsdApplyChoice): Record<string, unknown> {
  if (block < 0) throw new Error('select a shape first');
  if (!choice.osdPath) throw new Error('choose the .osd first');
  if (!finite(choice.weight) || choice.weight < 0 || choice.weight > 1) {
    throw new Error('the slider weight must be from 0 to 1');
  }
  const args: Record<string, unknown> = { block, osd_path: choice.osdPath, weight: choice.weight };
  if (choice.slider !== '') {
    if (!choice.ospPath) throw new Error('choose the .osp first');
    args.osp_path = choice.ospPath;
    args.slider = choice.slider;
    if (choice.setName !== '') args.set = choice.setName;
  } else {
    if (!choice.dataSet) throw new Error('pick the data set to apply');
    args.data_set = choice.dataSet;
  }
  return args;
}

export function describeOsdApply(report: SkinToolReport): string {
  const name = report.dataSet === '' ? 'slider' : `slider data ${report.dataSet}`;
  return report.saved === ''
    ? `${name} moved no vertex; nothing saved`
    : `${name} moved ${plural(report.moved, 'vertex', 'vertices')}`;
}

// Invariant: Create is offered only for a shape with no remap block; a shape that owns one gets no remap save.
export const remapAction = (remap: RemapData | null): 'create' | null =>
  remap !== null && remap.remapBlock < 0 ? 'create' : null;

async function call(host: NifHost, nifPath: string, op: string, args: Record<string, unknown>,
                    expectedSource = ''): Promise<SkinToolReport> {
  return parseSkinToolReport(await host.SkinToolOp(nifPath, '', op, JSON.stringify(args), false, expectedSource));
}

export const runBrush = (host: NifHost, nifPath: string, block: number, settings: BrushSettings, expectedSource = '') =>
  call(host, nifPath, 'brush', brushArgs(block, settings), expectedSource);

export const runTransfer = (host: NifHost, nifPath: string, args: Record<string, unknown>, expectedSource = '') =>
  call(host, nifPath, 'transfer', args, expectedSource);

export const readRemap = (host: NifHost, nifPath: string, block: number) => call(host, nifPath, 'remap_read', { block });

export const validateRemap = (host: NifHost, nifPath: string, block: number) =>
  call(host, nifPath, 'remap_validate', { block });

export const createRemap = (host: NifHost, nifPath: string, block: number, expectedSource = '') =>
  call(host, nifPath, 'remap_create', { block }, expectedSource);

export const readOsp = (host: NifHost, nifPath: string, path: string) => call(host, nifPath, 'osp_read', { path });

export const readOsd = (host: NifHost, nifPath: string, path: string) => call(host, nifPath, 'osd_read', { path });

export const runOsdApply = (host: NifHost, nifPath: string, block: number, choice: OsdApplyChoice, expectedSource = '') =>
  call(host, nifPath, 'osd_apply', osdApplyArgs(block, choice), expectedSource);
