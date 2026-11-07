export type SplitPlan = { sources: number[]; tris: number[] };

export const edgeKey = (a: number, b: number): number => (a < b ? a * 65536 + b : b * 65536 + a);

export function seamsInSelection(tris: readonly number[], sel: ReadonlySet<number>, hidden: ReadonlySet<number> = new Set()): Set<number> {
  const seams = new Set<number>();
  for (let t = 0; 3 * t + 2 < tris.length; t++) {
    if (hidden.has(t)) continue;
    for (let k = 0; k < 3; k++) {
      const a = tris[3 * t + k], b = tris[3 * t + ((k + 1) % 3)];
      if (a !== b && sel.has(a) && sel.has(b)) seams.add(edgeKey(a, b));
    }
  }
  return seams;
}

export function splitPlan(vertexCount: number, tris: readonly number[], seams: ReadonlySet<number>): SplitPlan {
  const sources: number[] = [];
  const out = tris.slice();
  const count = Math.floor(tris.length / 3);
  const incident: number[][] = Array.from({ length: vertexCount }, () => []);
  const edgeTris = new Map<number, number[]>();
  for (let t = 0; t < count; t++) {
    const a = tris[3 * t], b = tris[3 * t + 1], c = tris[3 * t + 2];
    if (![a, b, c].every(v => v >= 0 && v < vertexCount)) continue;
    if (a !== b) incident[a].push(t);
    if (b !== c) incident[b].push(t);
    if (c !== a) incident[c].push(t);
    for (const [p, q] of [[a, b], [b, c], [c, a]]) {
      if (p === q) continue;
      const key = edgeKey(p, q);
      const bucket = edgeTris.get(key);
      if (bucket) bucket.push(t); else edgeTris.set(key, [t]);
    }
  }
  for (let v = 0; v < vertexCount; v++) {
    const list = incident[v];
    if (list.length < 2) continue;
    const pos = new Map<number, number>();
    list.forEach((t, i) => pos.set(t, i));
    const parent = list.map((_, i) => i);
    const find = (start: number): number => {
      let i = start;
      while (parent[i] !== i) { parent[i] = parent[parent[i]]; i = parent[i]; }
      return i;
    };
    const join = (p: number, q: number): void => {
      const a = find(p), b = find(q);
      if (a !== b) parent[b] = a;
    };
    for (const t of list) {
      for (let k = 0; k < 3; k++) {
        const a = tris[3 * t + k], b = tris[3 * t + ((k + 1) % 3)];
        const other = a === v ? b : b === v ? a : -1;
        if (other < 0 || other === v) continue;
        if (seams.has(edgeKey(v, other))) continue;
        for (const shared of edgeTris.get(edgeKey(v, other)) ?? []) {
          if (shared !== t) join(pos.get(t)!, pos.get(shared)!);
        }
      }
    }
    const target = new Map<number, number>();
    list.forEach((t, i) => {
      const root = find(i);
      let to = target.get(root);
      if (to === undefined) {
        to = target.size === 0 ? v : vertexCount + sources.length;
        if (to !== v) sources.push(v);
        target.set(root, to);
      }
      for (let k = 0; k < 3; k++) if (tris[3 * t + k] === v) out[3 * t + k] = to;
    });
  }
  return { sources, tris: out };
}
