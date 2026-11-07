const triCount = (tris: readonly number[]): number => Math.floor(tris.length / 3);

const triOk = (tris: readonly number[], t: number, n: number): boolean =>
  [0, 1, 2].every(k => tris[3 * t + k] >= 0 && tris[3 * t + k] < n);

const setComp = (out: number[], k: number, x: number): void => {
  if (out[k] !== x) out[k] = Math.fround(x);
};

const eachEdge = (n: number, tris: readonly number[], fn: (a: number, b: number) => void): void => {
  for (let t = 0; t < triCount(tris); t++) {
    if (!triOk(tris, t, n)) continue;
    for (let k = 0; k < 3; k++) {
      const a = tris[3 * t + k], b = tris[3 * t + ((k + 1) % 3)];
      if (a !== b) fn(a, b);
    }
  }
};

export function boundaryVerts(vertexCount: number, tris: readonly number[]): Set<number> {
  const edges = new Map<number, [number, number, number]>();
  eachEdge(vertexCount, tris, (a, b) => {
    const lo = Math.min(a, b), hi = Math.max(a, b), key = lo * vertexCount + hi;
    const e = edges.get(key);
    if (e) e[2]++; else edges.set(key, [lo, hi, 1]);
  });
  const out = new Set<number>();
  for (const [lo, hi, uses] of edges.values()) {
    if (uses === 1) { out.add(lo); out.add(hi); }
  }
  return out;
}

export function relaxUvs(uvs: readonly number[], tris: readonly number[], sel: ReadonlySet<number>, iterations: number): number[] {
  const n = uvs.length >> 1;
  const passes = Number.isNaN(iterations) ? 0 : Math.min(200, Math.max(0, Math.floor(iterations)));
  const bnd = boundaryVerts(n, tris);
  const nb = new Map<number, Set<number>>();
  for (const i of sel) if (Number.isInteger(i) && i >= 0 && i < n && !bnd.has(i)) nb.set(i, new Set());
  eachEdge(n, tris, (a, b) => {
    nb.get(a)?.add(b);
    nb.get(b)?.add(a);
  });
  let cur = uvs.slice();
  for (let p = 0; p < passes; p++) {
    const next = cur.slice();
    for (const [i, ns] of nb) {
      if (!ns.size) continue;
      let su = 0, sv = 0;
      for (const j of ns) { su += cur[2 * j]; sv += cur[2 * j + 1]; }
      setComp(next, 2 * i, su / ns.size);
      setComp(next, 2 * i + 1, sv / ns.size);
    }
    cur = next;
  }
  return cur;
}

export function findStitchPairs(verts: readonly number[], ids: readonly number[], sel: ReadonlySet<number>, eps: number): [number, number][] {
  if (!(eps >= 1e-9)) return [];
  const n = Math.min(Math.floor(verts.length / 3), ids.length);
  const finite = (i: number): boolean => [0, 1, 2].every(k => Number.isFinite(verts[3 * i + k]));
  const pts = Array.from(sel)
    .filter(i => Number.isInteger(i) && i >= 0 && i < n && finite(i))
    .sort((p, q) => p - q);
  const cellOf = (i: number): number[] => [0, 1, 2].map(k => Math.floor(verts[3 * i + k] / eps));
  const grid = new Map<string, number[]>();
  for (const i of pts) {
    const key = cellOf(i).join(',');
    const bucket = grid.get(key);
    if (bucket) bucket.push(i); else grid.set(key, [i]);
  }
  const out: [number, number][] = [];
  for (const a of pts) {
    const [cx, cy, cz] = cellOf(a);
    const near = new Set<number>();
    for (let dx = -1; dx <= 1; dx++) {
      for (let dy = -1; dy <= 1; dy++) {
        for (let dz = -1; dz <= 1; dz++) {
          for (const b of grid.get([cx + dx, cy + dy, cz + dz].join(',')) ?? []) {
            if (b > a) near.add(b);
          }
        }
      }
    }
    for (const b of Array.from(near).sort((p, q) => p - q)) {
      if (ids[a] === ids[b]) continue;
      const ex = verts[3 * a] - verts[3 * b], ey = verts[3 * a + 1] - verts[3 * b + 1], ez = verts[3 * a + 2] - verts[3 * b + 2];
      if (ex * ex + ey * ey + ez * ez <= eps * eps) out.push([a, b]);
    }
  }
  return out;
}

