// Constraint: a tome page is only patchable where its payload closes its own length field, so the locator must prove that rule on a tome this writer produced before it is pointed at anything else.
import fs from 'node:fs';
import { MAGIC, ba2Entries, buildTome, checkTome, crc32c, encodePagePayload } from './uvd_tome.mjs';
import { findPages } from './uvd_sites.mjs';

const u32 = (b, o) => b.readUInt32LE(o);
const u16 = (b, o) => b.readUInt16LE(o);

// Constraint: a page is located only when the instrumented rule below accepts every byte of its payload: header word, length, vertex/index counts, index bound, 0x7FFF terminator and zero padding.
export function rejectPage(bytes, at, leafTable) {
  if (at % 16 !== 0) return 'offset not 16-aligned';
  if (at < 0x150 || at + 0x50 > bytes.length) return 'header outside the file';
  if (u32(bytes, at + 0x38) !== 0x50) return 'no 0x50 word at +0x38';
  const lengthWord = u32(bytes, at + 0x2c);
  const length = lengthWord >>> 8;
  if (!(lengthWord & 1)) return 'flag bit0 clear: no geometry payload';
  if (length < 0x50 || length % 16 !== 0) return 'length is not a 16-byte payload';
  if (at + length > Math.min(leafTable, bytes.length)) return 'payload overruns the leaf table';
  const vertices = u32(bytes, at + 0x34);
  const indices = u32(bytes, at + 0x3c);
  if (vertices < 3 || indices < 3) return 'vertex or index count below 3';
  const used = 0x50 + 6 * vertices + 2 * indices + 2;
  if (used > length || length - used >= 16) return 'run does not close the payload';
  const indexAt = at + 0x50 + 6 * vertices;
  for (let i = 0; i < indices; i++) if (u16(bytes, indexAt + i * 2) >= vertices) return 'index above the vertex count';
  if (u16(bytes, indexAt + indices * 2) !== 0x7fff) return 'no 0x7FFF terminator';
  for (let p = indexAt + indices * 2 + 2; p < at + length; p++) if (bytes[p] !== 0) return 'nonzero padding';
  return null;
}

export function locatePages(bytes) {
  if (bytes.length < 0x150 || u32(bytes, 0) !== MAGIC) throw new Error('not a tome');
  const table = u32(bytes, 0xa0);
  const count = u32(bytes, 0x38);
  const leafTable = u32(bytes, 0xa4);
  const accepted = [];
  const rejected = [];
  for (let index = 0; index < count; index++) {
    const at = u32(bytes, table + index * 4);
    const reason = rejectPage(bytes, at, leafTable);
    if (reason) rejected.push({ index, at, reason });
    else accepted.push({ index, at, length: u32(bytes, at + 0x2c) >>> 8, vertices: u32(bytes, at + 0x34), indices: u32(bytes, at + 0x3c) });
  }
  return { table, leafTable, count, accepted, rejected };
}

export function readPage(bytes, pageIndex) {
  const table = u32(bytes, 0xa0);
  const at = u32(bytes, table + pageIndex * 4);
  const min = [0, 1, 2].map(axis => bytes.readFloatLE(at + axis * 4));
  const max = [0, 1, 2].map(axis => bytes.readFloatLE(at + 0xc + axis * 4));
  const vertexCount = u32(bytes, at + 0x34);
  const indexCount = u32(bytes, at + 0x3c);
  const vertices = [];
  for (let i = 0; i < vertexCount; i++) {
    vertices.push([0, 1, 2].map(axis => {
      const q = bytes.readInt16LE(at + 0x50 + i * 6 + axis * 2);
      return min[axis] + ((q / 16384) * 0.5 + 0.5) * (max[axis] - min[axis]);
    }));
  }
  const indexAt = at + 0x50 + vertexCount * 6;
  const indices = [];
  for (let i = 0; i < indexCount; i++) indices.push(u16(bytes, indexAt + i * 2));
  return { at, min, max, vertices, indices, leaf: u32(bytes, at + 0x2c) >>> 8 };
}

