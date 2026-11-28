// Constraint: every count and offset in a written tome is computed from the bytes emitted, never copied from vanilla.
import fs from 'node:fs';
import zlib from 'node:zlib';

export const MAGIC = 0xd6000012;
export const BANNER_OFFSET = 0xbc;
export const HEADER_MIN = 0x100;
export const FIRST_SECTION = 0x150;
export const DEFAULT_BANNER = 'T 512.0 SO 128.0 SH 16.000 BF 100 F 0 CS 0.0 - 3.3.17 F 1 0 OG 0';

// Constraint: these page-constant offsets come from vanilla: 33 @payload+0x28, 224 @+0x2c, 240 @+0x30, 80 @+0x40, 128 @+0x44.
export const EMPTY_PAGE_CONSTANTS = { kind: 33, boundA: 224, boundB: 240, scaleA: 80, scaleB: 128 };

export function crc32c(data) {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0x82f63b78 ^ (c >>> 1) : c >>> 1;
    table[i] = c >>> 0;
  }
  let crc = 0xffffffff;
  for (const byte of data) crc = ((crc >>> 8) ^ table[(crc ^ byte) & 0xff]) >>> 0;
  return (crc ^ 0xffffffff) >>> 0;
}

export function ba2Entries(file) {
  const b = fs.readFileSync(file);
  if (b.subarray(0, 4).toString('ascii') !== 'BTDX') throw new Error('not a BA2: ' + file);
  if (b.subarray(8, 12).toString('ascii') !== 'GNRL') throw new Error('not a general archive');
  const count = b.readUInt32LE(12);
  const nameTable = Number(b.readBigUInt64LE(16));
  let pos = 24;
  const entries = [];
  for (let i = 0; i < count; i++) {
    const chunkCount = b[pos + 13];
    pos += 16;
    const chunks = [];
    for (let c = 0; c < chunkCount; c++) {
      chunks.push({ offset: Number(b.readBigUInt64LE(pos)), packed: b.readUInt32LE(pos + 8), unpacked: b.readUInt32LE(pos + 12) });
      pos += 20;
    }
    entries.push({ chunks });
  }
  let at = nameTable;
  for (const entry of entries) {
    const len = b.readUInt16LE(at);
    entry.path = b.subarray(at + 2, at + 2 + len).toString('latin1');
    at += 2 + len;
  }
  return {
    bytes: b,
    entries,
    extract(entry) {
      return Buffer.concat(entry.chunks.map(chunk => {
        const raw = b.subarray(chunk.offset, chunk.offset + (chunk.packed || chunk.unpacked));
        if (chunk.packed === 0 || chunk.packed === chunk.unpacked) return Buffer.from(raw);
        try { return zlib.inflateRawSync(raw); } catch { return zlib.inflateSync(raw); }
      }));
    },
    tomes() { return entries.filter(e => e.path.toLowerCase().endsWith('.uvd')); },
  };
}

// Constraint: parsing must verify magic before any offset is followed.

export function parseTome(bytes) {
  const u32 = o => bytes.readUInt32LE(o);
  const f32 = o => bytes.readFloatLE(o);
  if (u32(0) !== MAGIC) throw new Error('bad magic');
  const bannerEnd = bytes.indexOf(0, BANNER_OFFSET);
  const header = {
    size: u32(8),
    tile: f32(0x0c),
    // Constraint: the world box is three minimums at 0x14 and three maximums at 0x20, as measured on retail tomes.
    world: {
      min: [0x14, 0x18, 0x1c].map(f32),
      max: [0x20, 0x24, 0x28].map(f32),
    },
    counts: { a: u32(0x2c), b: u32(0x38), c: u32(0x7c), d: u32(0x8c), e: u32(0x90), f: u32(0x94), g: u32(0xac) },
    offsets: { s1: u32(0x30), s2: u32(0x3c), s3: u32(0x80), s4: u32(0x88), s5: u32(0x98),
      s6: u32(0x9c), page: u32(0xa0), leaf: u32(0xa4), child: u32(0xa8) },
  };
  const banner = bytes.subarray(BANNER_OFFSET, bannerEnd).toString('latin1');
  const pageEntry = header.offsets.page;
  const pageTarget = u32(pageEntry);
      const page = {
    entry: pageEntry,
    target: pageTarget,
    next: u32(pageTarget),
    // Constraint: retail page bounds are three minimums at the page base then three maximums at +0x0c.
    bounds: [0, 1, 2].flatMap(i => [f32(pageTarget + i * 4), f32(pageTarget + 0x0c + i * 4)]),
    lengthQ8: u32(pageTarget + 0x2c),
    scale: f32(pageTarget + 0x30),
    vertexOffset: u32(pageTarget + 0x38),
    vertexCount: u32(pageTarget + 0x34),
    indexCount: u32(pageTarget + 0x3c),
  };
  const leaf = { at: header.offsets.leaf, count: u32(header.offsets.leaf), rows: [] };
  for (let i = 0; i < leaf.count; i++) {
    const o = leaf.at + 4 + i * 20;
    leaf.rows.push({ kind: u32(o), offsetA: u32(o + 4), offsetB: u32(o + 8) });
  }
  const child = { at: header.offsets.child, records: [] };
  for (let o = child.at; o + 16 <= bytes.length; o += 16) {
    child.records.push({ tag: u32(o), words: [u32(o + 4), u32(o + 8), u32(o + 12)] });
  }
  return { header, banner, page, leaf, child };
}

