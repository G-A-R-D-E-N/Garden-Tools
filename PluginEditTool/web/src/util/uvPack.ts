import type { UvBox } from './uvMath';

export const triCount = (tris: readonly number[]): number => Math.floor(tris.length / 3);

export const triOk = (tris: readonly number[], t: number, n: number): boolean =>
  [0, 1, 2].every(k => tris[3 * t + k] >= 0 && tris[3 * t + k] < n);

const setComp = (out: number[], k: number, x: number): void => {
  if (out[k] !== x) out[k] = Math.fround(x);
};

const uvArea = (uvs: readonly number[], a: number, b: number, c: number): number =>
  Math.abs((uvs[2 * b] - uvs[2 * a]) * (uvs[2 * c + 1] - uvs[2 * a + 1]) - (uvs[2 * b + 1] - uvs[2 * a + 1]) * (uvs[2 * c] - uvs[2 * a])) / 2;

const area3d = (verts: readonly number[], a: number, b: number, c: number): number => {
  const ux = verts[3 * b] - verts[3 * a], uy = verts[3 * b + 1] - verts[3 * a + 1], uz = verts[3 * b + 2] - verts[3 * a + 2];
  const vx = verts[3 * c] - verts[3 * a], vy = verts[3 * c + 1] - verts[3 * a + 1], vz = verts[3 * c + 2] - verts[3 * a + 2];
  return Math.hypot(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx) / 2;
};

const triAreas = (uvs: readonly number[], verts: readonly number[], tris: readonly number[], t: number): [number, number] => {
  const a = tris[3 * t], b = tris[3 * t + 1], c = tris[3 * t + 2];
  return [uvArea(uvs, a, b, c), area3d(verts, a, b, c)];
};

const selIslands = (ids: readonly number[], n: number, sel: ReadonlySet<number>): Set<number> => {
  const out = new Set<number>();
  for (const i of sel) if (i >= 0 && i < n) out.add(ids[i]);
  return out;
};

const islandBoxes = (uvs: readonly number[], ids: readonly number[], n: number, keep: ReadonlySet<number>): Map<number, UvBox> => {
  const boxes = new Map<number, UvBox>();
  for (let i = 0; i < n; i++) {
    if (!keep.has(ids[i])) continue;
    const u = uvs[2 * i], v = uvs[2 * i + 1];
    const b = boxes.get(ids[i]);
    if (b) {
      b.minU = Math.min(b.minU, u); b.minV = Math.min(b.minV, v);
      b.maxU = Math.max(b.maxU, u); b.maxV = Math.max(b.maxV, v);
    } else boxes.set(ids[i], { minU: u, minV: v, maxU: u, maxV: v });
  }
  return boxes;
};

export function packIslands(uvs: readonly number[], ids: readonly number[], sel: ReadonlySet<number>, margin: number): number[] {
  const out = uvs.slice();
  const n = Math.min(uvs.length >> 1, ids.length);
  const boxes = islandBoxes(uvs, ids, n, selIslands(ids, n, sel));
  const items = Array.from(boxes, ([isl, b]) => ({ isl, b, w: b.maxU - b.minU, h: b.maxV - b.minV }))
    .filter(it => it.w > 1e-12 || it.h > 1e-12)
    .sort((p, q) => q.h - p.h);
  if (!items.length) return out;
  const m = Number.isFinite(margin) ? Math.min(0.25, Math.max(0, margin)) : 0;
  const maxExt = items.reduce((mx, it) => Math.max(mx, it.w, it.h), 0);
  const place = (s: number): [number, number][] | null => {
    const slots: [number, number][] = [];
    let x = m, y = m, shelf = 0;
    for (const it of items) {
      const w = it.w * s, h = it.h * s;
      if (x + w > 1 - m) { x = m; y += shelf + m; shelf = 0; }
      if (x + w > 1 - m || y + h > 1 - m) return null;
      slots.push([x, y]);
      x += w + m;
      shelf = Math.max(shelf, h);
    }
    return slots;
  };
  let lo = 0, hi = 1 / maxExt, best: [number, number][] | null = null;
  for (let iter = 0; iter < 40; iter++) {
    const mid = (lo + hi) / 2;
    const slots = place(mid);
    if (slots) { lo = mid; best = slots; } else hi = mid;
  }
  if (!best) return out;
  const fit = best;
  const at = new Map<number, { x: number; y: number; b: UvBox }>();
  items.forEach((it, k) => at.set(it.isl, { x: fit[k][0], y: fit[k][1], b: it.b }));
  for (let i = 0; i < n; i++) {
    const p = at.get(ids[i]);
    if (!p) continue;
    setComp(out, 2 * i, p.x + (uvs[2 * i] - p.b.minU) * lo);
    setComp(out, 2 * i + 1, p.y + (uvs[2 * i + 1] - p.b.minV) * lo);
  }
  return out;
}

