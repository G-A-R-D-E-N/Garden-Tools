import { bounds, pointInBox, type UvBox } from './uvMath';

export type SelectMode = 'vertex' | 'edge' | 'face' | 'island';
export type Align = 'min' | 'centre' | 'max';

const triCount = (tris: readonly number[]): number => Math.floor(tris.length / 3);

const triOk = (tris: readonly number[], t: number, n: number): boolean =>
  [0, 1, 2].every(k => tris[3 * t + k] >= 0 && tris[3 * t + k] < n);

const setComp = (out: number[], k: number, x: number): void => {
  if (out[k] !== x) out[k] = Math.fround(x);
};

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

export function mergeAtCentre(uvs: readonly number[], sel: ReadonlySet<number>): number[] {
  const out = uvs.slice();
  const b = bounds(uvs, sel);
  if (!b) return out;
  const cu = (b.minU + b.maxU) / 2, cv = (b.minV + b.maxV) / 2;
  for (const i of sel) {
    setComp(out, 2 * i, cu);
    setComp(out, 2 * i + 1, cv);
  }
  return out;
}

export function mergeByDistance(uvs: readonly number[], sel: ReadonlySet<number>, dist: number): number[] {
  const out = uvs.slice();
  if (!(dist >= 1e-9)) return out;
  const m = Array.from(sel);
  const parent = m.map((_, k) => k);
  const find = (start: number): number => {
    let i = start;
    while (parent[i] !== i) { parent[i] = parent[parent[i]]; i = parent[i]; }
    return i;
  };
  const grid = new Map<string, number[]>();
  m.forEach((vi, k) => {
    const u = uvs[2 * vi], v = uvs[2 * vi + 1];
    const cx = Math.floor(u / dist), cy = Math.floor(v / dist);
    for (let dx = -1; dx <= 1; dx++) {
      for (let dy = -1; dy <= 1; dy++) {
        for (const j of grid.get(`${cx + dx},${cy + dy}`) ?? []) {
          const du = uvs[2 * m[j]] - u, dv = uvs[2 * m[j] + 1] - v;
          if (du * du + dv * dv <= dist * dist) parent[find(k)] = find(j);
        }
      }
    }
    const key = `${cx},${cy}`;
    const cell = grid.get(key);
    if (cell) cell.push(k); else grid.set(key, [k]);
  });
  const sums = new Map<number, [number, number, number]>();
  m.forEach((vi, k) => {
    const r = find(k);
    const s: [number, number, number] = sums.get(r) ?? [0, 0, 0];
    s[0] += uvs[2 * vi]; s[1] += uvs[2 * vi + 1]; s[2]++;
    sums.set(r, s);
  });
  m.forEach((vi, k) => {
    const s = sums.get(find(k));
    if (s && s[2] > 1) {
      setComp(out, 2 * vi, s[0] / s[2]);
      setComp(out, 2 * vi + 1, s[1] / s[2]);
    }
  });
  return out;
}

export function alignSel(uvs: readonly number[], sel: ReadonlySet<number>, axis: 'u' | 'v', to: Align): number[] {
  const out = uvs.slice();
  const b = bounds(uvs, sel);
  if (!b) return out;
  const lo = axis === 'u' ? b.minU : b.minV, hi = axis === 'u' ? b.maxU : b.maxV;
  const x = to === 'min' ? lo : to === 'max' ? hi : (lo + hi) / 2;
  const k = axis === 'u' ? 0 : 1;
  for (const i of sel) setComp(out, 2 * i + k, x);
  return out;
}

export function snapGrid(uvs: readonly number[], sel: ReadonlySet<number>, step: number): number[] {
  const out = uvs.slice();
  if (!(step > 0)) return out;
  for (const i of sel) {
    for (let k = 0; k < 2; k++) setComp(out, 2 * i + k, Math.round(uvs[2 * i + k] / step) * step);
  }
  return out;
}

export function snapPixels(uvs: readonly number[], sel: ReadonlySet<number>, w: number, h: number, centre: boolean): number[] {
  const out = uvs.slice();
  if (!(w > 0 && h > 0)) return out;
  const snap = (x: number, size: number): number =>
    centre ? (Math.min(size - 1, Math.floor(x * size)) + 0.5) / size : Math.round(x * size) / size;
  for (const i of sel) {
    setComp(out, 2 * i, snap(uvs[2 * i], w));
    setComp(out, 2 * i + 1, snap(uvs[2 * i + 1], h));
  }
  return out;
}

export function clampTile(uvs: readonly number[], sel: ReadonlySet<number>): number[] {
  const out = uvs.slice();
  for (const i of sel) {
    for (let k = 0; k < 2; k++) setComp(out, 2 * i + k, Math.min(1, Math.max(0, uvs[2 * i + k])));
  }
  return out;
}
