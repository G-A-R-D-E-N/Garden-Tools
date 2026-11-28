export const HAVOK_METRES_TO_NIF = 69.99125;

export interface NifTransform {
  t: [number, number, number];
  r: number[];
  s: number;
}

export interface CollisionShapeGeo {
  vertices: number[];
  triangles: number[];
}

export interface BodyPlacement {
  t: [number, number, number];
  q: [number, number, number, number];
}

export interface CollisionBodyGeo {
  node: string;
  motion: 'static' | 'dynamic';
  transform: NifTransform;
  bodyPlacement?: BodyPlacement;
  shapes: CollisionShapeGeo[];
}

export interface CollisionGeo {
  bodies: CollisionBodyGeo[];
  skipped: { node: string; kind: string }[];
}

function rotateQuat(q: [number, number, number, number], x: number, y: number, z: number): [number, number, number] {
  const [qx, qy, qz, qw] = q;
  const tx = 2 * (qy * z - qz * y);
  const ty = 2 * (qz * x - qx * z);
  const tz = 2 * (qx * y - qy * x);
  return [x + qw * tx + (qy * tz - qz * ty), y + qw * ty + (qz * tx - qx * tz), z + qw * tz + (qx * ty - qy * tx)];
}

function applyBodyPlacement(havokMetres: number[], body: BodyPlacement): number[] {
  const out = new Array<number>(havokMetres.length);
  for (let i = 0; i < havokMetres.length; i += 3) {
    const placed = rotateQuat(body.q, havokMetres[i], havokMetres[i + 1], havokMetres[i + 2]);
    out[i] = placed[0] + body.t[0];
    out[i + 1] = placed[1] + body.t[1];
    out[i + 2] = placed[2] + body.t[2];
  }
  return out;
}

export function placeCollisionPoints(havokMetres: number[], node: NifTransform, body?: BodyPlacement): number[] {
  const local = body ? applyBodyPlacement(havokMetres, body) : havokMetres;
  const out = new Array<number>(local.length);
  const { t, r, s } = node;
  for (let i = 0; i < local.length; i += 3) {
    const x = local[i] * HAVOK_METRES_TO_NIF;
    const y = local[i + 1] * HAVOK_METRES_TO_NIF;
    const z = local[i + 2] * HAVOK_METRES_TO_NIF;
    for (let row = 0; row < 3; row++) {
      out[i + row] = s * (r[row * 3] * x + r[row * 3 + 1] * y + r[row * 3 + 2] * z) + t[row];
    }
  }
  return out;
}
