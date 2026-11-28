import type { HkxHost } from './backend';
import './hkxHost';
import './hkxRagdollHost';

export interface RagdollBodyRow {
  id: number;
  name: string;
  position?: number[];
}

export interface RagdollConstraintRow {
  id: number;
  kind: string;
  body_a: number;
  body_b: number;
  min_angle: number;
  max_angle: number;
  twist_min_angle: number | null;
  twist_max_angle: number | null;
  cone_max_angle: number | null;
  plane_min_angle?: number | null;
  plane_max_angle?: number | null;
  friction?: number | null;
  cone_angle_mode?: number | null;
  plane_angle_mode?: number | null;
}

export interface RagdollCapsuleRow {
  shape: number;
  a: number[];
  b: number[];
  radius: number;
  editable: boolean;
  reason: string;
}

export interface RagdollMaterialRow {
  material: number;
  dynamic_friction: number;
  static_friction: number;
  restitution: number;
  shared: boolean;
}

export interface RagdollView {
  bodies: RagdollBodyRow[];
  constraints: RagdollConstraintRow[];
  capsules?: RagdollCapsuleRow[];
  materials?: RagdollMaterialRow[];
  poseBoneCount: number | null;
}

export type MaterialField = 'dynamic_friction' | 'static_friction' | 'restitution';
export const MATERIAL_FIELDS: MaterialField[] = ['dynamic_friction', 'static_friction', 'restitution'];
export type MaterialValues = Partial<Record<MaterialField, number>> & { allow_shared?: boolean };
export interface CapsuleValues {
  a?: number[];
  b?: number[];
  radius?: number;
}

// Invariant: shape edits are staged apart from the constraint limits, and only staged rows reach the save payload.
export interface StagedShapeEdits {
  capsules: Record<number, CapsuleValues>;
  materials: Record<number, MaterialValues>;
}
export const NO_SHAPE_EDITS: StagedShapeEdits = { capsules: {}, materials: {} };

export const capsuleLength = (row: RagdollCapsuleRow): number =>
  Math.hypot(row.b[0] - row.a[0], row.b[1] - row.a[1], row.b[2] - row.a[2]);

// Constraint: a new length keeps the centre and the stored axis, because the native edit refuses a turned axis.
export function resizedEndpoints(row: RagdollCapsuleRow, length: number): { a: number[]; b: number[] } {
  const current = capsuleLength(row);
  const scale = current > 0 ? length / current : 1;
  const centre = row.a.map((v, i) => (v + row.b[i]) / 2);
  const half = row.a.map((v, i) => ((row.b[i] - v) / 2) * scale);
  return { a: centre.map((v, i) => v - half[i]), b: centre.map((v, i) => v + half[i]) };
}

// Constraint: the keys are the native edit keys, so one record serves both the save rows and the preview overrides.
export type LimitField = 'twist_min' | 'twist_max' | 'cone_max' | 'hinge_min' | 'hinge_max' | 'plane_min' | 'plane_max' | 'friction';
export type LimitValues = Partial<Record<LimitField, number>>;

// Constraint: plane and friction are listed only when the file stores them, because the native edit refuses a field the constraint lacks.
export function limitFieldsOf(row: RagdollConstraintRow): LimitField[] {
  const stored = (value: number | null | undefined) => typeof value === 'number';
  if (row.kind === 'ragdoll') {
    return ['twist_min', 'twist_max', 'cone_max',
            ...(stored(row.plane_min_angle) && stored(row.plane_max_angle) ? (['plane_min', 'plane_max'] as const) : []),
            ...(stored(row.friction) ? (['friction'] as const) : [])];
  }
  if (row.kind === 'limited_hinge') return ['hinge_min', 'hinge_max', ...(stored(row.friction) ? (['friction'] as const) : [])];
  return [];
}

export function storedLimits(row: RagdollConstraintRow): LimitValues {
  const friction = row.friction ?? undefined;
  if (row.kind === 'ragdoll') {
    return { twist_min: row.twist_min_angle ?? undefined, twist_max: row.twist_max_angle ?? undefined,
             cone_max: row.cone_max_angle ?? undefined, plane_min: row.plane_min_angle ?? undefined,
             plane_max: row.plane_max_angle ?? undefined, friction };
  }
  if (row.kind === 'limited_hinge') return { hinge_min: row.min_angle, hinge_max: row.max_angle, friction };
  return {};
}

// Constraint: staging sends only fields the user changed or already staged, so an untouched stored value that a non-vanilla file holds out of range never reaches the native range check.
export function changedLimits(row: RagdollConstraintRow, values: LimitValues, staged: LimitValues = {}): LimitValues {
  const stored = storedLimits(row);
  return Object.fromEntries(Object.entries(values).filter(([field, value]) =>
    field in staged || value !== stored[field as LimitField]));
}

// Constraint: the stored mode is the one the native edit gate reads, so a field the gate would refuse is read only here and says why.
// UNVERIFIED: modes 0 (cone) and 1 (plane) are the vanilla values per the BGS tests; Radicle's kConeAtomMode and kPlaneAtomMode must match.
const EDITABLE_CONE_MODE = 0;
const EDITABLE_PLANE_MODE = 1;
export function blockedReason(row: RagdollConstraintRow, field: LimitField): string | null {
  const refuse = (what: string, stored: number | null | undefined, editable: number) =>
    typeof stored === 'number' && stored !== editable
      ? `the ${what} is stored in angle measurement mode ${stored}, and only mode ${editable} can be edited` : null;
  if (field === 'cone_max') return refuse('cone limit', row.cone_angle_mode, EDITABLE_CONE_MODE);
  if (field === 'plane_min' || field === 'plane_max') return refuse('plane limit', row.plane_angle_mode, EDITABLE_PLANE_MODE);
  return null;
}