export function averageIslandScale(uvs: readonly number[], verts: readonly number[], tris: readonly number[], ids: readonly number[], sel: ReadonlySet<number>): number[] {
  const out = uvs.slice();
  const n = Math.min(uvs.length >> 1, Math.floor(verts.length / 3), ids.length);
  const boxes = islandBoxes(uvs, ids, n, selIslands(ids, n, sel));
  const area = new Map<number, [number, number]>();
  for (let t = 0; t < triCount(tris); t++) {
    if (!triOk(tris, t, n)) continue;
    const isl = ids[tris[3 * t]];
    if (!boxes.has(isl)) continue;
    const [uv, a3] = triAreas(uvs, verts, tris, t);
    const s: [number, number] = area.get(isl) ?? [0, 0];
    s[0] += uv; s[1] += a3;
    area.set(isl, s);
  }
  const valid: { isl: number; r: number; a3: number; cu: number; cv: number }[] = [];
  for (const [isl, b] of boxes) {
    const [uv, a3] = area.get(isl) ?? [0, 0];
    if (uv > 1e-12 && a3 > 1e-12) valid.push({ isl, r: Math.sqrt(uv / a3), a3, cu: (b.minU + b.maxU) / 2, cv: (b.minV + b.maxV) / 2 });
  }
  if (valid.length < 2) return out;
  const target = valid.reduce((acc, v) => acc + v.a3 * v.r, 0) / valid.reduce((acc, v) => acc + v.a3, 0);
  if (!Number.isFinite(target) || target <= 0) return out;
  const fix = new Map<number, { k: number; cu: number; cv: number }>();
  const clampScale = (k: number): number => (Number.isFinite(k) ? Math.min(1000, Math.max(0.001, k)) : 1);
  for (const v of valid) fix.set(v.isl, { k: clampScale(target / v.r), cu: v.cu, cv: v.cv });
  for (let i = 0; i < n; i++) {
    const f = fix.get(ids[i]);
    if (!f) continue;
    setComp(out, 2 * i, f.cu + (uvs[2 * i] - f.cu) * f.k);
    setComp(out, 2 * i + 1, f.cv + (uvs[2 * i + 1] - f.cv) * f.k);
  }
  return out;
}

export function triStretch(uvs: readonly number[], verts: readonly number[], tris: readonly number[]): number[] {
  const n = Math.min(uvs.length >> 1, Math.floor(verts.length / 3));
  const areas = Array.from({ length: triCount(tris) }, (_, t): [number, number] =>
    triOk(tris, t, n) ? triAreas(uvs, verts, tris, t) : [0, 0]);
  const good = (p: [number, number]): boolean => p[0] > 1e-12 && p[1] > 1e-12;
  let uvSum = 0, a3Sum = 0;
  for (const p of areas) if (good(p)) { uvSum += p[0]; a3Sum += p[1]; }
  const m = uvSum / a3Sum;
  const ln4 = Math.log(4);
  return areas.map(p => (good(p) ? Math.min(1, Math.abs(Math.log(p[0] / p[1] / m)) / ln4) : 0));
}
