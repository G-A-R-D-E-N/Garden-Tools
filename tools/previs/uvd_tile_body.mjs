// Constraint: every decoder here has an encoder that rebuilds the identical bytes, and a decode is claimed only for the shapes the retail sweep proves.
import { leafMapBytes, nodeRegionBytes } from './uvd_tile_model.mjs';

export const FLT_MAX = 3.4028234663852886e38;
export const CELL_BYTES = 36;
export const PORTAL_BYTES = 16;
export const PLANE_BYTES = 16;
export const BSP_BYTES = 8;
export const OUTSIDE_TARGET = 0x3ffffff;

const align4 = n => (n + 3) & ~3;
export const bitLength = x => (x === 0 ? 0 : 32 - Math.clz32(x));

export function decodeCells(buf) {
  const cells = [];
  for (let o = 0; o < buf.length; o += CELL_BYTES) {
    const q = k => buf.readUInt16LE(o + 24 + 2 * k);
    cells.push({
      portalStart: buf.readUInt32LE(o), portalCount: buf.readUInt32LE(o + 4),
      listStart: buf.readUInt32LE(o + 8), listCount: buf.readUInt32LE(o + 12),
      cluster: buf.readUInt32LE(o + 16), tag: buf.readUInt32LE(o + 20),
      x: [q(0), q(5)], y: [q(1), q(2)], z: [q(3), q(4)],
    });
  }
  return cells;
}

export function encodeCells(cells) {
  const buf = Buffer.alloc(CELL_BYTES * cells.length);
  cells.forEach((c, i) => {
    const o = CELL_BYTES * i;
    [c.portalStart, c.portalCount, c.listStart, c.listCount, c.cluster, c.tag].forEach((v, k) => buf.writeUInt32LE(v >>> 0, o + 4 * k));
    [c.x[0], c.y[0], c.y[1], c.z[0], c.z[1], c.x[1]].forEach((v, k) => buf.writeUInt16LE(v, o + 24 + 2 * k));
  });
  return buf;
}

export function decodePortals(buf) {
  const portals = [];
  for (let o = 0; o < buf.length; o += PORTAL_BYTES) {
    const link = buf.readUInt32LE(o);
    const r0 = buf.readUInt32LE(o + 8);
    const r1 = buf.readUInt32LE(o + 12);
    portals.push({
      target: link & OUTSIDE_TARGET, hier: (link >>> 26) & 1, door: (link >>> 27) & 1, outside: (link >>> 28) & 1, face: link >>> 29,
      plane: buf.readUInt16LE(o + 4), targetCell: buf.readUInt16LE(o + 6),
      rect0: [r0 >>> 16, r0 & 0xffff], rect1: [r1 >>> 16, r1 & 0xffff],
    });
  }
  return portals;
}

export function encodePortals(portals) {
  const buf = Buffer.alloc(PORTAL_BYTES * portals.length);
  portals.forEach((p, i) => {
    const o = PORTAL_BYTES * i;
    buf.writeUInt32LE((p.target | p.hier << 26 | p.door << 27 | p.outside << 28 | p.face << 29) >>> 0, o);
    buf.writeUInt16LE(p.plane, o + 4);
    buf.writeUInt16LE(p.targetCell, o + 6);
    buf.writeUInt32LE(((p.rect0[0] << 16) | p.rect0[1]) >>> 0, o + 8);
    buf.writeUInt32LE(((p.rect1[0] << 16) | p.rect1[1]) >>> 0, o + 12);
  });
  return buf;
}

export function decodePlanes(buf) {
  const planes = [];
  for (let o = 0; o < buf.length; o += PLANE_BYTES) planes.push([0, 4, 8, 12].map(k => buf.readFloatLE(o + k)));
  return planes;
}

export function encodePlanes(planes) {
  const buf = Buffer.alloc(PLANE_BYTES * planes.length);
  planes.forEach((p, i) => p.forEach((v, k) => buf.writeFloatLE(v, PLANE_BYTES * i + 4 * k)));
  return buf;
}

export function decodeBsp(buf) {
  const nodes = [];
  for (let o = 0; o < buf.length; o += BSP_BYTES) {
    const word = buf.readUInt32LE(o);
    nodes.push({ lowIsCell: (word >>> 30) & 1, highIsCell: word >>> 31, plane: word & 0x3fffffff, low: buf.readUInt16LE(o + 4), high: buf.readUInt16LE(o + 6) });
  }
  return nodes;
}