const nextIsland = (pairs: readonly [number, number][], ids: readonly number[], anchored: ReadonlySet<number>): number | null => {
  let best: number | null = null;
  for (const [a, b] of pairs) {
    const ia = ids[a], ib = ids[b];
    if (anchored.has(ia) === anchored.has(ib)) continue;
    const cand = anchored.has(ia) ? ib : ia;
    if (best === null || cand < best) best = cand;
  }
  return best;
};

const unanchoredSeed = (pairs: readonly [number, number][], ids: readonly number[], anchored: ReadonlySet<number>): number | null => {
  let best: number | null = null;
  for (const [a, b] of pairs) {
    if (anchored.has(ids[a]) || anchored.has(ids[b])) continue;
    const cand = Math.min(ids[a], ids[b]);
    if (best === null || cand < best) best = cand;
  }
  return best;
};

const stitchIsland = (out: number[], ids: readonly number[], n: number, m: number, pairs: readonly [number, number][],
                      anchored: ReadonlySet<number>): void => {
  const corr: [number, number][] = [];
  for (const [a, b] of pairs) {
    if (ids[a] === m && anchored.has(ids[b])) corr.push([a, b]);
    else if (ids[b] === m && anchored.has(ids[a])) corr.push([b, a]);
  }
  const k = corr.length;
  let pu = 0, pv = 0, qu = 0, qv = 0;
  for (const [a, b] of corr) {
    pu += out[2 * a]; pv += out[2 * a + 1];
    qu += out[2 * b]; qv += out[2 * b + 1];
  }
  pu /= k; pv /= k; qu /= k; qv /= k;
  let dot = 0, cross = 0;
  for (const [a, b] of corr) {
    const xu = out[2 * a] - pu, xv = out[2 * a + 1] - pv;
    const yu = out[2 * b] - qu, yv = out[2 * b + 1] - qv;
    dot += xu * yu + xv * yv;
    cross += xu * yv - xv * yu;
  }
  const rot = k > 1 ? Math.atan2(cross, dot) : 0, c = Math.cos(rot), s = Math.sin(rot);
  for (let i = 0; i < n; i++) {
    if (ids[i] !== m) continue;
    const xu = out[2 * i] - pu, xv = out[2 * i + 1] - pv;
    setComp(out, 2 * i, qu + xu * c - xv * s);
    setComp(out, 2 * i + 1, qv + xu * s + xv * c);
  }
  const mids = corr.map(([a, b]): [number, number] => [(out[2 * a] + out[2 * b]) / 2, (out[2 * a + 1] + out[2 * b + 1]) / 2]);
  corr.forEach(([a, b], j) => {
    const [mu, mv] = mids[j];
    setComp(out, 2 * a, mu); setComp(out, 2 * a + 1, mv);
    setComp(out, 2 * b, mu); setComp(out, 2 * b + 1, mv);
  });
};

export function stitchSelection(uvs: readonly number[], verts: readonly number[], ids: readonly number[], sel: ReadonlySet<number>, eps: number): number[] {
  const out = uvs.slice();
  const pairs = findStitchPairs(verts, ids, sel, eps);
  if (!pairs.length) return out;
  const n = Math.min(out.length >> 1, ids.length);
  const anchored = new Set<number>([ids[pairs[0][0]]]);
  for (;;) {
    let m = nextIsland(pairs, ids, anchored);
    while (m !== null) {
      stitchIsland(out, ids, n, m, pairs, anchored);
      anchored.add(m);
      m = nextIsland(pairs, ids, anchored);
    }
    const seed = unanchoredSeed(pairs, ids, anchored);
    if (seed === null) return out;
    anchored.add(seed);
  }
}

