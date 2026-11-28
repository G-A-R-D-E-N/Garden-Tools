// Constraint: parse and write share these size rules; every offset, count and size in a written tome is derived from the model, never copied from the input.
import { MAGIC, crc32c } from './uvd_tome.mjs';

export const HEADER_BYTES = 0x150;
export const TILE_HEADER_BYTES = 0x50;
export const BANNER_AT = 0xbc;

export const align16 = n => (n + 15) & ~15;
const align4 = n => (n + 3) & ~3;

// Constraint: the kd node region is 2 bits per node, then a u16 rank per 256 nodes, then one u8 rank per 16 nodes except every sixteenth (measured on 189,036 trees).
export function nodeRegionBytes(n) {
  return align16(align4(Math.ceil(2 * n / 8)) + align4(2 * Math.floor(n / 256)) + Math.floor(n / 16) - Math.floor(n / 256));
}
export const leafMapBytes = (nodeCount, mapWidth) => Math.ceil(((nodeCount + 1) / 2) * mapWidth / 8);
export const slotPathBytes = (slots, pathBits) => Math.ceil(slots * pathBits / 8);
export const runListBytes = (count, bits) => Math.ceil(count * bits / 8);

export class TomeLayoutError extends Error {
  name = 'TomeLayoutError';
}
const fail = msg => { throw new TomeLayoutError(msg); };
const hex = n => '0x' + n.toString(16);

class Reader {
  constructor(bytes, base, limit, label) { this.bytes = bytes; this.base = base; this.limit = limit; this.label = label; this.pos = 0; }
  u32(o) { return this.bytes.readUInt32LE(this.base + o); }
  u16(o) { return this.bytes.readUInt16LE(this.base + o); }
  f32(o) { return this.bytes.readFloatLE(this.base + o); }
  // Constraint: a region starts exactly where the previous one ended, an absent region has offset 0, and every byte between a payload and its 16-byte boundary is zero.
  take(name, off, len, { whole = false } = {}) {
    if (len === 0) {
      if (off !== 0) fail(`${this.label}${name}: offset ${hex(off)} for an empty region`);
      return Buffer.alloc(0);
    }
    if (off !== this.pos) fail(`${this.label}${name}: at ${hex(off)}, expected ${hex(this.pos)}`);
    const end = align16(off + len);
    if (this.base + end > this.limit) fail(`${this.label}${name}: ${hex(end)} runs past the end`);
    if (!whole) {
      for (let p = this.base + off + len; p < this.base + end; p++) if (this.bytes[p] !== 0) fail(`${this.label}${name}: nonzero padding at ${hex(p - this.base)}`);
    }
    this.pos = whole ? off + len : end;
    return Buffer.from(this.bytes.subarray(this.base + off, this.base + off + len));
  }
  zero(name, off, len) {
    for (let p = this.base + off; p < this.base + off + len; p++) if (this.bytes[p] !== 0) fail(`${this.label}${name}: nonzero at ${hex(p - this.base)}`);
  }
}

function parseKd(r, word, nodeOff, leafMapOff, splitCount, splitOff, prefix) {
  const nodeCount = word >>> 5;
  const mapWidth = word & 31;
  if (nodeCount % 2 === 0) fail(`${prefix}: even node count ${nodeCount}`);
  return {
    nodeCount, mapWidth,
    nodes: r.take(prefix + '.nodes', nodeOff, nodeRegionBytes(nodeCount)),
    splits: r.take(prefix + '.splits', splitOff, 4 * splitCount),
    leafMap: r.take(prefix + '.leafMap', leafMapOff, mapWidth ? leafMapBytes(nodeCount, mapWidth) : 0),
  };
}