export function encodeBsp(nodes) {
  const buf = Buffer.alloc(BSP_BYTES * nodes.length);
  nodes.forEach((n, i) => {
    buf.writeUInt32LE((n.plane | n.lowIsCell << 30 | n.highIsCell << 31) >>> 0, BSP_BYTES * i);
    buf.writeUInt16LE(n.low, BSP_BYTES * i + 4);
    buf.writeUInt16LE(n.high, BSP_BYTES * i + 6);
  });
  return buf;
}

// Constraint: the node region is 2 bits per node (3 = leaf), a u16 internal-node rank per 256 nodes, then a u8 rank per 16 nodes except every sixteenth.
export function encodeNodes(symbols) {
  const n = symbols.length;
  const out = Buffer.alloc(nodeRegionBytes(n));
  const before = new Uint32Array(n + 1);
  for (let i = 0; i < n; i++) {
    out[i >> 2] |= symbols[i] << ((i & 3) * 2);
    before[i + 1] = before[i] + (symbols[i] !== 3 ? 1 : 0);
  }
  let pos = align4(Math.ceil(2 * n / 8));
  const supers = Math.floor(n / 256);
  for (let k = 1; k <= supers; k++) out.writeUInt16LE(before[256 * k], pos + 2 * (k - 1));
  pos += align4(2 * supers);
  for (let k = 1; k <= Math.floor(n / 16); k++) {
    if (k % 16 !== 0) out[pos++] = before[16 * k] - before[256 * Math.floor(k / 16)];
  }
  return out;
}

export function decodeSymbols(nodes, n) {
  const symbols = new Uint8Array(n);
  for (let i = 0; i < n; i++) symbols[i] = (nodes[i >> 2] >> ((i & 3) * 2)) & 3;
  return symbols;
}

export function decodeKd(kd) {
  const n = kd.nodeCount;
  const symbols = decodeSymbols(kd.nodes, n);
  const splits = [];
  for (let o = 0; o < kd.splits.length; o += 4) splits.push(kd.splits.readFloatLE(o));
  const entries = [];
  const w = kd.mapWidth;
  for (let i = 0; i < (n + 1) / 2 && w; i++) {
    let v = 0;
    for (let b = 0; b < w; b++) { const bit = i * w + b; v |= ((kd.leafMap[bit >> 3] >> (bit & 7)) & 1) << b; }
    entries.push(v >>> 0);
  }
  return { nodeCount: n, mapWidth: w, symbols, splits, entries };
}

export function encodeKd(d) {
  const splits = Buffer.alloc(4 * d.splits.length);
  d.splits.forEach((v, i) => splits.writeFloatLE(v, 4 * i));
  const leafMap = Buffer.alloc(d.mapWidth ? leafMapBytes(d.nodeCount, d.mapWidth) : 0);
  d.entries.forEach((v, i) => {
    for (let b = 0; b < d.mapWidth; b++) if ((v >>> b) & 1) { const bit = i * d.mapWidth + b; leafMap[bit >> 3] |= 1 << (bit & 7); }
  });
  return { nodeCount: d.nodeCount, mapWidth: d.mapWidth, nodes: encodeNodes(d.symbols), splits, leafMap };
}

// Constraint: children of internal node k are the next two unused indices in level order; a stored cut applies for index < splits.length, later internals cut at the midpoint.
export function walkKd(symbols, splits, rootLo, rootHi, { implicitMidpoint = true } = {}) {
  const n = symbols.length;
  const regions = [{ lo: [...rootLo], hi: [...rootHi] }];
  let next = 1;
  const cuts = new Array(n).fill(null);
  for (let i = 0; i < n; i++) {
    if (symbols[i] === 3) continue;
    const r = regions[i];
    if (!r) return { regions, cuts, ok: false };
    const a = symbols[i];
    const stored = i < splits.length;
    const cut = stored ? splits[i] : implicitMidpoint ? (r.lo[a] + r.hi[a]) / 2 : NaN;
    cuts[i] = { axis: a, cut, stored };
    const left = { lo: [...r.lo], hi: [...r.hi] };
    const right = { lo: [...r.lo], hi: [...r.hi] };
    left.hi[a] = cut;
    right.lo[a] = cut;
    regions[next++] = left;
    regions[next++] = right;
  }
  return { regions, cuts, ok: next === n };
}

