// Constraint: slot paths, cell starts, tile leaf flags and the empty-slot LOD marker are derived on encode from the tree and the tiles, never stored in the decoded form.
import { slotPathBytes } from './uvd_tile_model.mjs';
import { FLT_MAX, decodeKd, decodeSymbols, encodeKd, encodeNodes, walkKd } from './uvd_tile_body.mjs';

const f32s = (buf, count) => Array.from({ length: count }, (_, i) => buf.readFloatLE(4 * i));
const u32s = (buf, count) => Array.from({ length: count }, (_, i) => buf.readUInt32LE(4 * i));

export function readPacked(buf, count, width) {
  const out = [];
  for (let i = 0; i < count; i++) {
    let v = 0;
    for (let b = 0; b < width; b++) { const bit = i * width + b; v += ((buf[bit >> 3] >> (bit & 7)) & 1) * 2 ** b; }
    out.push(v);
  }
  return out;
}

export function writePacked(values, width, bytes) {
  const buf = Buffer.alloc(bytes);
  values.forEach((v, i) => {
    for (let b = 0; b < width; b++) if (Math.floor(v / 2 ** b) % 2) { const bit = i * width + b; buf[bit >> 3] |= 1 << (bit & 7); }
  });
  return buf;
}

export function treeShape(symbols) {
  const n = symbols.length;
  const parent = new Array(n).fill(-1);
  const side = new Array(n).fill(0);
  let next = 1;
  for (let i = 0; i < n; i++) {
    if (symbols[i] === 3) continue;
    parent[next] = i; side[next] = 0;
    parent[next + 1] = i; side[next + 1] = 1;
    next += 2;
  }
  const path = i => { const steps = []; for (let j = i; parent[j] >= 0; j = parent[j]) steps.unshift(side[j]); return steps; };
  return { parent, path, maxDepth: Math.max(...Array.from({ length: n }, (_, i) => path(i).length)) };
}

// Constraint: a slot path is one sentinel bit at pathBits-1-depth with the inverted turns (root first) in the bits above it, measured on 321,002 slots.
export function slotPathBits(symbols) {
  const shape = treeShape(symbols);
  const pathBits = shape.maxDepth + 1;
  const values = Array.from(symbols, (_, i) => {
    const steps = shape.path(i);
    let v = 2 ** (pathBits - 1 - steps.length);
    steps.forEach((s, k) => { if (!s) v += 2 ** (pathBits - steps.length + k); });
    return v;
  });
  return { pathBits, bytes: writePacked(values, pathBits, slotPathBytes(symbols.length, pathBits)) };
}

export function decodeHeader(model) {
  const n = model.slots.length;
  const nobj = model.objectBoxes.length / 24;
  const pairs = (list, w0, w1) => readPacked(list.bytes, list.count, w0 + w1).map(v => [v % 2 ** w0, Math.floor(v / 2 ** w0)]);
  return {
    banner: model.banner, lodBase: model.lodBase, reserved10: model.reserved10, world: model.world, widths: model.widths,
    topKd: { symbols: decodeSymbols(model.topKd.nodes, n), splits: f32s(model.topKd.splits, model.topKd.splits.length / 4) },
    lod: f32s(model.lod, n),
    objects: {
      boxes: Array.from({ length: nobj }, (_, i) => f32s(model.objectBoxes.subarray(24 * i, 24 * i + 24), 6)),
      distances: model.objectDistances ? Array.from({ length: nobj }, (_, i) => f32s(model.objectDistances.subarray(32 * i, 32 * i + 32), 8)) : null,
      ids: u32s(model.objectIds, nobj),
    },
    runA: pairs(model.runListA, model.widths[0], model.widths[1]), runB: pairs(model.runListB, model.widths[2], model.widths[3]),
    clusters: Array.from({ length: model.clusterNodes.length / 20 }, (_, i) => {
      const o = 20 * i;
      const q = k => model.clusterNodes.readUInt16LE(o + 8 + 2 * k);
      return { portalStart: model.clusterNodes.readUInt32LE(o), portalCount: model.clusterNodes.readUInt32LE(o + 4), x: [q(0), q(5)], y: [q(1), q(2)], z: [q(3), q(4)] };
    }),
    clusterPortals: model.clusterPortals,
    gates: { count: model.gates.count, records: Array.from({ length: model.gates.records.length / 12 }, (_, i) => u32s(model.gates.records.subarray(12 * i, 12 * i + 12), 3)), lists: model.gates.lists, trailer: model.gates.trailer },
    child: {
      trees: model.child.trees.map(t => (t ? decodeKd(t) : null)),
      rows: model.child.rows,
    },
  };
}

