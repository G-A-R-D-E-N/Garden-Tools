const triCount = (tris: readonly number[]): number => Math.floor(tris.length / 3);

const triOk = (tris: readonly number[], t: number, n: number): boolean =>
  [0, 1, 2].every(k => tris[3 * t + k] >= 0 && tris[3 * t + k] < n);

const local2d = (verts: readonly number[], a: number, b: number, c: number): [number, number][] | null => {
  const ax = verts[3 * a], ay = verts[3 * a + 1], az = verts[3 * a + 2];
  const e1x = verts[3 * b] - ax, e1y = verts[3 * b + 1] - ay, e1z = verts[3 * b + 2] - az;
  const len1 = Math.hypot(e1x, e1y, e1z);
  if (!(len1 > 1e-12)) return null;
  const ux = e1x / len1, uy = e1y / len1, uz = e1z / len1;
  const e2x = verts[3 * c] - ax, e2y = verts[3 * c + 1] - ay, e2z = verts[3 * c + 2] - az;
  const x3 = e2x * ux + e2y * uy + e2z * uz;
  const y3 = Math.hypot(e2x - ux * x3, e2y - uy * x3, e2z - uz * x3);
  if (!(y3 > 1e-12)) return null;
  return [[0, 0], [len1, 0], [x3, y3]];
};

type System = {
  rowStart: Int32Array;
  rowCol: Int32Array;
  rowVal: Float64Array;
  rowCount: number;
  rhs: Float64Array;
  diag: Float64Array;
  dof: number;
};

const buildSystem = (verts: readonly number[], tris: readonly number[], members: readonly number[], free: ReadonlyMap<number, number>,
                     pins: ReadonlyMap<number, [number, number]>): System | null => {
  const local = new Map<number, number>();
  members.forEach((v, i) => local.set(v, i));
  const cols: number[] = [];
  const vals: number[] = [];
  const start: number[] = [];
  const rhs: number[] = [];
  for (let t = 0; t < triCount(tris); t++) {
    if (!triOk(tris, t, verts.length / 3)) continue;
    const tri = [tris[3 * t], tris[3 * t + 1], tris[3 * t + 2]];
    if (!tri.every(v => local.has(v))) continue;
    const xy = local2d(verts, tri[0], tri[1], tri[2]);
    if (xy === null) continue;
    const w: [number, number][] = [
      [xy[2][0] - xy[1][0], xy[2][1] - xy[1][1]],
      [-xy[2][0], -xy[2][1]],
      [xy[1][0], 0],
    ];
    for (const imaginary of [false, true]) {
      const at = cols.length;
      start.push(at);
      rhs.push(0);
      for (let j = 0; j < 3; j++) {
        const [re, im] = w[j];
        const uCoef = imaginary ? im : re;
        const vCoef = imaginary ? re : -im;
        const vertex = tri[j];
        const base = free.get(vertex);
        if (base !== undefined) {
          if (uCoef !== 0) { cols.push(base); vals.push(uCoef); }
          if (vCoef !== 0) { cols.push(base + 1); vals.push(vCoef); }
        } else {
          const pin = pins.get(vertex) ?? [0, 0];
          rhs[rhs.length - 1] -= uCoef * pin[0] + vCoef * pin[1];
        }
      }
    }
  }
  if (!rhs.length) return null;
  start.push(cols.length);
  const dof = 2 * free.size;
  const diag = new Float64Array(dof);
  for (let i = 0; i < cols.length; i++) diag[cols[i]] += vals[i] * vals[i];
  return {
    rowStart: Int32Array.from(start), rowCol: Int32Array.from(cols), rowVal: Float64Array.from(vals),
    rowCount: rhs.length, rhs: Float64Array.from(rhs), diag, dof,
  };
};

