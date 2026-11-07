export interface UvPoint { u: number; v: number; }
export interface UvBox { minU: number; minV: number; maxU: number; maxV: number; }
export interface UvXform { tx: number; ty: number; sx: number; sy: number; rotRad: number; pivot: UvPoint; }
export interface UvView { scale: number; px: number; py: number; }
export type PivotMode = 'centre' | 'median' | 'tile';

export function islands(vertexCount: number, tris: readonly number[]): number[] {
  const parent = Array.from({ length: vertexCount }, (_, i) => i);
  const root = (start: number): number => {
    let i = start;
    while (parent[i] !== i) { parent[i] = parent[parent[i]]; i = parent[i]; }
    return i;
  };
  for (let t = 0; t + 2 < tris.length; t += 3) {
    const a = tris[t], b = tris[t + 1], c = tris[t + 2];
    if (a >= vertexCount || b >= vertexCount || c >= vertexCount) continue;
    parent[root(a)] = root(b);
    parent[root(b)] = root(c);
  }
  const ids = new Map<number, number>();
  return Array.from({ length: vertexCount }, (_, i) => {
    const r = root(i);
    if (!ids.has(r)) ids.set(r, ids.size);
    return ids.get(r) ?? 0;
  });
}

export function allVerts(uvs: readonly number[]): ReadonlySet<number> {
  return new Set(Array.from({ length: uvs.length >> 1 }, (_, i) => i));
}

export function bounds(uvs: readonly number[], sel: ReadonlySet<number>): UvBox | null {
  if (!sel.size) return null;
  let minU = Infinity, minV = Infinity, maxU = -Infinity, maxV = -Infinity;
  for (const i of sel) {
    const u = uvs[2 * i], v = uvs[2 * i + 1];
    minU = Math.min(minU, u); minV = Math.min(minV, v);
    maxU = Math.max(maxU, u); maxV = Math.max(maxV, v);
  }
  return { minU, minV, maxU, maxV };
}

const middle = (xs: number[]): number => {
  const s = [...xs].sort((a, b) => a - b);
  const m = s.length >> 1;
  return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
};

export function median(uvs: readonly number[], sel: ReadonlySet<number>): UvPoint | null {
  if (!sel.size) return null;
  const us: number[] = [], vs: number[] = [];
  for (const i of sel) { us.push(uvs[2 * i]); vs.push(uvs[2 * i + 1]); }
  return { u: middle(us), v: middle(vs) };
}

export function resolvePivot(uvs: readonly number[], sel: ReadonlySet<number>, mode: PivotMode): UvPoint {
  if (mode === 'tile') return { u: 0.5, v: 0.5 };
  if (mode === 'median') return median(uvs, sel) ?? { u: 0.5, v: 0.5 };
  const b = bounds(uvs, sel);
  return b ? { u: (b.minU + b.maxU) / 2, v: (b.minV + b.maxV) / 2 } : { u: 0.5, v: 0.5 };
}

export function transformSel(uvs: readonly number[], sel: ReadonlySet<number>, x: UvXform): number[] {
  const out = uvs.slice();
  const c = Math.cos(x.rotRad), s = Math.sin(x.rotRad);
  for (const i of sel) {
    const dx = (uvs[2 * i] - x.pivot.u) * x.sx;
    const dy = (uvs[2 * i + 1] - x.pivot.v) * x.sy;
    out[2 * i] = Math.fround(x.pivot.u + dx * c - dy * s + x.tx);
    out[2 * i + 1] = Math.fround(x.pivot.v + dx * s + dy * c + x.ty);
  }
  return out;
}

export function pointInBox(u: number, v: number, b: UvBox): boolean {
  return u >= b.minU && u <= b.maxU && v >= b.minV && v <= b.maxV;
}

export function pickNearest(uvs: readonly number[], u: number, v: number, maxDist: number): number {
  let best = -1, bestD = maxDist * maxDist;
  for (let i = 0; 2 * i + 1 < uvs.length; i++) {
    const d = (uvs[2 * i] - u) ** 2 + (uvs[2 * i + 1] - v) ** 2;
    if (d <= bestD) { best = i; bestD = d; }
  }
  return best;
}

export function frameView(uvs: readonly number[], w: number, h: number): UvView {
  const b = bounds(uvs, allVerts(uvs)) ?? { minU: 0, minV: 0, maxU: 1, maxV: 1 };
  const minU = Math.min(b.minU, 0), maxU = Math.max(b.maxU, 1);
  const minV = Math.min(b.minV, 0), maxV = Math.max(b.maxV, 1);
  const scale = 0.9 * Math.min(w / (maxU - minU), h / (maxV - minV));
  return { scale, px: w / 2 - ((minU + maxU) / 2) * scale, py: h / 2 - ((minV + maxV) / 2) * scale };
}