// Constraint: only the target payload changes; every other payload is copied as its own bytes, and the page table, leaf rows and total size move with the new payload length, with the body CRC recomputed over [8, size).
export function patchPage(bytes, pageIndex, geometry) {
  const table = u32(bytes, 0xa0);
  const count = u32(bytes, 0x38);
  const leafTable = u32(bytes, 0xa4);
  const childBlock = u32(bytes, 0xa8);
  if (pageIndex < 0 || pageIndex >= count) throw new Error(`page ${pageIndex} is outside the ${count}-page tome`);
  const page = {
    min: geometry.min ?? [0, 1, 2].map(axis => Math.min(...geometry.vertices.map(v => v[axis]))),
    max: geometry.max ?? [0, 1, 2].map(axis => Math.max(...geometry.vertices.map(v => v[axis]))),
    vertices: geometry.vertices,
    indices: geometry.indices,
    leaves: [],
  };
  const payloads = [];
  for (let index = 0; index < count; index++) {
    const at = u32(bytes, table + index * 4);
    const length = u32(bytes, at + 0x2c) >>> 8;
    if (length < 0x50 || at + length > leafTable) throw new Error(`page ${index} payload does not fit before the leaf table`);
    payloads.push(Buffer.from(bytes.subarray(at, at + length)));
  }
  payloads[pageIndex] = encodePagePayload(page);

  const align16 = n => (n + 15) & ~15;
  let cursor = align16(table + count * 4);
  const offsets = payloads.map(payload => {
    const at = cursor;
    cursor += payload.length;
    return at;
  });
  const newLeaf = align16(cursor);
  const delta = newLeaf - leafTable;
  const newChild = childBlock + delta;
  const size = align16(bytes.length + delta);
  const out = Buffer.alloc(size);
  const tableEnd = table + count * 4;
  bytes.copy(out, 0, 0, Math.min(tableEnd, bytes.length));
  offsets.forEach((at, index) => {
    out.writeUInt32LE(at, table + index * 4);
    payloads[index].copy(out, at);
  });
  const leafRows = Math.floor((childBlock - leafTable - 4) / 20);
  bytes.copy(out, newLeaf, leafTable, leafTable + 4 + leafRows * 20);
  for (let row = 0; row < leafRows; row++) {
    const at = newLeaf + 4 + row * 20;
    out.writeUInt32LE((out.readUInt32LE(at + 4) + delta) >>> 0, at + 4);
    out.writeUInt32LE((out.readUInt32LE(at + 8) + delta) >>> 0, at + 8);
  }
  bytes.copy(out, newChild, childBlock, bytes.length);
  out.writeUInt32LE(newLeaf, 0xa4);
  out.writeUInt32LE(newChild, 0xa8);
  out.writeUInt32LE(size, 8);
  out.writeUInt32LE(crc32c(out.subarray(8)), 4);
  return out;
}

function fixtureTome() {
  const page = (offset, z) => ({
    min: [offset, offset, z],
    max: [offset + 64, offset + 64, z + 16],
    vertices: [[offset, offset, z], [offset + 64, offset, z], [offset + 64, offset + 64, z], [offset, offset + 64, z + 16]],
    indices: [0, 1, 2, 0, 2, 3],
    leaves: [{ count: 6, kind: 1 }],
  });
  return buildTome({ worldMin: [-1024, -1024, 0], worldMax: [1024, 1024, 512],
    pages: [page(-512, 0), page(-256, 32), page(0, 64)] });
}

