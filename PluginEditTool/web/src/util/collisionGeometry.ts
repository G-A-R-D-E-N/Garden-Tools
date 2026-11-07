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

export interface CollisionBodyGeo {
  node: string;
  motion: 'static' | 'dynamic';
  transform: NifTransform;
  shapes: CollisionShapeGeo[];
}

export interface CollisionGeo {
  bodies: CollisionBodyGeo[];
  skipped: { node: string; kind: string }[];
}

export function placeCollisionPoints(havokMetres: number[], node: NifTransform): number[] {
  const out = new Array<number>(havokMetres.length);
  const { t, r, s } = node;
  for (let i = 0; i < havokMetres.length; i += 3) {
    const x = havokMetres[i] * HAVOK_METRES_TO_NIF;
    const y = havokMetres[i + 1] * HAVOK_METRES_TO_NIF;
    const z = havokMetres[i + 2] * HAVOK_METRES_TO_NIF;
    for (let row = 0; row < 3; row++) {
      out[i + row] = s * (r[row * 3] * x + r[row * 3 + 1] * y + r[row * 3 + 2] * z) + t[row];
    }
  }
  return out;
}
