import type { HkxBone } from '../backend';

export interface SkinBone {
  name: string;
  rotation: number[];
  translation: number[];
  scale: number;
}

export interface ShapeSkin {
  weights: number[];
  boneIndices: number[];
  bones: SkinBone[];
}

// Invariant: an Affine is a row-major 3x4 matrix that maps a point as M * (x, y, z, 1).
type Affine = number[];

const IDENTITY: Affine = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0];

function multiply(a: Affine, b: Affine): Affine {
  const out = new Array<number>(12);
  for (let r = 0; r < 3; r++) {
    for (let c = 0; c < 3; c++) {
      out[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c];
    }
    out[r * 4 + 3] = a[r * 4] * b[3] + a[r * 4 + 1] * b[7] + a[r * 4 + 2] * b[11] + a[r * 4 + 3];
  }
  return out;
}

// Requires: quaternion order x, y, z, w, UNVERIFIED against real Havok data.
function hkxModelMatrix(bone: HkxBone): Affine {
  const [x, y, z, w] = bone.rotation;
  const len = Math.hypot(x, y, z, w) || 1;
  const qx = x / len, qy = y / len, qz = z / len, qw = w / len;
  const [sx, sy, sz] = bone.scale;
  const [tx, ty, tz] = bone.translation;
  return [
    (1 - 2 * (qy * qy + qz * qz)) * sx, 2 * (qx * qy - qz * qw) * sy, 2 * (qx * qz + qy * qw) * sz, tx,
    2 * (qx * qy + qz * qw) * sx, (1 - 2 * (qx * qx + qz * qz)) * sy, 2 * (qy * qz - qx * qw) * sz, ty,
    2 * (qx * qz - qy * qw) * sx, 2 * (qy * qz + qx * qw) * sy, (1 - 2 * (qx * qx + qy * qy)) * sz, tz,
  ];
}

// Requires: BoneData applied as s * (R * p) + t with R row-major, direction UNVERIFIED against real data.
function boneDataMatrix(bone: SkinBone): Affine {
  const r = bone.rotation, t = bone.translation, s = bone.scale;
  return [
    s * r[0], s * r[1], s * r[2], t[0],
    s * r[3], s * r[4], s * r[5], t[1],
    s * r[6], s * r[7], s * r[8], t[2],
  ];
}

export interface PosedShape {
  positions: number[];
  normals: number[];
  unmatched: number;
}

// Constraint: bones are matched by lower-case name; an unmatched skin bone uses the identity, so a vertex bound only to unmatched bones keeps its NIF position.
export function poseShape(verts: number[], normals: number[], skin: ShapeSkin, pose: HkxBone[]): PosedShape {
  const byName = new Map<string, HkxBone>();
  for (const bone of pose) {
    const key = bone.name.toLowerCase();
    if (!byName.has(key)) byName.set(key, bone);
  }
  let unmatched = 0;
  const matrices = skin.bones.map(bone => {
    const target = byName.get(bone.name.toLowerCase());
    if (!target) {
      unmatched++;
      return IDENTITY;
    }
    return multiply(hkxModelMatrix(target), boneDataMatrix(bone));
  });

  const count = verts.length / 3;
  const positions = new Array<number>(verts.length);
  const posedNormals = new Array<number>(normals.length);
  const withNormals = normals.length === verts.length;
  for (let v = 0; v < count; v++) {
    const px = verts[3 * v], py = verts[3 * v + 1], pz = verts[3 * v + 2];
    const nx = withNormals ? normals[3 * v] : 0, ny = withNormals ? normals[3 * v + 1] : 0, nz = withNormals ? normals[3 * v + 2] : 0;
    let total = 0;
    let ox = 0, oy = 0, oz = 0, mx = 0, my = 0, mz = 0;
    for (let k = 0; k < 4; k++) {
      const weight = skin.weights[4 * v + k] ?? 0;
      const m = matrices[skin.boneIndices[4 * v + k]];
      if (!(weight > 0) || !m) continue;
      total += weight;
      ox += weight * (m[0] * px + m[1] * py + m[2] * pz + m[3]);
      oy += weight * (m[4] * px + m[5] * py + m[6] * pz + m[7]);
      oz += weight * (m[8] * px + m[9] * py + m[10] * pz + m[11]);
      mx += weight * (m[0] * nx + m[1] * ny + m[2] * nz);
      my += weight * (m[4] * nx + m[5] * ny + m[6] * nz);
      mz += weight * (m[8] * nx + m[9] * ny + m[10] * nz);
    }
    if (total === 0) {
      positions[3 * v] = px; positions[3 * v + 1] = py; positions[3 * v + 2] = pz;
      if (withNormals) { posedNormals[3 * v] = nx; posedNormals[3 * v + 1] = ny; posedNormals[3 * v + 2] = nz; }
      continue;
    }
    positions[3 * v] = ox / total; positions[3 * v + 1] = oy / total; positions[3 * v + 2] = oz / total;
    if (withNormals) {
      const len = Math.hypot(mx, my, mz) || 1;
      posedNormals[3 * v] = mx / len; posedNormals[3 * v + 1] = my / len; posedNormals[3 * v + 2] = mz / len;
    }
  }
  return { positions, normals: withNormals ? posedNormals : [], unmatched };
}