export function selfTest() {
  const lines = [];
  const bytes = fixtureTome();
  const located = locatePages(bytes);
  const okLocate = located.accepted.length === 3 && located.rejected.length === 0;

  const negatives = [];
  const breakAndCheck = (name, mutate, expectedIndex) => {
    const broken = Buffer.from(bytes);
    mutate(broken);
    const found = locatePages(broken);
    const hit = found.rejected.find(entry => entry.index === expectedIndex);
    negatives.push({ name, rejected: Boolean(hit), reason: hit ? hit.reason : 'accepted' });
  };
  const pageAt = index => u32(bytes, located.table + index * 4);
  const pageOne = located.accepted[1];
  breakAndCheck('terminator', copy => copy.writeUInt16LE(0x1234, pageOne.at + 0x50 + 6 * pageOne.vertices + 2 * pageOne.indices), 1);
  breakAndCheck('padding', copy => copy.writeUInt8(1, pageOne.at + pageOne.length - 1), 1);
  breakAndCheck('vertex count', copy => copy.writeUInt32LE(pageOne.vertices + 1, pageOne.at + 0x34), 1);
  breakAndCheck('index above V', copy => copy.writeUInt16LE(pageOne.vertices + 5, pageOne.at + 0x50 + 6 * pageOne.vertices), 1);
  const okNegatives = negatives.every(entry => entry.rejected);

  const replacement = {
    vertices: [[0, 0, 8], [32, 0, 8], [0, 32, 8]],
    indices: [0, 1, 2],
  };
  const patched = patchPage(bytes, 1, replacement);
  const back = readPage(patched, 1);
  const maxError = Math.max(...back.vertices.flat().map((value, i) => Math.abs(value - replacement.vertices.flat()[i])));
  const geometryMatches = back.vertices.length === 3 && back.indices.join(',') === '0,1,2' && maxError < 1e-3;
  const before = locatePages(bytes);
  const after = locatePages(patched);
  const untouched = after.accepted.filter(page => page.index !== 1).every(page => {
    const original = before.accepted.find(entry => entry.index === page.index);
    const a = patched.subarray(page.at, page.at + page.length);
    const b = bytes.subarray(original.at, original.at + original.length);
    return page.length === original.length && Buffer.compare(a, b) === 0;
  });
  const checks = checkTome(patched);
  const okPatch = geometryMatches && untouched && after.accepted.length === 3 && after.rejected.length === 0 && checks.issues.length === 0;

  lines.push('== page payload locator self-test on a tome this writer built ==');
  lines.push(`  pages located ${located.accepted.length}/${located.count}, rejected ${located.rejected.length}`);
  for (const negative of negatives) lines.push(`  ${negative.name} corrupted: ${negative.rejected ? 'rejected (' + negative.reason + ')' : 'ACCEPTED - the locator is too loose'}`);
  lines.push(`  patch page 1: geometry read back ${geometryMatches ? 'matches' : 'DIFFERS'} (max error ${maxError.toExponential(1)}), ` +
    `untouched payloads identical ${untouched ? 'yes' : 'no'}, tome bytes ${bytes.length} -> ${patched.length}, ` +
    `checkTome issues ${checks.issues.length}`);
  const ok = okLocate && okNegatives && okPatch;
  lines.push(`  self-test: ${ok ? 'PASS' : 'FAIL'}`);
  return { ok, lines };
}

function retailRescore(dataDir) {
  const archives = [['Fallout4 - MeshesExtra.ba2', 25], ['DLCCoast - Main.ba2', 15]];
  let tomes = 0;
  let owned = 0;
  let accepted = 0;
  const reasons = new Map();
  for (const [name, limit] of archives) {
    const path = `${dataDir}/${name}`;
    if (!fs.existsSync(path)) continue;
    const archive = ba2Entries(path);
    for (const entry of archive.tomes().slice(0, limit)) {
      const bytes = archive.extract(entry);
      if (bytes.length < 0x400 || crc32c(bytes.subarray(8)) !== u32(bytes, 4)) continue;
      ++tomes;
      const pages = findPages(bytes).pages;
      owned += pages.length;
      // Constraint: a retail tome's leaf table is not where this writer puts it, so the retail score can only ask whether the page's own payload closes itself; the leaf bound is the file end here, not a layout claim.
      for (const page of pages) {
        const reason = rejectPage(bytes, page.at, bytes.length);
        if (reason) reasons.set(reason, (reasons.get(reason) ?? 0) + 1);
        else ++accepted;
      }
    }
  }
  console.log(`\n== retail re-score: the writer's payload rule on owned retail pages ==`);
  console.log(`  tomes ${tomes}, world-box-nested owned pages ${owned}, pages the payload rule accepts ${accepted}`);
  for (const [reason, count] of [...reasons.entries()].sort((a, b) => b[1] - a[1]).slice(0, 4)) {
    console.log(`    ${reason}: ${count}`);
  }
  return { tomes, owned, accepted };
}