function parseTile(bytes, at, limit, slot) {
  const r = new Reader(bytes, at, limit, `tile slot ${slot}: `);
  const word = r.u32(0x2c);
  const size = word >>> 8;
  const leaf = (word & 1) === 1;
  if (size < TILE_HEADER_BYTES || size % 16 !== 0) fail(`${r.label}size ${size}`);
  if (at + size > limit) fail(`${r.label}size ${size} runs past the end`);
  r.limit = at + size;
  const cellCount = r.u16(0x34);
  if (r.u32(0x38) !== TILE_HEADER_BYTES) fail(`${r.label}cells at ${hex(r.u32(0x38))}`);
  r.pos = TILE_HEADER_BYTES;
  const tile = {
    box: [0, 4, 8, 12, 16, 20].map(o => r.f32(o)),
    leaf, flags: (word & 0xff) >>> 1,
    expand: r.f32(0x30), clusterCount: r.u16(0x36),
    kd: null, planes: Buffer.alloc(0), bsp: Buffer.alloc(0),
  };
  tile.cells = r.take('cells', TILE_HEADER_BYTES, 36 * cellCount);
  const planeCount = r.u32(0x4c);
  const bspCount = r.u32(0x44);
  const nextAfterPortals = r.u32(0x48) || (leaf ? r.u32(0x1c) : size);
  const portalOff = r.u32(0x3c);
  if (portalOff !== r.pos) fail(`${r.label}portals at ${hex(portalOff)}, expected ${hex(r.pos)}`);
  tile.portals = r.take('portals', portalOff, nextAfterPortals - portalOff, { whole: true });
  tile.planes = r.take('planes', r.u32(0x48), 16 * planeCount);
  if (leaf) {
    tile.kd = parseKd(r, r.u32(0x18), r.u32(0x1c), r.u32(0x20), r.u32(0x24), r.u32(0x28), r.label + 'kd');
  } else {
    r.zero('kd descriptor', 0x18, 0x14);
  }
  tile.bsp = r.take('bsp', r.u32(0x40), 8 * bspCount);
  if (r.pos !== size) fail(`${r.label}regions end at ${hex(r.pos)}, size field ${hex(size)}`);
  return tile;
}

export function parse(bytes) {
  if (bytes.length < HEADER_BYTES) fail('shorter than the 0x150-byte header');
  const r = new Reader(bytes, 0, bytes.length, '');
  if (r.u32(0) !== MAGIC) fail('bad magic');
  if (r.u32(8) !== bytes.length) fail(`size field ${r.u32(8)} != ${bytes.length}`);
  if (bytes.length % 16 !== 0) fail('size is not 16-aligned');
  if (crc32c(bytes.subarray(8)) !== r.u32(4)) fail('crc mismatch');
  const bannerEnd = bytes.indexOf(0, BANNER_AT);
  if (bannerEnd < 0 || bannerEnd >= HEADER_BYTES) fail('banner has no NUL inside the header');
  r.zero('after banner', bannerEnd, HEADER_BYTES - bannerEnd);
  r.zero('reserved 0xb0', 0xb0, BANNER_AT - 0xb0);
  r.zero('reserved 0x34', 0x34, 4);
  r.zero('reserved 0x4c', 0x4c, 4);

  const n = r.u32(0x90);
  if (n === 0) fail('no tile slots');
  if (r.u32(0x2c) >>> 5 !== n) fail('top kd node count differs from the slot count');
  const model = {
    banner: bytes.toString('latin1', BANNER_AT, bannerEnd),
    lodBase: r.f32(0x0c), reserved10: r.u32(0x10),
    world: { min: [0x14, 0x18, 0x1c].map(o => r.f32(o)), max: [0x20, 0x24, 0x28].map(o => r.f32(o)) },
    pathBits: r.u32(0x94),
  };
  r.pos = HEADER_BYTES;
  if (r.u32(0x2c) & 31) fail('top kd tree with a leaf map is not modelled');
  model.topKd = {
    nodes: r.take('topKd.nodes', r.u32(0x30), nodeRegionBytes(n)),
    splits: r.take('topKd.splits', r.u32(0x3c), 4 * r.u32(0x38)),
  };
  model.slotPaths = r.take('slotPaths', r.u32(0x98), slotPathBytes(n, model.pathBits));
  model.lod = r.take('lod', r.u32(0x9c), 4 * n);
  model.cellStarts = r.take('cellStarts', r.u32(0x88), 4 * (n + 1));

  const widthWord = r.u32(0x54);
  if (widthWord >>> 20) fail('list widths use more than 20 bits');
  model.widths = [0, 5, 10, 15].map(s => (widthWord >>> s) & 31);
  const objects = r.u32(0x40);
  model.runListA = { count: r.u32(0x5c), bytes: r.take('runListA', r.u32(0x58), runListBytes(r.u32(0x5c), model.widths[0] + model.widths[1])) };
  model.runListB = { count: r.u32(0x64), bytes: r.take('runListB', r.u32(0x60), runListBytes(r.u32(0x64), model.widths[2] + model.widths[3])) };
  model.clusterNodes = r.take('clusterNodes', r.u32(0x80), 20 * r.u32(0x7c));
  const gateCount = r.u32(0x68);
  const afterPortals = gateCount ? r.u32(0x70) : objects ? r.u32(0x44) : r.u32(0xa0);
  model.clusterPortals = r.take('clusterPortals', r.u32(0x84), r.u32(0x84) ? afterPortals - r.u32(0x84) : 0, { whole: true });
  model.gates = { count: gateCount };
  model.gates.records = r.take('gates.records', r.u32(0x70), 12 * r.u32(0x74));
  const afterGates = objects ? r.u32(0x44) : r.u32(0xa0);
  model.gates.lists = r.take('gates.lists', r.u32(0x78), r.u32(0x78) ? afterGates - r.u32(0x78) : 0, { whole: true });
  model.objectBoxes = r.take('objectBoxes', r.u32(0x44), 24 * objects);
  model.objectDistances = r.u32(0x48) ? r.take('objectDistances', r.u32(0x48), 32 * objects) : null;
  model.objectIds = r.take('objectIds', r.u32(0x50), 4 * objects);

  const tableOff = r.u32(0xa0);
  r.take('tileTable', tableOff, 4 * n);
  model.slots = [];
  let leafTiles = 0;
  for (let s = 0; s < n; s++) {
    const at = bytes.readUInt32LE(tableOff + 4 * s);
    if (at === 0) { model.slots.push(null); continue; }
    if (at !== r.pos) fail(`tile slot ${s}: at ${hex(at)}, expected ${hex(r.pos)}`);
    const tile = parseTile(bytes, at, bytes.length, s);
    if (tile.leaf) leafTiles++;
    model.slots.push(tile);
    r.pos = at + (bytes.readUInt32LE(at + 0x2c) >>> 8);
  }
  if (leafTiles !== r.u32(0x8c)) fail(`leaf tile count ${r.u32(0x8c)} != tiles with the leaf flag ${leafTiles}`);
  const lmOff = r.u32(0xa4);
  model.gates.trailer = r.take('gates.trailer', r.u32(0x6c), gateCount ? lmOff - r.u32(0x6c) : 0, { whole: true });
  const rowBytes = r.take('leafMatch', lmOff, 16 * leafTiles);
  parseChild(model, bytes, r, rowBytes);
  return model;
}