// Constraint: the preview consumes twist, cone and hinge limits only, so plane and friction never become preview overrides.
const NOT_PREVIEWED: LimitField[] = ['plane_min', 'plane_max', 'friction'];
export const previewLimitsOf = (values: LimitValues): LimitValues =>
  Object.fromEntries(Object.entries(values).filter(([field]) => !NOT_PREVIEWED.includes(field as LimitField)));

export interface PreviewJoint {
  constraint: number;
  kind: string;
  anchor_error: number;
  twist_angle: number | null;
  cone_angle: number | null;
  hinge_angle: number | null;
  violation: number;
  overridden: boolean;
}

export interface PreviewReply {
  steps: number;
  frames: { step: number; bodies: { body: number; position: number[]; rotation: number[] }[] }[];
  bodies: { body: number; mass: number; position: number[]; grounded: boolean }[];
  constraints: PreviewJoint[];
}

const parseReply = <T>(raw: unknown): T => {
  const reply = (typeof raw === 'string' ? JSON.parse(raw) : raw) as T & { error?: string };
  if (reply.error) throw new Error(reply.error);
  return reply;
};

export async function loadRagdoll(host: HkxHost, path: string): Promise<RagdollView> {
  if (!host.ReadRagdoll) throw new Error('this host build cannot read a ragdoll');
  const reply = parseReply<Omit<RagdollView, 'poseBoneCount'> & {
    loaded?: boolean; headline?: string;
    skeleton?: { bone_count: number } | null; animation_skeleton?: { bone_count: number } | null;
  }>(await host.ReadRagdoll(path, 2000));
  if (!reply.loaded) throw new Error(reply.headline || 'the file holds no usable ragdoll');
  // Constraint: a pose must match the skeleton the ragdoll mappings index, which is the animation skeleton when the file names one.
  const poseBoneCount = reply.animation_skeleton?.bone_count ?? reply.skeleton?.bone_count ?? null;
  return { bodies: reply.bodies, constraints: reply.constraints, capsules: reply.capsules, materials: reply.materials,
           poseBoneCount };
}

const limitRows = (limits: Record<number, LimitValues>) =>
  Object.entries(limits)
    .filter(([, values]) => Object.keys(values).length > 0)
    .map(([constraint, values]) => ({ constraint: Number(constraint), ...values }));

// Invariant: the save edits are built from the staged list alone, so a preview override can never reach the file.
export const buildSaveEdits = (staged: Record<number, LimitValues>, shapes: StagedShapeEdits = NO_SHAPE_EDITS) => ({
  constraints: limitRows(staged),
  ...(Object.keys(shapes.capsules).length > 0
    ? { shapes: Object.entries(shapes.capsules).map(([shape, values]) => ({ shape: Number(shape), ...values })) } : {}),
  ...(Object.keys(shapes.materials).length > 0
    ? { materials: Object.entries(shapes.materials).map(([material, values]) => ({ material: Number(material), ...values })) } : {}),
});

// Invariant: the preview options carry the preview overrides and nothing from the staged save list.
export interface PoseBone {
  translation: number[];
  rotation: number[];
}

// Constraint: a pose is sent only when given; without one the preview starts from the authored pose.
export const buildPreviewOptions = (overrides: Record<number, LimitValues>, steps: number, frames: number,
                                    pose?: PoseBone[]) => ({
  steps, frames,
  limit_overrides: limitRows(Object.fromEntries(Object.entries(overrides).map(([id, values]) => [id, previewLimitsOf(values)]))),
  ...(pose && pose.length > 0 ? { pose: pose.map(b => ({ position: b.translation, rotation: b.rotation })) } : {}),
});

export async function runPreview(host: HkxHost, path: string, overrides: Record<number, LimitValues>,
                                 steps = 120, frames = 24, pose?: PoseBone[]): Promise<PreviewReply> {
  if (!host.PreviewRagdoll) throw new Error('this host build cannot preview a ragdoll');
  return parseReply<PreviewReply>(await host.PreviewRagdoll(path, JSON.stringify(buildPreviewOptions(overrides, steps, frames, pose))));
}

export async function saveRagdoll(host: HkxHost, path: string, outPath: string, staged: Record<number, LimitValues>,
                                  overwrite: boolean, shapes: StagedShapeEdits = NO_SHAPE_EDITS):
    Promise<{ saved: boolean; path: string; sha256: string }> {
  if (!host.SaveRagdoll) throw new Error('this host build cannot save a ragdoll');
  return parseReply(await host.SaveRagdoll(path, outPath, JSON.stringify(buildSaveEdits(staged, shapes)), overwrite));
}

// Constraint: the line names the joint and its excess, so a limit the run broke is never shown as a clean preview.
export const describeJoint = (joint: PreviewJoint): string => {
  const angle = joint.twist_angle ?? joint.hinge_angle;
  const parts = [`constraint ${joint.constraint} (${joint.kind})`];
  if (angle !== null) parts.push(`angle ${angle.toFixed(3)} rad`);
  if (joint.cone_angle !== null) parts.push(`cone ${joint.cone_angle.toFixed(3)} rad`);
  parts.push(joint.violation > 1e-3 ? `over its limit by ${joint.violation.toFixed(3)} rad` : 'inside its limits');
  return parts.join(', ');
};