const edgeNormals = (c: number[]): [number, number][] => [0, 1, 2].map((e): [number, number] => {
  const j = (e + 1) % 3;
  const du = c[2 * j] - c[2 * e], dv = c[2 * j + 1] - c[2 * e + 1], len = Math.hypot(du, dv);
  return [-dv / len, du / len];
});

const projection = (c: number[], ax: number, ay: number): [number, number] => {
  const d = [0, 1, 2].map(k => c[2 * k] * ax + c[2 * k + 1] * ay);
  return [Math.min(...d), Math.max(...d)];
};

const interiorsOverlap = (p: number[], q: number[]): boolean =>
  edgeNormals(p).concat(edgeNormals(q)).every(([ax, ay]) => {
    const [pa, pb] = projection(p, ax, ay), [qa, qb] = projection(q, ax, ay);
    return Math.min(pb, qb) - Math.max(pa, qa) > 1e-9;
  });

export function overlappingFaces(uvs: readonly number[], tris: readonly number[], hidden: ReadonlySet<number>): number[] {
  const n = uvs.length >> 1;
  const live: { t: number; c: number[]; minU: number; maxU: number; minV: number; maxV: number }[] = [];
  for (let t = 0; t < triCount(tris); t++) {
    if (hidden.has(t) || !triOk(tris, t, n)) continue;
    const c: number[] = [];
    for (let k = 0; k < 3; k++) c.push(uvs[2 * tris[3 * t + k]], uvs[2 * tris[3 * t + k] + 1]);
    if (!c.every(x => Number.isFinite(x))) continue;
    const twiceArea = (c[2] - c[0]) * (c[5] - c[1]) - (c[3] - c[1]) * (c[4] - c[0]);
    if (Math.abs(twiceArea) <= 2e-12) continue;
    live.push({
      t, c,
      minU: Math.min(c[0], c[2], c[4]), maxU: Math.max(c[0], c[2], c[4]),
      minV: Math.min(c[1], c[3], c[5]), maxV: Math.max(c[1], c[3], c[5]),
    });
  }
  if (live.length < 2) return [];
  let extent = 0;
  for (const f of live) extent += Math.max(f.maxU - f.minU, f.maxV - f.minV);
  const cell = Math.max(extent / live.length, 1e-9);
  const grid = new Map<string, number[]>();
  const large: number[] = [];
  live.forEach((f, j) => {
    const u0 = Math.floor(f.minU / cell), u1 = Math.floor(f.maxU / cell);
    const v0 = Math.floor(f.minV / cell), v1 = Math.floor(f.maxV / cell);
    if ((u1 - u0 + 1) * (v1 - v0 + 1) > 1024) { large.push(j); return; }
    for (let cu = u0; cu <= u1; cu++) {
      for (let cv = v0; cv <= v1; cv++) {
        const key = cu + ',' + cv;
        const bucket = grid.get(key);
        if (bucket) bucket.push(j); else grid.set(key, [j]);
      }
    }
  });
  const hit = new Set<number>();
  const test = (p: (typeof live)[number], q: (typeof live)[number]): void => {
    if (Math.min(p.maxU, q.maxU) - Math.max(p.minU, q.minU) <= 1e-9) return;
    if (Math.min(p.maxV, q.maxV) - Math.max(p.minV, q.minV) <= 1e-9) return;
    if (!interiorsOverlap(p.c, q.c)) return;
    hit.add(p.t); hit.add(q.t);
  };
  for (const bucket of grid.values()) {
    for (let x = 0; x < bucket.length; x++) {
      for (let y = x + 1; y < bucket.length; y++) test(live[bucket[x]], live[bucket[y]]);
    }
  }
  for (const j of large) {
    for (let k = 0; k < live.length; k++) if (k !== j) test(live[j], live[k]);
  }
  return Array.from(hit).sort((p, q) => p - q);
}
