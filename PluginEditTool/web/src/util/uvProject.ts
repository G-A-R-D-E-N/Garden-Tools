import { packIslands, triCount, triOk } from './uvPack';

const CANCEL_RATIO = 1e-3;

const unit = (v: readonly number[]): [number, number, number] | null => {
  const len = Math.hypot(v[0], v[1], v[2]);
  return len > 1e-12 && Number.isFinite(len) ? [v[0] / len, v[1] / len, v[2] / len] : null;
};

const faceNormal = (verts: readonly number[], a: number, b: number, c: number): [number, number, number] | null => {
  const ux = verts[3 * b] - verts[3 * a], uy = verts[3 * b + 1] - verts[3 * a + 1], uz = verts[3 * b + 2] - verts[3 * a + 2];
  const vx = verts[3 * c] - verts[3 * a], vy = verts[3 * c + 1] - verts[3 * a + 1], vz = verts[3 * c + 2] - verts[3 * a + 2];
  const n: [number, number, number] = [uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx];
  return Number.isFinite(n[0] + n[1] + n[2]) ? n : null;
};

type Basis = { u: [number, number, number]; v: [number, number, number] };

const basisFor = (verts: readonly number[], tris: readonly number[], ids: readonly number[], island: number,
                  normal: readonly number[]): Basis | null => {
  const vc = Math.floor(verts.length / 3);
  let bestLen = 0;
  let bestDir: [number, number, number] | null = null;
  for (let t = 0; t < triCount(tris); t++) {
    if (!triOk(tris, t, vc) || ids[tris[3 * t]] !== island) continue;
    for (let k = 0; k < 3; k++) {
      const a = tris[3 * t + k], b = tris[3 * t + ((k + 1) % 3)];
      const dx = verts[3 * b] - verts[3 * a], dy = verts[3 * b + 1] - verts[3 * a + 1], dz = verts[3 * b + 2] - verts[3 * a + 2];
      const len = Math.hypot(dx, dy, dz);
      if (len <= bestLen || len <= 1e-12) continue;
      const d: [number, number, number] = [dx / len, dy / len, dz / len];
      if (Math.abs(d[0] * normal[0] + d[1] * normal[1] + d[2] * normal[2]) > 0.9) continue;
      bestLen = len;
      bestDir = d;
    }
  }
  let u: [number, number, number] | null = null;
  if (bestDir !== null) {
    const dn = bestDir[0] * normal[0] + bestDir[1] * normal[1] + bestDir[2] * normal[2];
    u = unit([bestDir[0] - normal[0] * dn, bestDir[1] - normal[1] * dn, bestDir[2] - normal[2] * dn]);
  }
  if (u === null) {
    const helper: [number, number, number] = Math.abs(normal[2]) < 0.9 ? [0, 0, 1] : [0, 1, 0];
    u = unit([
      helper[1] * normal[2] - helper[2] * normal[1],
      helper[2] * normal[0] - helper[0] * normal[2],
      helper[0] * normal[1] - helper[1] * normal[0],
    ]);
  }
  if (u === null) return null;
  const v: [number, number, number] = [
    normal[1] * u[2] - normal[2] * u[1],
    normal[2] * u[0] - normal[0] * u[2],
    normal[0] * u[1] - normal[1] * u[0],
  ];
  return Number.isFinite(v[0] + v[1] + v[2]) ? { u, v } : null;
};

export function islandPlanes(
  verts: readonly number[], tris: readonly number[], ids: readonly number[], sel: ReadonlySet<number>,
): Map<number, Basis & { cu: number; cv: number }> {
  const n = Math.min(Math.floor(verts.length / 3), ids.length);
  const keep = new Set<number>();
  for (const i of sel) if (Number.isInteger(i) && i >= 0 && i < n) keep.add(ids[i]);
  const sums = new Map<number, [number, number, number]>();
  const absSums = new Map<number, number>();
  const centroids = new Map<number, [number, number, number, number]>();
  for (let i = 0; i < n; i++) {
    if (!keep.has(ids[i])) continue;
    const c = centroids.get(ids[i]) ?? [0, 0, 0, 0];
    c[0] += verts[3 * i]; c[1] += verts[3 * i + 1]; c[2] += verts[3 * i + 2]; c[3] += 1;
    centroids.set(ids[i], c);
  }
  for (let t = 0; t < triCount(tris); t++) {
    if (!triOk(tris, t, n)) continue;
    const island = ids[tris[3 * t]];
    if (!keep.has(island)) continue;
    const fn = faceNormal(verts, tris[3 * t], tris[3 * t + 1], tris[3 * t + 2]);
    if (fn === null) continue;
    const sum = sums.get(island) ?? [0, 0, 0];
    sum[0] += fn[0]; sum[1] += fn[1]; sum[2] += fn[2];
    sums.set(island, sum);
    absSums.set(island, (absSums.get(island) ?? 0) + Math.hypot(fn[0], fn[1], fn[2]));
  }
  const planes = new Map<number, Basis & { cu: number; cv: number }>();
  for (const [island, sum] of sums) {
    const absSum = absSums.get(island) ?? 0;
    if (!(Math.hypot(sum[0], sum[1], sum[2]) > CANCEL_RATIO * absSum)) continue;
    const normal = unit(sum);
    const c = centroids.get(island);
    if (normal === null || c === undefined || c[3] === 0) continue;
    const basis = basisFor(verts, tris, ids, island, normal);
    if (basis === null) continue;
    const cu = (c[0] * basis.u[0] + c[1] * basis.u[1] + c[2] * basis.u[2]) / c[3];
    const cv = (c[0] * basis.v[0] + c[1] * basis.v[1] + c[2] * basis.v[2]) / c[3];
    if (!Number.isFinite(cu) || !Number.isFinite(cv)) continue;
    planes.set(island, { ...basis, cu, cv });
  }
  return planes;
}

export function smartProjectInfo(
  uvs: readonly number[], verts: readonly number[], tris: readonly number[], ids: readonly number[], sel: ReadonlySet<number>,
  margin = 0.01,
): { uvs: number[]; projected: number; skipped: number } {
  const out = uvs.slice();
  const n = Math.min(uvs.length >> 1, Math.floor(verts.length / 3), ids.length);
  const wanted = new Set<number>();
  for (const i of sel) if (Number.isInteger(i) && i >= 0 && i < n) wanted.add(ids[i]);
  const planes = islandPlanes(verts, tris, ids, sel);
  const projected = new Set<number>();
  for (let i = 0; i < n; i++) {
    const plane = planes.get(ids[i]);
    if (plane === undefined) continue;
    const u = verts[3 * i] * plane.u[0] + verts[3 * i + 1] * plane.u[1] + verts[3 * i + 2] * plane.u[2] - plane.cu;
    const v = verts[3 * i] * plane.v[0] + verts[3 * i + 1] * plane.v[1] + verts[3 * i + 2] * plane.v[2] - plane.cv;
    if (!Number.isFinite(u) || !Number.isFinite(v)) continue;
    out[2 * i] = Math.fround(u);
    out[2 * i + 1] = Math.fround(v);
    projected.add(i);
  }
  const skipped = Math.max(0, wanted.size - planes.size);
  if (projected.size === 0) return { uvs: out, projected: 0, skipped };
  return { uvs: packIslands(out, ids, projected, margin), projected: projected.size, skipped };
}

export function smartProject(
  uvs: readonly number[], verts: readonly number[], tris: readonly number[], ids: readonly number[], sel: ReadonlySet<number>,
  margin = 0.01,
): number[] {
  return smartProjectInfo(uvs, verts, tris, ids, sel, margin).uvs;
}