const solveSystem = (sys: System): { x: Float64Array; converged: boolean } | null => {
  const { rowStart, rowCol, rowVal, rowCount, rhs, diag, dof } = sys;
  if (dof === 0 || rowCount === 0) return null;
  const rowVec = new Float64Array(rowCount);
  const mulM = (vec: Float64Array, out: Float64Array): void => {
    for (let row = 0; row < rowCount; row++) {
      let sum = 0;
      for (let k = rowStart[row]; k < rowStart[row + 1]; k++) sum += rowVal[k] * vec[rowCol[k]];
      out[row] = sum;
    }
  };
  const mulMT = (vec: Float64Array, out: Float64Array): void => {
    out.fill(0);
    for (let row = 0; row < rowCount; row++) {
      const value = vec[row];
      if (value === 0) continue;
      for (let k = rowStart[row]; k < rowStart[row + 1]; k++) out[rowCol[k]] += rowVal[k] * value;
    }
  };
  const rhsNormal = new Float64Array(dof);
  mulMT(rhs, rhsNormal);
  let base = 0;
  for (let i = 0; i < dof; i++) base += rhsNormal[i] * rhsNormal[i];
  if (!(base > 0) || !Number.isFinite(base)) return null;
  let trace = 0;
  for (let i = 0; i < dof; i++) trace += diag[i];
  if (!(trace > 0) || !Number.isFinite(trace)) return null;
  const lambda = 1e-12 * trace / dof;
  const x = new Float64Array(dof);
  const r = Float64Array.from(rhsNormal);
  const z = new Float64Array(dof);
  const p = new Float64Array(dof);
  const ap = new Float64Array(dof);
  const products = new Float64Array(dof);
  const normalProduct = (vec: Float64Array, out: Float64Array): void => {
    mulM(vec, rowVec);
    mulMT(rowVec, out);
    for (let i = 0; i < dof; i++) out[i] += lambda * vec[i];
  };
  const pre = (vec: Float64Array, out: Float64Array): void => {
    for (let i = 0; i < dof; i++) {
      const d = diag[i] + lambda;
      out[i] = d > 0 ? vec[i] / d : vec[i];
    }
  };
  normalProduct(x, ap);
  for (let i = 0; i < dof; i++) r[i] -= ap[i];
  pre(r, z);
  p.set(z);
  let rz = 0;
  for (let i = 0; i < dof; i++) rz += r[i] * z[i];
  const target = 1e-22 * base;
  for (let iter = 0; iter < 2000 && rz > target; iter++) {
    normalProduct(p, ap);
    let pap = 0;
    for (let i = 0; i < dof; i++) pap += p[i] * ap[i];
    if (!(pap > 0) || !Number.isFinite(pap)) return null;
    const alpha = rz / pap;
    for (let i = 0; i < dof; i++) { x[i] += alpha * p[i]; r[i] -= alpha * ap[i]; }
    pre(r, z);
    let rzNext = 0;
    for (let i = 0; i < dof; i++) rzNext += r[i] * z[i];
    const beta = rzNext / rz;
    rz = rzNext;
    for (let i = 0; i < dof; i++) p[i] = z[i] + beta * p[i];
    if (!Number.isFinite(rz)) return null;
    if (rz <= target) break;
  }
  for (let i = 0; i < dof; i++) if (!Number.isFinite(x[i])) return null;
  normalProduct(x, products);
  let res = 0;
  for (let i = 0; i < dof; i++) {
    const d = rhsNormal[i] - products[i];
    res += d * d;
  }
  const scale = Math.max(1.0, Math.sqrt(base));
  return { x, converged: Math.sqrt(res) <= 1e-9 * scale };
};
export function lscmUnwrapInfo(
  uvs: readonly number[], verts: readonly number[], tris: readonly number[], ids: readonly number[],
  sel: ReadonlySet<number>, pins: ReadonlyMap<number, [number, number]>,
): { uvs: number[]; unwrapped: number; skipped: number } {
  const out = uvs.slice();
  const n = Math.min(uvs.length >> 1, Math.floor(verts.length / 3), ids.length);
  const wanted = new Set<number>();
  for (const i of sel) if (Number.isInteger(i) && i >= 0 && i < n) wanted.add(ids[i]);
  let unwrapped = 0;
  let skipped = 0;
  for (const island of Array.from(wanted).sort((a, b) => a - b)) {
    const members: number[] = [];
    for (let i = 0; i < n; i++) if (ids[i] === island) members.push(i);
    const islandPins = new Map<number, [number, number]>();
    for (const v of members) {
      const pin = pins.get(v);
      if (pin !== undefined) islandPins.set(v, pin);
    }
    if (islandPins.size < 2) { skipped++; continue; }
    const pinList = Array.from(islandPins.values());
    let spread = 0;
    for (let i = 1; i < pinList.length; i++) {
      spread = Math.max(spread, Math.hypot(pinList[i][0] - pinList[0][0], pinList[i][1] - pinList[0][1]));
    }
    if (!(spread > 1e-6)) { skipped++; continue; }
    const free = new Map<number, number>();
    for (const v of members) if (!islandPins.has(v)) free.set(v, 2 * free.size);
    const sys = buildSystem(verts, tris, members, free, islandPins);
    if (sys === null) { skipped++; continue; }
    const solved = solveSystem(sys);
    if (solved === null || !solved.converged) { skipped++; continue; }
    let wrote = false;
    for (const v of members) {
      const pin = islandPins.get(v);
      const base = free.get(v);
      if (pin === undefined && (base === undefined || (sys.diag[base] === 0 && sys.diag[base + 1] === 0))) continue;
      const u = pin !== undefined ? pin[0] : solved.x[base!];
      const w = pin !== undefined ? pin[1] : solved.x[base! + 1];
      if (!Number.isFinite(u) || !Number.isFinite(w)) continue;
      out[2 * v] = Math.fround(u);
      out[2 * v + 1] = Math.fround(w);
      wrote = true;
    }
    if (wrote) unwrapped++; else skipped++;
  }
  return { uvs: out, unwrapped, skipped };
}

export function lscmUnwrap(
  uvs: readonly number[], verts: readonly number[], tris: readonly number[], ids: readonly number[],
  sel: ReadonlySet<number>, pins: ReadonlyMap<number, [number, number]>,
): number[] {
  return lscmUnwrapInfo(uvs, verts, tris, ids, sel, pins).uvs;
}