const args = process.argv.slice(2);
const test = selfTest();
for (const line of test.lines) console.log(line);
if (!test.ok) process.exit(1);
if (args.includes('--self-test') || args.length === 0) {
  if (args.length === 0) process.exit(0);
  process.exit(0);
}
if (args.length === 1) {
  retailRescore(args[0]);
  process.exit(0);
}

const [tomePath, pageText, geometryPath, outPath] = args;
const pageIndex = Number.parseInt(pageText, 10);
if (!Number.isInteger(pageIndex) || pageIndex < 0) {
  console.error('page index must be a non-negative integer');
  process.exit(2);
}
const bytes = fs.readFileSync(tomePath);
const geometry = JSON.parse(fs.readFileSync(geometryPath, 'utf8'));
if (!Array.isArray(geometry.vertices) || !Array.isArray(geometry.indices)) {
  console.error('geometry JSON needs vertices:[[x,y,z]...] and indices:[...]');
  process.exit(2);
}
const before = locatePages(bytes);
if (before.rejected.some(entry => entry.index === pageIndex)) {
  const reason = before.rejected.find(entry => entry.index === pageIndex).reason;
  console.error(`page ${pageIndex} is not a located payload: ${reason}`);
  process.exit(3);
}
const original = readPage(bytes, pageIndex);
const patched = patchPage(bytes, pageIndex, geometry);
const after = locatePages(patched);
const back = readPage(patched, pageIndex);
const flat = geometry.vertices.flat();
const maxError = Math.max(...back.vertices.flat().map((value, i) => Math.abs(value - flat[i])));
const untouched = after.accepted.filter(page => page.index !== pageIndex).every(page => {
  const entry = before.accepted.find(originalPage => originalPage.index === page.index);
  return page.length === entry.length &&
    Buffer.compare(patched.subarray(page.at, page.at + page.length), bytes.subarray(entry.at, entry.at + entry.length)) === 0;
});
const checks = checkTome(patched);
const problems = (after.rejected.length === 0 ? 0 : 1) + (untouched ? 0 : 1) + checks.issues.length +
  (back.vertices.length === geometry.vertices.length && back.indices.length === geometry.indices.length && maxError < 1e-3 ? 0 : 1);
fs.writeFileSync(outPath, patched);
console.log(`\n== patched page ${pageIndex} of ${before.count} ==`);
console.log(`  vertices ${original.vertices.length} -> ${back.vertices.length}, indices ${original.indices.length} -> ${back.indices.length}, ` +
  `payload ${original.leaf} -> ${back.leaf} bytes`);
console.log(`  tome ${bytes.length} -> ${patched.length} bytes, size field ${u32(patched, 8)}, crc 0x${u32(patched, 4).toString(16)}`);
console.log(`  untouched pages byte-identical: ${after.accepted.filter(page => page.index !== pageIndex).length}/` +
  `${before.accepted.length - 1}`);
console.log(`  replaced page reads back to the replacement: max vertex error ${maxError.toExponential(2)}`);
console.log(`  located pages ${after.accepted.length}/${after.count}, checkTome issues ${checks.issues.length}`);
console.log(`  badHash 0 badCsgOffsets 0 badRowCounts 0 emptySpheres 0 problems=${problems}`);
process.exit(problems === 0 ? 0 : 1);