export function encodeHeader(h, model) {
  const n = model.slots.length;
  const symbols = h.topKd.symbols;
  const slots = model.slots.map((tile, i) => (tile ? { ...tile, leaf: symbols[i] === 3 } : null));
  const paths = slotPathBits(symbols);
  const cellStarts = Buffer.alloc(4 * (n + 1));
  let run = 0;
  slots.forEach((tile, i) => { cellStarts.writeUInt32LE(run, 4 * i); run += tile ? tile.cells.length / 36 : 0; });
  cellStarts.writeUInt32LE(run, 4 * n);
  const lod = Buffer.alloc(4 * n);
  h.lod.forEach((v, i) => lod.writeFloatLE(slots[i] ? v : -1, 4 * i));
  const splits = Buffer.alloc(4 * h.topKd.splits.length);
  h.topKd.splits.forEach((v, i) => splits.writeFloatLE(v, 4 * i));
  const boxes = Buffer.alloc(24 * h.objects.boxes.length);
  h.objects.boxes.forEach((b, i) => b.forEach((v, k) => boxes.writeFloatLE(v, 24 * i + 4 * k)));
  let distances = null;
  if (h.objects.distances) {
    distances = Buffer.alloc(32 * h.objects.distances.length);
    h.objects.distances.forEach((d, i) => d.forEach((v, k) => distances.writeFloatLE(v, 32 * i + 4 * k)));
  }
  const ids = Buffer.alloc(4 * h.objects.ids.length);
  h.objects.ids.forEach((v, i) => ids.writeUInt32LE(v, 4 * i));
  const list = (entries, w0, w1) => ({
    count: entries.length,
    bytes: writePacked(entries.map(([a, b]) => a + b * 2 ** w0), w0 + w1, Math.ceil(entries.length * (w0 + w1) / 8)),
  });
  const clusterNodes = Buffer.alloc(20 * h.clusters.length);
  h.clusters.forEach((c, i) => {
    const o = 20 * i;
    clusterNodes.writeUInt32LE(c.portalStart, o);
    clusterNodes.writeUInt32LE(c.portalCount, o + 4);
    [c.x[0], c.y[0], c.y[1], c.z[0], c.z[1], c.x[1]].forEach((v, k) => clusterNodes.writeUInt16LE(v, o + 8 + 2 * k));
  });
  const records = Buffer.alloc(12 * h.gates.records.length);
  h.gates.records.forEach((r, i) => r.forEach((v, k) => records.writeUInt32LE(v, 12 * i + 4 * k)));
  return {
    ...model, banner: h.banner, lodBase: h.lodBase, reserved10: h.reserved10, world: h.world, widths: h.widths,
    pathBits: paths.pathBits, slotPaths: paths.bytes, cellStarts, lod, slots,
    topKd: { nodes: encodeNodes(symbols), splits },
    objectBoxes: boxes, objectDistances: distances, objectIds: ids,
    runListA: list(h.runA, h.widths[0], h.widths[1]), runListB: list(h.runB, h.widths[2], h.widths[3]),
    clusterNodes, clusterPortals: h.clusterPortals,
    gates: { count: h.gates.count, records, lists: h.gates.lists, trailer: h.gates.trailer },
    child: { trees: h.child.trees.map(t => (t ? encodeKd(t) : null)), rows: h.child.rows },
  };
}

export const ENFORCED_HEADER = [
  'top kd: node symbols consume exactly n nodes', 'top kd: leaf <=> FLT_MAX cut', 'top kd: tile box == node region (bit-exact)',
  'top kd: tile leaf flag <=> kd leaf node', 'top kd: every kd leaf slot holds a tile', 'top kd: nodes re-encode from symbols',
  'slot paths: re-derived from the tree == stored', 'cell starts: re-derived from the tiles == stored', 'lod: -1 iff empty slot',
  'lod: finite and positive for tiles', 'header: lod base 512 == banner T', 'header: world box finite and ordered', 'header: reserved word is zero',
  'objects: boxes finite and ordered', 'objects: ids distinct', 'objects: distance padding words (w == 0, last == FLT_MAX or 2^24)', 'clusters: portal starts chain', 'clusters: counts sum to the portal region',
  'clusters: aabb ordered (lo <= hi)', 'gates: count > 0 iff records and trailer exist', 'child: tree node symbols consume exactly n nodes',
  'child: tree node region re-encodes from symbols', 'child: leaf rows == leaf tiles',
];