export function checkTome(bytes, { generated = false } = {}) {
  const issues = [];
  const u32 = o => bytes.readUInt32LE(o);
  if (u32(0) !== MAGIC) issues.push('magic');
  if (u32(8) !== bytes.length) issues.push(`size field ${u32(8)} != ${bytes.length}`);
  if (bytes.length % 16 !== 0) issues.push('not 16-aligned');
  if (crc32c(bytes.subarray(8)) !== u32(4)) issues.push('crc');
  let parsed;
  try { parsed = parseTome(bytes); } catch (e) { issues.push('parse: ' + e.message); return { issues }; }
  for (const [name, at] of Object.entries(parsed.header.offsets)) {
    if (at % 16 !== 0) issues.push(`${name} offset 0x${at.toString(16)} not 16-aligned`);
    if (at >= bytes.length) issues.push(`${name} offset outside file`);
  }
  // Constraint: the 20-byte leaf row and 16-byte child record shapes are this writer's layout, so only assert them on files it wrote.
  if (generated) {
    if (parsed.leaf.at + 4 + parsed.leaf.count * 20 > parsed.header.offsets.child) issues.push('leaf table overruns child block');
    if ((bytes.length - parsed.header.offsets.child) % 16 !== 0) issues.push('child block not a whole number of 16-byte records');
  }
  return { issues, parsed };
}

// Constraint: writing must derive every offset from the emission order.

// Constraint: geometry is quantised to 16-bit per page because vanilla pages hold values inside +-16384 with 0x7FFF as an end marker.
function quantise(value, min, max) {
  const extent = max - min;
  if (extent <= 0) return 0;
  const normalised = ((value - min) / extent) * 2 - 1;
  return Math.max(-16384, Math.min(16384, Math.round(normalised * 16384)));
}

function align16(n) { return (n + 15) & ~15; }

// Constraint: the writer and the page patcher emit a page through this one function, so a rewritten page is the same record a written one is and the header layout cannot drift between the two paths.
export function encodePagePayload(page) {
  {
    const parts = [];
    const head = Buffer.alloc(0x50);
    // Constraint: retail page bounds are three minimums at +0x00 then three maximums at +0x0c.
    for (let i = 0; i < 3; i++) {
      head.writeFloatLE(page.min[i], i * 4);
      head.writeFloatLE(page.max[i], 0x0c + i * 4);
    }
    const extent = Math.max(...page.max.map((v, i) => v - page.min[i]), 1);
    const payloadBytes = align16(0x50 + page.vertices.length * 6 + page.indices.length * 2 + 2);
    // Constraint: the measured page length is the payload size in Q8 at +0x2c, and bit0 marks a page with geometry.
    head.writeUInt32LE(((payloadBytes << 8) | (page.vertices.length ? 1 : 0)) >>> 0, 0x2c);
    head.writeFloatLE(Math.max(1, Math.round(extent / 64)), 0x30);
    head.writeUInt32LE(page.vertices.length, 0x34);
    // Constraint: 0x50 at +0x38 is the only page header word measured constant on every retail page.
    head.writeUInt32LE(0x50, 0x38);
    head.writeUInt32LE(page.indices.length, 0x3c);
    parts.push(head);
    const vertexBytes = Buffer.alloc(page.vertices.length * 6);
    page.vertices.forEach((v, i) => {
      for (let axis = 0; axis < 3; axis++) {
        vertexBytes.writeInt16LE(quantise(v[axis], page.min[axis], page.max[axis]), i * 6 + axis * 2);
      }
    });
    parts.push(vertexBytes);
    const indexBytes = Buffer.alloc(page.indices.length * 2 + 2);
    page.indices.forEach((index, i) => indexBytes.writeUInt16LE(index, i * 2));
    indexBytes.writeUInt16LE(0x7fff, page.indices.length * 2);
    parts.push(indexBytes);
    const joined = Buffer.concat(parts);
    return Buffer.concat([joined, Buffer.alloc(align16(joined.length) - joined.length)]);
  }
}

