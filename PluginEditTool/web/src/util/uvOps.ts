import { pointInBox, type UvBox } from './uvMath';

export type SelectMode = 'vertex' | 'edge' | 'face' | 'island';
export type Align = 'min' | 'centre' | 'max';

const triCount = (tris: readonly number[]): number => Math.floor(tris.length / 3);

const triOk = (tris: readonly number[], t: number, n: number): boolean =>
  [0, 1, 2].every(k => tris[3 * t + k] >= 0 && tris[3 * t + k] < n);

const side = (uvs: readonly number[], i: number, j: number, u: number, v: number): number =>
  (uvs[2 * j] - uvs[2 * i]) * (v - uvs[2 * i + 1]) - (uvs[2 * j + 1] - uvs[2 * i + 1]) * (u - uvs[2 * i]);

const segDist = (uvs: readonly number[], i: number, j: number, u: number, v: number): number => {
  const ax = uvs[2 * i], ay = uvs[2 * i + 1];
  const dx = uvs[2 * j] - ax, dy = uvs[2 * j + 1] - ay;
  const len2 = dx * dx + dy * dy;
  const s = len2 ? Math.min(1, Math.max(0, ((u - ax) * dx + (v - ay) * dy) / len2)) : 0;
  return Math.hypot(u - ax - s * dx, v - ay - s * dy);
};

export function visibleVerts(vertexCount: number, tris: readonly number[], hidden: ReadonlySet<number>): ReadonlySet<number> {
  const visible = new Set<number>(), used = new Set<number>();
  for (let t = 0; t < triCount(tris); t++) {
    if (!triOk(tris, t, vertexCount)) continue;
    for (let k = 0; k < 3; k++) {
      used.add(tris[3 * t + k]);
      if (!hidden.has(t)) visible.add(tris[3 * t + k]);
    }
  }
  for (let i = 0; i < vertexCount; i++) if (!used.has(i)) visible.add(i);
  return visible;
}

export function pickEdge(uvs: readonly number[], tris: readonly number[], hidden: ReadonlySet<number>, u: number, v: number, maxDist: number): [number, number] | null {
  const n = uvs.length >> 1;
  let best: [number, number] | null = null, bestD = Infinity;
  for (let t = 0; t < triCount(tris); t++) {
    if (hidden.has(t) || !triOk(tris, t, n)) continue;
    for (let k = 0; k < 3; k++) {
      const i = tris[3 * t + k], j = tris[3 * t + ((k + 1) % 3)];
      const d = segDist(uvs, i, j, u, v);
      if (d <= maxDist && d < bestD) { best = [i, j]; bestD = d; }
    }
  }
  return best;
}

export function pickFace(uvs: readonly number[], tris: readonly number[], hidden: ReadonlySet<number>, u: number, v: number): [number, number, number] | null {
  const n = uvs.length >> 1;
  for (let t = triCount(tris) - 1; t >= 0; t--) {
    if (hidden.has(t) || !triOk(tris, t, n)) continue;
    const a = tris[3 * t], b = tris[3 * t + 1], c = tris[3 * t + 2];
    if (side(uvs, a, b, uvs[2 * c], uvs[2 * c + 1]) === 0) continue;
    const d1 = side(uvs, a, b, u, v), d2 = side(uvs, b, c, u, v), d3 = side(uvs, c, a, u, v);
    const neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    if (!(neg && pos)) return [a, b, c];
  }
  return null;
}

export function boxVerts(uvs: readonly number[], tris: readonly number[], hidden: ReadonlySet<number>, box: UvBox, mode: 'vertex' | 'edge' | 'face'): number[] {
  const n = uvs.length >> 1;
  const inside = (i: number): boolean => pointInBox(uvs[2 * i], uvs[2 * i + 1], box);
  const out = new Set<number>();
  if (mode === 'vertex') {
    for (const i of visibleVerts(n, tris, hidden)) if (inside(i)) out.add(i);
  } else {
    for (let t = 0; t < triCount(tris); t++) {
      if (hidden.has(t) || !triOk(tris, t, n)) continue;
      const ids = [tris[3 * t], tris[3 * t + 1], tris[3 * t + 2]];
      const ins = ids.map(inside);
      if (mode === 'face') {
        if (ins.every(x => x)) ids.forEach(i => out.add(i));
      } else {
        for (let k = 0; k < 3; k++) {
          const j = (k + 1) % 3;
          if (ins[k] && ins[j]) { out.add(ids[k]); out.add(ids[j]); }
        }
      }
    }
  }
  return Array.from(out).sort((p, q) => p - q);
}

export function invert(visible: ReadonlySet<number>, sel: ReadonlySet<number>): Set<number> {
  const out = new Set<number>();
  for (const i of visible) if (!sel.has(i)) out.add(i);
  return out;
}

export function linked(ids: readonly number[], sel: ReadonlySet<number>): number[] {
  const picked = new Set<number>();
  for (const i of sel) if (i < ids.length) picked.add(ids[i]);
  const out: number[] = [];
  for (let i = 0; i < ids.length; i++) if (picked.has(ids[i])) out.push(i);
  return out;
}

export function selectedTris(tris: readonly number[], vertexCount: number, sel: ReadonlySet<number>, hidden: ReadonlySet<number>): number[] {
  const out: number[] = [];
  for (let t = 0; t < triCount(tris); t++) {
    if (hidden.has(t) || !triOk(tris, t, vertexCount)) continue;
    if ([0, 1, 2].every(k => sel.has(tris[3 * t + k]))) out.push(t);
  }
  return out;
}