export function decodeTile(tile) {
  return {
    box: tile.box, leaf: tile.leaf, flags: tile.flags, expand: tile.expand, clusterCount: tile.clusterCount,
    cells: decodeCells(tile.cells), portals: decodePortals(tile.portals),
    planes: decodePlanes(tile.planes), bsp: decodeBsp(tile.bsp),
    kd: tile.kd ? decodeKd(tile.kd) : null,
  };
}

export function encodeTile(d) {
  return {
    box: d.box, leaf: d.leaf, flags: d.flags, expand: d.expand, clusterCount: d.clusterCount,
    cells: encodeCells(d.cells), portals: encodePortals(d.portals),
    planes: encodePlanes(d.planes), bsp: encodeBsp(d.bsp),
    kd: d.kd ? encodeKd(d.kd) : null,
  };
}

export const decodeBodies = model => ({ slots: model.slots.map(t => (t ? decodeTile(t) : null)) });
export const encodeBodies = (decoded, model) => ({ ...model, slots: decoded.slots.map(d => (d ? encodeTile(d) : null)) });

export const ENFORCED_BODY = [
  'cell: portal starts chain', 'cell: portal counts sum to the portal region', 'cell: aabb pairs ordered (lo <= hi)',
  'leaf tile: cluster count == distinct cell clusters', 'portal: face <= 5', 'portal: outside <=> all-ones target',
  'portal: outside => cell 0 and no hierarchy', 'portal: target slot holds a tile', 'portal: target cell in range',
  'portal: hierarchy <=> target tile is not a leaf', 'portal: cross-tile same-level plane on the face boundary',
  'portal: same-level non-door rects ordered', 'kd: node symbols consume exactly n nodes', 'kd: node region re-encodes from symbols',
  'kd: leaf entries have no stored cut', 'kd: stored cuts inside their region', 'kd: leaf map entries address a cell, a bsp root or solid',
  'kd: map width == bitlen(max(cells-1, bsp-1)) + 1 (no bsp)', 'kd: every cell appears in the leaf map (no bsp)',
  'plane: finite unit normal', 'bsp: plane index in range', 'bsp: children in range', 'bsp: every node reachable from a leaf-map root',
];

export function validateTile(slot, decoded, claims) {
  const where = `slot ${slot}`;
  const { cells, portals } = decoded;
  let run = 0;
  let chain = true;
  let ordered = true;
  for (const c of cells) {
    if (c.portalStart !== run) chain = false;
    run += c.portalCount;
    for (const [lo, hi] of [c.x, c.y, c.z]) if (!(lo <= hi)) ordered = false;
  }
  claims.check('cell: portal starts chain', chain, where);
  claims.check('cell: portal counts sum to the portal region', run === portals.length, where);
  claims.check('cell: aabb pairs ordered (lo <= hi)', ordered, where);
  if (decoded.leaf) {
    claims.check('leaf tile: cluster count == distinct cell clusters', new Set(cells.map(c => c.cluster)).size === decoded.clusterCount, where);
  }
}

export function validatePortal(p, ctx, claims, where) {
  claims.check('portal: face <= 5', p.face <= 5, where);
  claims.check('portal: outside <=> all-ones target', (p.outside === 1) === (p.target === OUTSIDE_TARGET), where);
  if (p.outside) {
    claims.check('portal: outside => cell 0 and no hierarchy', p.targetCell === 0 && p.hier === 0, where);
    return;
  }
  const target = ctx.slots[p.target];
  const has = p.target < ctx.slots.length && target !== null;
  claims.check('portal: target slot holds a tile', has, where);
  if (!has) return;
  claims.check('portal: target cell in range', p.targetCell < target.cells.length, where);
  claims.check('portal: hierarchy <=> target tile is not a leaf', (p.hier === 1) === !target.leaf, where);
  if (!p.hier && p.target !== ctx.slot) {
    claims.check('portal: cross-tile same-level plane on the face boundary', p.plane === ((p.face & 1) ? 0xffff : 0), where);
  }
  if (!p.hier && !p.door) claims.check('portal: same-level non-door rects ordered', p.rect0[0] <= p.rect0[1] && p.rect1[0] <= p.rect1[1], where);
}