export function validateHeader(model, h, claims) {
  const n = model.slots.length;
  const sym = h.topKd.symbols;
  const where = 'header';
  const walk = walkKd(sym, h.topKd.splits, h.world.min, h.world.max, { implicitMidpoint: false });
  claims.check('top kd: node symbols consume exactly n nodes', walk.ok && sym.filter(s => s === 3).length === (n + 1) / 2, where);
  claims.check('top kd: leaf <=> FLT_MAX cut', sym.every((s, i) => (s === 3) === (h.topKd.splits[i] === FLT_MAX)) && h.topKd.splits.length === n, where);
  claims.check('top kd: nodes re-encode from symbols', encodeNodes(sym).equals(model.topKd.nodes), where);
  let leafOk = true;
  model.slots.forEach((tile, i) => {
    if (!tile) return;
    const r = walk.regions[i];
    const same = r && [0, 1, 2].every(a => tile.box[a] === r.lo[a] && tile.box[3 + a] === r.hi[a]);
    claims.check('top kd: tile box == node region (bit-exact)', same, `slot ${i}`);
    claims.check('top kd: tile leaf flag <=> kd leaf node', tile.leaf === (sym[i] === 3), `slot ${i}`);
    claims.check('lod: finite and positive for tiles', Number.isFinite(h.lod[i]) && h.lod[i] > 0, `slot ${i}`);
  });
  sym.forEach((s, i) => { if (s === 3 && !model.slots[i]) leafOk = false; });
  claims.check('top kd: every kd leaf slot holds a tile', leafOk, where);
  claims.check('lod: -1 iff empty slot', h.lod.every((v, i) => (v === -1) === (model.slots[i] === null)), where);
  const paths = slotPathBits(sym);
  claims.check('slot paths: re-derived from the tree == stored', paths.pathBits === model.pathBits && paths.bytes.equals(model.slotPaths), where);
  let run = 0;
  let starts = true;
  model.slots.forEach((tile, i) => { if (model.cellStarts.readUInt32LE(4 * i) !== run) starts = false; run += tile ? tile.cells.length / 36 : 0; });
  claims.check('cell starts: re-derived from the tiles == stored', starts && model.cellStarts.readUInt32LE(4 * n) === run, where);
  claims.check('header: lod base 512 == banner T', h.lodBase === 512 && h.banner.startsWith('T 512.0 '), where);
  claims.check('header: world box finite and ordered', [0, 1, 2].every(a => Number.isFinite(h.world.min[a]) && Number.isFinite(h.world.max[a]) && h.world.min[a] <= h.world.max[a]), where);
  claims.check('header: reserved word is zero', h.reserved10 === 0, where);
  claims.check('objects: boxes finite and ordered', h.objects.boxes.every(b => b.every(Number.isFinite) && [0, 1, 2].every(a => b[a] <= b[3 + a])), where);
  claims.check('objects: ids distinct', new Set(h.objects.ids).size === h.objects.ids.length, where);
  if (h.objects.distances) {
    h.objects.distances.forEach((d, i) => {
      const b = h.objects.boxes[i];
      claims.check('objects: distance record xyz == box', [0, 1, 2].every(a => d[a] === b[a] && d[4 + a] === b[3 + a]), `object ${i}`);
      claims.check('objects: distance padding words (w == 0, last == FLT_MAX or 2^24)', d[3] === 0 && (d[7] === FLT_MAX || d[7] === 16777216), `object ${i}`);
    });
  }
  let cursor = 0;
  let chain = true;
  let ordered = true;
  for (const c of h.clusters) {
    if (c.portalStart !== cursor) chain = false;
    cursor += c.portalCount;
    if (!(c.x[0] <= c.x[1] && c.y[0] <= c.y[1] && c.z[0] <= c.z[1])) ordered = false;
  }
  claims.check('clusters: portal starts chain', chain, where);
  claims.check('clusters: counts sum to the portal region', cursor * 16 === h.clusterPortals.length, where);
  claims.check('clusters: aabb ordered (lo <= hi)', ordered, where);
  claims.check('gates: count > 0 iff records and trailer exist', (h.gates.count > 0) === (h.gates.records.length > 0 && h.gates.trailer.length > 0), where);
  h.child.trees.forEach((t, i) => {
    if (!t) return;
    claims.check('child: tree node symbols consume exactly n nodes', walkKd(t.symbols, [], [0, 0, 0], [1, 1, 1]).ok && t.symbols.filter(s => s === 3).length === (t.nodeCount + 1) / 2, `tree ${i}`);
    claims.check('child: tree node region re-encodes from symbols', encodeNodes(t.symbols).equals(model.child.trees[i].nodes), `tree ${i}`);
  });
  claims.check('child: leaf rows == leaf tiles', h.child.rows.length === model.slots.filter(t => t && t.leaf).length, where);
}