export function buildTome(request) {
  const { worldMin, worldMax, banner = DEFAULT_BANNER, pages } = request;
  const pageCount = pages.length;
  const leafTotal = pages.reduce((sum, p) => sum + p.leaves.length, 0);

  const payloads = pages.map(encodePagePayload);

  const descriptorStart = FIRST_SECTION;
  const sectionStart = descriptorStart + Math.max(pageCount, 1) * 0x70;
  const pageEntryStart = sectionStart;
  // Constraint: retail publishes page offsets as a u32 array, so each table slot is four bytes.
  const pageEntryBytes = pageCount * 4;
  let cursor = align16(pageEntryStart + pageEntryBytes);
  const payloadOffsets = [];
  for (const payload of payloads) {
    cursor = align16(cursor);
    payloadOffsets.push(cursor);
    cursor += payload.length;
  }
  const leafTableStart = align16(cursor);
  const leafTableBytes = 4 + leafTotal * 20;
  const childStart = align16(leafTableStart + leafTableBytes);
  const childBytes = leafTotal * 32;
  let size = align16(childStart + childBytes);
  if (size < HEADER_MIN) size = HEADER_MIN;
  const out = Buffer.alloc(size);

  out.writeUInt32LE(MAGIC, 0);
  out.writeUInt32LE(size, 8);
  out.writeFloatLE(512, 0x0c);
  // Constraint: the world box is three minimums at 0x14 and three maximums at 0x20, as measured on retail tomes.
  for (let i = 0; i < 3; i++) {
    out.writeFloatLE(worldMin[i], 0x14 + i * 4);
    out.writeFloatLE(worldMax[i], 0x20 + i * 4);
  }
  out.writeUInt32LE(pageCount * 0x20, 0x2c);
  out.writeUInt32LE(descriptorStart, 0x30);
  out.writeUInt32LE(pageCount, 0x38);
  out.writeUInt32LE(descriptorStart + 0x10, 0x3c);
  out.writeUInt32LE(pageCount, 0x7c);
  out.writeUInt32LE(pageEntryStart, 0x80);
  out.writeUInt32LE(descriptorStart + 0x20, 0x88);
  out.writeUInt32LE(leafTotal, 0x8c);
  out.writeUInt32LE(leafTotal, 0x90);
  out.writeUInt32LE(pageCount, 0x94);
  out.writeUInt32LE(descriptorStart + 0x30, 0x98);
  out.writeUInt32LE(descriptorStart + 0x40, 0x9c);
  out.writeUInt32LE(pageEntryStart, 0xa0);
  out.writeUInt32LE(leafTableStart, 0xa4);
  out.writeUInt32LE(childStart, 0xa8);
  out.writeUInt32LE(leafTotal, 0xac);
  out.write(banner, BANNER_OFFSET, 'latin1');

  // Constraint: one 0x70-byte record block per page at the descriptor offset.
  for (let p = 0; p < pageCount; p++) {
    const at = descriptorStart + p * 0x70;
    out.writeUInt32LE(3, at);
    out.writeUInt32LE(0x7f7fffff, at + 0x10);
    out.writeUInt32LE(1, at + 0x20);
    out.writeFloatLE(1, at + 0x30);
    out.writeUInt32LE(p, at + 0x40);
  }

  // Constraint: each page entry stores the offset of its own payload.
  for (let p = 0; p < pageCount; p++) {
    out.writeUInt32LE(payloadOffsets[p], pageEntryStart + p * 4);
  }
  payloads.forEach((payload, p) => payload.copy(out, payloadOffsets[p]));

  out.writeUInt32LE(leafTotal, leafTableStart);
  let leafIndex = 0;
  let childCursor = childStart;
  for (const page of pages) {
    for (const leaf of page.leaves) {
      const rowAt = leafTableStart + 4 + leafIndex * 20;
      out.writeUInt32LE(EMPTY_PAGE_CONSTANTS.kind, rowAt);
      out.writeUInt32LE(childCursor, rowAt + 4);
      out.writeUInt32LE(childCursor + 16, rowAt + 8);
      out.writeUInt32LE(leaf.count, childCursor);
      out.writeUInt32LE(leaf.kind, childCursor + 4);
      out.writeUInt32LE(0, childCursor + 16);
      leafIndex++;
      childCursor += 32;
    }
  }

  const crc = crc32c(out.subarray(8));
  out.writeUInt32LE(crc, 4);
  return out;
}

// Constraint: a round trip must decode the payload offsets the writer emitted.

export function readGeometry(bytes) {
  const parsed = parseTome(bytes);
  const pages = [];
  for (let p = 0; p < parsed.leaf.count; p++) {
    const entryAt = parsed.header.offsets.page + p * 16;
    if (entryAt + 16 > bytes.length) break;
  }
  const target = parsed.page.target;
  const bounds = parsed.page.bounds;
  const vertexCount = parsed.page.vertexCount;
  const vertices = [];
  for (let i = 0; i < vertexCount; i++) {
    const row = [];
    for (let axis = 0; axis < 3; axis++) {
      const q = bytes.readInt16LE(target + 0x50 + i * 6 + axis * 2);
      const extent = bounds[axis * 2 + 1] - bounds[axis * 2];
      row.push(bounds[axis * 2] + ((q / 16384) * 0.5 + 0.5) * extent);
    }
    vertices.push(row);
  }
  const indexStart = target + 0x50 + vertexCount * 6;
  const indices = [];
  for (let i = 0; i < parsed.page.indexCount; i++) {
    indices.push(bytes.readUInt16LE(indexStart + i * 2));
  }
  return { bounds, vertices, indices, leaves: parsed.leaf.rows, child: parsed.child.records };
}