function parseChild(model, bytes, r, rowBytes) {
  const childOff = r.u32(0xa8);
  const count = r.u32(0xac);
  if (childOff !== r.pos) fail(`child block at ${hex(childOff)}, expected ${hex(r.pos)}`);
  const bodyStart = align16(childOff + 20 * count);
  if (bodyStart > bytes.length) fail('child descriptors run past the end');
  const trees = [];
  const starts = [];
  for (let i = 0; i < count; i++) {
    const d = childOff + 20 * i;
    const [word, nodeOff, leafOff, splitCount, splitOff] = [0, 4, 8, 12, 16].map(o => bytes.readUInt32LE(d + o));
    if (word === 0) {
      if (nodeOff || leafOff || splitCount || splitOff) fail(`child descriptor ${i}: empty tree with offsets`);
      trees.push(null);
      continue;
    }
    starts.push({ at: nodeOff, kind: 'T', i });
    trees.push({ word, nodeOff, leafOff, splitCount, splitOff });
  }
  const rows = [];
  for (let k = 0; k < rowBytes.length / 16; k++) {
    const w = [0, 4, 8, 12].map(o => rowBytes.readUInt32LE(16 * k + o));
    rows.push({ words: [w[0], w[2], w[3]], block: null, at: w[1] });
    if (w[1]) starts.push({ at: w[1], kind: 'B', i: k });
  }
  starts.sort((a, b) => a.at - b.at);
  const rd = new Reader(bytes, 0, bytes.length, 'child: ');
  rd.pos = bodyStart;
  starts.forEach((s, idx) => {
    if (s.kind === 'T') {
      const t = trees[s.i];
      trees[s.i] = parseKd(rd, t.word, t.nodeOff, t.leafOff, t.splitCount, t.splitOff, `tree ${s.i}`);
      return;
    }
    const next = starts[idx + 1]?.at ?? bytes.length;
    rows[s.i].block = rd.take(`block ${s.i}`, s.at, next - s.at, { whole: true });
  });
  if (rd.pos !== bytes.length) fail(`child body ends at ${hex(rd.pos)}, file ends at ${hex(bytes.length)}`);
  rows.forEach(row => { row.treesBefore = 0; });
  let order = 0;
  const treeIdx = trees.map((t, i) => (t ? i : -1)).filter(i => i >= 0);
  for (const s of starts) {
    if (s.kind === 'T') { order++; continue; }
    const nextTree = treeIdx[order];
    rows[s.i].treesBefore = nextTree === undefined ? count : nextTree;
  }
  model.child = { trees, rows: rows.map(({ words, block, treesBefore }) => ({ words, block, treesBefore })) };
}