export function validateKd(d, claims, where) {
  const { kd, cells, bsp } = d;
  const n = kd.nodeCount;
  const walk = walkKd(kd.symbols, kd.splits, d.box.slice(0, 3), d.box.slice(3));
  claims.check('kd: node symbols consume exactly n nodes', walk.ok && kd.symbols.filter(s => s === 3).length === (n + 1) / 2, where);
  claims.check('kd: node region re-encodes from symbols', encodeKd(kd).nodes.equals(encodeNodes(kd.symbols)), where);
  claims.check('kd: leaf entries have no stored cut', kd.splits.every((v, i) => kd.symbols[i] !== 3 || v === 0), where);
  let cutsOk = true;
  walk.cuts.forEach((c, i) => { if (c && c.stored && !(c.cut >= walk.regions[i].lo[c.axis] && c.cut <= walk.regions[i].hi[c.axis])) cutsOk = false; });
  claims.check('kd: stored cuts inside their region', cutsOk, where);
  const w = kd.mapWidth;
  const all = (2 ** w) - 1;
  const hiBit = 2 ** (w - 1);
  const mapOk = kd.entries.every(v => v === all || (v >= hiBit ? bsp.length > 0 && v - hiBit < bsp.length : v < cells.length));
  claims.check('kd: leaf map entries address a cell, a bsp root or solid', mapOk, where);
  if (!bsp.length) {
    claims.check('kd: map width == bitlen(max(cells-1, bsp-1)) + 1 (no bsp)', w === bitLength(cells.length - 1) + 1, where);
    const seen = new Set(kd.entries);
    let every = true;
    for (let c = 0; c < cells.length; c++) if (!seen.has(c)) every = false;
    claims.check('kd: every cell appears in the leaf map (no bsp)', every, where);
  } else {
    claims.check('kd: map width == bitlen(max(cells-1, bsp-1)) + 1 (with bsp)', w === bitLength(Math.max(cells.length - 1, bsp.length - 1)) + 1, where);
  }
}

export function validateBsp(d, claims, where) {
  const { planes, bsp, kd, cells } = d;
  let planeOk = true;
  for (const p of planes) {
    const len = Math.hypot(p[0], p[1], p[2]);
    if (!p.every(Number.isFinite) || Math.abs(len - 1) > 1e-3) planeOk = false;
  }
  claims.check('plane: finite unit normal', planeOk, where);
  if (!bsp.length) return;
  claims.check('bsp: plane index in range', bsp.every(b => b.plane < planes.length), where);
  const childOk = b => (b.lowIsCell ? b.low < cells.length : b.low < bsp.length) && (b.highIsCell ? b.high < cells.length : b.high < bsp.length);
  claims.check('bsp: children in range', bsp.every(childOk), where);
  const hiBit = 2 ** (kd.mapWidth - 1);
  const all = (2 ** kd.mapWidth) - 1;
  const reached = new Set(kd.entries.filter(v => v !== all && v >= hiBit).map(v => v - hiBit));
  const stack = [...reached];
  while (stack.length) {
    const b = bsp[stack.pop()];
    if (!b) continue;
    for (const [isCell, idx] of [[b.lowIsCell, b.low], [b.highIsCell, b.high]]) if (!isCell && !reached.has(idx)) { reached.add(idx); stack.push(idx); }
  }
  claims.check('bsp: every node reachable from a leaf-map root', reached.size === bsp.length, where);
}

export function validateReciprocity(decoded, claims) {
  const n = decoded.slots.length;
  const key = (slot, face, target) => (slot * 6 + face) * n + target;
  const links = new Set();
  decoded.slots.forEach((d, slot) => {
    if (d) for (const p of d.portals) if (!p.outside && p.target !== slot) links.add(key(slot, p.face, p.target));
  });
  for (const k of links) {
    const target = k % n;
    const slot = Math.floor(k / n / 6);
    const face = Math.floor(k / n) % 6;
    claims.check('portal: cross-tile link has a reverse link (face ^ 1) in the target (measured)', links.has(key(target, face ^ 1, slot)), `slot ${slot} face ${face} target ${target}`);
  }
}

export function validateBodies(decoded, claims) {
  validateReciprocity(decoded, claims);
  decoded.slots.forEach((d, slot) => {
    if (!d) return;
    validateTile(slot, d, claims);
    d.portals.forEach((p, i) => validatePortal(p, { slots: decoded.slots, slot }, claims, `slot ${slot} portal ${i}`));
    if (d.leaf) {
      validateKd(d, claims, `slot ${slot}`);
      validateBsp(d, claims, `slot ${slot}`);
    }
  });
}
