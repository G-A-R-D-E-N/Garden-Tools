import type { HkxBone } from '../backend';

export type Vec3 = [number, number, number];

export type MeshMode = 'Shaded' | 'Wireframe' | 'Off';
export const MESH_MODES: readonly MeshMode[] = ['Shaded', 'Wireframe', 'Off'];

export const parseMeshMode = (saved: string | null): MeshMode => MESH_MODES.find(mode => mode === saved) ?? 'Shaded';

export const meshDrawState = (mode: MeshMode, wireframe: boolean): { solid: boolean; wire: boolean } => ({
  solid: mode === 'Shaded',
  wire: mode === 'Wireframe' || (mode === 'Shaded' && wireframe),
});

function rotate(q: HkxBone['rotation'], v: Vec3): Vec3 {
  const [x, y, z, w] = q;
  const tx = 2 * (y * v[2] - z * v[1]);
  const ty = 2 * (z * v[0] - x * v[2]);
  const tz = 2 * (x * v[1] - y * v[0]);
  return [v[0] + w * tx + (y * tz - z * ty), v[1] + w * ty + (z * tx - x * tz), v[2] + w * tz + (x * ty - y * tx)];
}

function multiplyRotation(p: HkxBone['rotation'], r: HkxBone['rotation']): HkxBone['rotation'] {
  const q: HkxBone['rotation'] = [
    p[3] * r[0] + p[0] * r[3] + p[1] * r[2] - p[2] * r[1],
    p[3] * r[1] - p[0] * r[2] + p[1] * r[3] + p[2] * r[0],
    p[3] * r[2] + p[0] * r[1] - p[1] * r[0] + p[2] * r[3],
    p[3] * r[3] - p[0] * r[0] - p[1] * r[1] - p[2] * r[2],
  ];
  const length = Math.hypot(...q) || 1;
  return [q[0] / length, q[1] / length, q[2] / length, q[3] / length];
}

// Invariant: mirrors Radicle model_space_pose, so a parent index that is not lower than its bone is refused with null.
export function referenceModelPose(local: HkxBone[]): HkxBone[] | null {
  const model: HkxBone[] = [];
  for (let i = 0; i < local.length; i++) {
    const bone = local[i];
    if (bone.parent === -1) {
      model.push(bone);
      continue;
    }
    if (bone.parent < 0 || bone.parent >= i) return null;
    const parent = model[bone.parent];
    const scaled: Vec3 = [bone.translation[0] * parent.scale[0], bone.translation[1] * parent.scale[1], bone.translation[2] * parent.scale[2]];
    const moved = rotate(parent.rotation, scaled);
    model.push({
      name: bone.name,
      parent: bone.parent,
      translation: [parent.translation[0] + moved[0], parent.translation[1] + moved[1], parent.translation[2] + moved[2]],
      rotation: multiplyRotation(parent.rotation, bone.rotation),
      scale: [parent.scale[0] * bone.scale[0], parent.scale[1] * bone.scale[1], parent.scale[2] * bone.scale[2]],
    });
  }
  return model;
}

export const rootPosition = (model: HkxBone[]): Vec3 | null => {
  const root = model.find(bone => bone.parent < 0);
  return root ? [root.translation[0], root.translation[1], root.translation[2]] : null;
};

// Invariant: the camera shift is the change in (travel - framedAt) while following and zero when not, so toggling or re-framing never adds travel twice.
export function followStep(travel: Vec3, framedAt: Vec3, applied: Vec3, follow: boolean): { step: Vec3; applied: Vec3 } {
  const want: Vec3 = follow
    ? [travel[0] - framedAt[0], travel[1] - framedAt[1], travel[2] - framedAt[2]]
    : [0, 0, 0];
  return { step: [want[0] - applied[0], want[1] - applied[1], want[2] - applied[2]], applied: want };
}