function writeKd(kd, rel) {
  const parts = [];
  let pos = rel;
  const put = buf => { if (!buf.length) return 0; const at = pos; parts.push([at, buf]); pos = align16(at + buf.length); return at; };
  const nodeOff = put(kd.nodes);
  const splitOff = put(kd.splits);
  const leafOff = put(kd.leafMap);
  return { parts, end: pos, nodeOff, splitOff, leafOff, splitCount: kd.splits.length / 4, word: (kd.nodeCount << 5 | kd.mapWidth) >>> 0 };
}

function writeTile(tile) {
  const parts = [];
  let pos = TILE_HEADER_BYTES;
  const put = (buf, whole = false) => {
    if (!buf.length) return 0;
    const at = pos; parts.push([at, buf]); pos = whole ? at + buf.length : align16(at + buf.length); return at;
  };
  const head = Buffer.alloc(TILE_HEADER_BYTES);
  tile.box.forEach((v, i) => head.writeFloatLE(v, 4 * i));
  put(tile.cells);
  pos = align16(TILE_HEADER_BYTES + tile.cells.length);
  head.writeUInt32LE(TILE_HEADER_BYTES, 0x38);
  head.writeUInt32LE(put(tile.portals, true), 0x3c);
  head.writeUInt32LE(put(tile.planes), 0x48);
  head.writeUInt32LE(tile.planes.length / 16, 0x4c);
  if (tile.leaf) {
    const kd = writeKd(tile.kd, pos);
    parts.push(...kd.parts);
    pos = kd.end;
    head.writeUInt32LE(kd.word, 0x18);
    head.writeUInt32LE(kd.nodeOff, 0x1c);
    head.writeUInt32LE(kd.leafOff, 0x20);
    head.writeUInt32LE(kd.splitCount, 0x24);
    head.writeUInt32LE(kd.splitOff, 0x28);
  }
  head.writeUInt32LE(put(tile.bsp), 0x40);
  head.writeUInt32LE(tile.bsp.length / 8, 0x44);
  head.writeUInt32LE((pos << 8 | tile.flags << 1 | (tile.leaf ? 1 : 0)) >>> 0, 0x2c);
  head.writeFloatLE(tile.expand, 0x30);
  head.writeUInt16LE(tile.cells.length / 36, 0x34);
  head.writeUInt16LE(tile.clusterCount, 0x36);
  const out = Buffer.alloc(pos);
  head.copy(out, 0);
  for (const [at, buf] of parts) buf.copy(out, at);
  return out;
}

