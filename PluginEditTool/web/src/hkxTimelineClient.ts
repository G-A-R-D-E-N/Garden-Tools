import type { HkxHost } from './backend';
import './hkxTimelineHost';

export interface HkxClipEditReply {
  source: string;
  ops: number;
  bytes: number;
  saved: boolean;
  out?: string;
  frame_count: number;
  duration: number;
  track_count: number;
  motion: string;
  annotation_tracks: { name: string; annotations: number }[];
  report: {
    annotations_dropped: number;
    resampled: boolean;
    position_error: number;
    rotation_error: number;
    scale_error: number;
  };
}

// Requires a host that carries the clip edit RPC; an older host build says so instead of showing an unchanged clip.
export async function editClip(host: HkxHost, path: string, ops: unknown[], outPath: string,
                               expectedSource: string, overwrite: boolean,
                               spline: boolean | null): Promise<HkxClipEditReply> {
  if (!host.ClipEdit) throw new Error('this host build cannot edit an animation clip');
  const raw = await host.ClipEdit(path, JSON.stringify(ops), outPath, expectedSource, overwrite, spline);
  const reply = (typeof raw === 'string' ? JSON.parse(raw) : raw) as HkxClipEditReply & { error?: string };
  if (reply.error) throw new Error(reply.error);
  if (typeof reply.frame_count !== 'number') throw new Error('clip edit reply has no frame count');
  return reply;
}

// Constraint: an op list must be a non-empty JSON array of objects, so a typo is refused before the host is called.
export function parseOps(text: string): unknown[] {
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch {
    throw new Error('edit ops are not valid JSON');
  }
  if (!Array.isArray(value) || value.length === 0 || value.some(op => typeof op !== 'object' || op === null || Array.isArray(op))) {
    throw new Error('edit ops must be a non-empty list of objects, each with an op name');
  }
  return value;
}

type FieldKind = 'index' | 'number' | 'flag' | 'text' | 'vec3' | 'vec4';
export interface OpField { name: string; label: string; kind: FieldKind; optional?: boolean }

const frame = (name: string, label: string): OpField => ({ name, label, kind: 'index' });
const limit = (name: string, label: string): OpField => ({ name, label, kind: 'number', optional: true });
const note: OpField[] = [{ name: 'time', label: 'Time (s)', kind: 'number' }, { name: 'text', label: 'Annotation text', kind: 'text' }];
const noteAt: OpField[] = [frame('track', 'Annotation track'), frame('index', 'Annotation index')];

// Invariant: names and fields equal the allow lists of HkxService::clipEdit, so a built op is never refused as unknown.
export const OP_FIELDS: Record<string, OpField[]> = {
  trim: [frame('first', 'First frame'), frame('last', 'Last frame')],
  retime: [{ name: 'scale', label: 'Time scale', kind: 'number' },
    { name: 'keep_frame_rate', label: 'Keep the frame rate (resample)', kind: 'flag', optional: true },
    limit('max_position', 'Max position error'), limit('max_rotation', 'Max rotation error'), limit('max_scale', 'Max scale error')],
  loop: [frame('blend_frames', 'Blend frames')],
  set_key: [frame('track', 'Track'), frame('frame', 'Frame'),
    { name: 'translation', label: 'Translation x, y, z', kind: 'vec3', optional: true },
    { name: 'rotation', label: 'Rotation x, y, z, w', kind: 'vec4', optional: true },
    { name: 'scale', label: 'Scale x, y, z', kind: 'vec3', optional: true }],
  copy_pose: [frame('from', 'From frame'), frame('to', 'To frame')],
  insert_frames: [frame('at', 'At frame'), frame('count', 'Frame count')],
  delete_frames: [frame('first', 'First frame'), frame('count', 'Frame count')],
  move_frames: [frame('first', 'First frame'), frame('count', 'Frame count'), frame('to', 'To frame')],
  add_annotation: [{ name: 'track_name', label: 'Annotation track name', kind: 'text' }, ...note],
  edit_annotation: [...noteAt, ...note],
  remove_annotation: noteAt,
};

const parseField = (field: OpField, raw: string): unknown => {
  const text = raw.trim();
  if (field.kind === 'flag') return text === 'true' ? true : undefined;
  if (text === '') {
    if (field.optional) return undefined;
    throw new Error(`${field.label} is required`);
  }
  if (field.kind === 'text') return raw;
  if (field.kind === 'index') {
    if (!/^\d+$/.test(text) || !Number.isSafeInteger(Number(text))) throw new Error(`${field.label} must be a whole number of zero or more`);
    return Number(text);
  }
  const parts = field.kind === 'number' ? [text] : text.split(/[\s,]+/).filter(Boolean);
  const size = field.kind === 'vec4' ? 4 : field.kind === 'vec3' ? 3 : 1;
  const values = parts.map(Number);
  if (values.length !== size || values.some(value => !Number.isFinite(value))) {
    throw new Error(size === 1 ? `${field.label} must be a number` : `${field.label} must be ${size} numbers separated by commas`);
  }
  return size === 1 ? values[0] : values;
};

// Constraint: a blank optional field is left out, so the host default applies and the op text stays what the host would parse.
export function buildOp(op: string, values: Record<string, string>): Record<string, unknown> {
  const fields = OP_FIELDS[op];
  if (!fields) throw new Error(`unknown op '${op}'`);
  const built: Record<string, unknown> = { op };
  for (const field of fields) {
    const parsed = parseField(field, values[field.name] ?? '');
    if (parsed !== undefined) built[field.name] = parsed;
  }
  if (op === 'set_key' && built.translation === undefined && built.rotation === undefined && built.scale === undefined) {
    throw new Error('set_key needs a translation, a rotation or a scale');
  }
  return built;
}

// Constraint: the line states frames, duration and dropped annotations, so a trim that lost events is never silent.
export const describeClipEdit = (reply: HkxClipEditReply): string =>
  `${reply.saved ? 'Saved' : 'Preview'}: ${reply.frame_count} frames, ${reply.duration.toFixed(3)} s, ` +
  `${reply.annotation_tracks.reduce((sum, track) => sum + track.annotations, 0)} annotations` +
  (reply.report.annotations_dropped > 0 ? `, ${reply.report.annotations_dropped} dropped` : '');