export function write(model) {
  const n = model.slots.length;
  const h = Buffer.alloc(HEADER_BYTES);
  const w32 = (o, v) => h.writeUInt32LE(v >>> 0, o);
  const parts = [];
  let pos = HEADER_BYTES;
  const put = (buf, hdr, whole = false) => {
    if (!buf || !buf.length) { if (hdr !== undefined) w32(hdr, 0); return 0; }
    const at = pos;
    parts.push([at, buf]);
    pos = whole ? at + buf.length : align16(at + buf.length);
    if (hdr !== undefined) w32(hdr, at);
    return at;
  };
  const same = (label, got, want) => { if (got !== want) throw new TomeLayoutError(`model: ${label} is ${got}, expected ${want}`); };
  h.writeUInt32LE(MAGIC, 0);
  h.writeFloatLE(model.lodBase, 0x0c);
  w32(0x10, model.reserved10);
  model.world.min.forEach((v, i) => h.writeFloatLE(v, 0x14 + 4 * i));
  model.world.max.forEach((v, i) => h.writeFloatLE(v, 0x20 + 4 * i));
  h.write(model.banner, BANNER_AT, 'latin1');
  w32(0x2c, n << 5);
  w32(0x90, n);
  w32(0x94, model.pathBits);
  put(model.topKd.nodes, 0x30);
  w32(0x38, model.topKd.splits.length / 4);
  put(model.topKd.splits, 0x3c);
  same('slotPaths bytes', model.slotPaths.length, slotPathBytes(n, model.pathBits));
  put(model.slotPaths, 0x98);
  same('lod bytes', model.lod.length, 4 * n);
  put(model.lod, 0x9c);
  same('cellStarts bytes', model.cellStarts.length, 4 * (n + 1));
  put(model.cellStarts, 0x88);
  w32(0x54, model.widths.reduce((acc, v, i) => acc | v << 5 * i, 0));
  same('runListA bytes', model.runListA.bytes.length, runListBytes(model.runListA.count, model.widths[0] + model.widths[1]));
  w32(0x5c, model.runListA.count);
  put(model.runListA.bytes, 0x58);
  same('runListB bytes', model.runListB.bytes.length, runListBytes(model.runListB.count, model.widths[2] + model.widths[3]));
  w32(0x64, model.runListB.count);
  put(model.runListB.bytes, 0x60);
  w32(0x7c, model.clusterNodes.length / 20);
  put(model.clusterNodes, 0x80);
  put(model.clusterPortals, 0x84, true);
  w32(0x68, model.gates.count);
  w32(0x74, model.gates.records.length / 12);
  put(model.gates.records, 0x70);
  put(model.gates.lists, 0x78, true);
  const objects = model.objectBoxes.length / 24;
  w32(0x40, objects);
  put(model.objectBoxes, 0x44);
  if (model.objectDistances) same('objectDistances bytes', model.objectDistances.length, 32 * objects);
  put(model.objectDistances, 0x48);
  same('objectIds bytes', model.objectIds.length, 4 * objects);
  put(model.objectIds, 0x50);

  const tableAt = pos;
  w32(0xa0, tableAt);
  pos = align16(tableAt + 4 * n);
  const table = Buffer.alloc(4 * n);
  let leafTiles = 0;
  model.slots.forEach((tile, s) => {
    if (!tile) return;
    const buf = writeTile(tile);
    table.writeUInt32LE(pos, 4 * s);
    parts.push([pos, buf]);
    pos += buf.length;
    if (tile.leaf) leafTiles++;
  });
  parts.push([tableAt, table]);
  w32(0x8c, leafTiles);
  w32(0x6c, model.gates.trailer.length ? pos : 0);
  put(model.gates.trailer, undefined, true);
  pos = align16(pos);

  same('leaf match rows', model.child.rows.length, leafTiles);
  const rowsAt = pos;
  w32(0xa4, rowsAt);
  const childAt = align16(rowsAt + 16 * leafTiles);
  w32(0xa8, childAt);
  const count = model.child.trees.length;
  w32(0xac, count);
  const desc = Buffer.alloc(20 * count);
  const body = [];
  let cur = align16(childAt + 20 * count);
  const emitTree = i => {
    const t = model.child.trees[i];
    if (!t) return;
    const kd = writeKd(t, cur);
    body.push(...kd.parts);
    cur = kd.end;
    [kd.word, kd.nodeOff, kd.leafOff, kd.splitCount, kd.splitOff].forEach((v, k) => desc.writeUInt32LE(v >>> 0, 20 * i + 4 * k));
  };
  const rowBuf = Buffer.alloc(16 * leafTiles);
  let nextTree = 0;
  model.child.rows.forEach((row, k) => {
    if (row.block) {
      while (nextTree < row.treesBefore) emitTree(nextTree++);
      body.push([cur, row.block]);
      rowBuf.writeUInt32LE(cur, 16 * k + 4);
      cur += row.block.length;
    }
    row.words.forEach((v, j) => rowBuf.writeUInt32LE(v >>> 0, 16 * k + [0, 8, 12][j]));
  });
  while (nextTree < count) emitTree(nextTree++);
  parts.push([rowsAt, rowBuf], [childAt, desc], ...body);
  const size = align16(cur);
  const out = Buffer.alloc(size);
  h.copy(out, 0);
  for (const [at, buf] of parts) buf.copy(out, at);
  out.writeUInt32LE(size, 8);
  out.writeUInt32LE(crc32c(out.subarray(8)), 4);
  return out;
}
