#!/usr/bin/env node
// Constraint: a shipped CSG container is measured against the layout the writer relies on; any violated property exits 1.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

const u32 = (b, o) => b.readUInt32LE(o);
const INFLATED = 65536;
const K_HEAD = 28;
const K_CORE = 32;
const K_SUFFIX = 8;
const K_REC = 72;

function parseCsg(b) {
  if (b.subarray(0, 4).toString('ascii') !== 'bcsg') throw new Error('missing bcsg magic');
  const rowCount = u32(b, 4);
  const chunkCount = u32(b, 8);
  const chunks = [];
  for (let i = 0; i < chunkCount; i++) {
    chunks.push({ compressed: u32(b, 12 + i * 8), offset: u32(b, 16 + i * 8) });
  }
  const rowTable = 12 + chunkCount * 8;
  const rows = [];
  for (let i = 0; i < rowCount; i++) {
    const at = rowTable + i * 20;
    rows.push({
      dataOffset: u32(b, at),
      descLow: u32(b, at + 4),
      vertexCount: u32(b, at + 12),
      indexCount: u32(b, at + 16),
    });
  }
  const inflated = chunks.map(chunk => zlib.inflateSync(b.subarray(chunk.offset, chunk.offset + chunk.compressed)));
  return { bytes: b.length, rowCount, chunkCount, rows, inflated, stream: Buffer.concat(inflated) };
}

function ba2Crc32(text) {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[i] = c >>> 0;
  }
  let crc = 0;
  for (const byte of Buffer.from(text, 'latin1')) crc = (crc >>> 8) ^ table[(crc ^ byte) & 0xff];
  return crc >>> 0;
}

function sharedObjectTables(b) {
  const found = [];
  for (let base = 0; base + K_HEAD <= b.length; base += 4) {
    const objectCount = u32(b, base + 24);
    if (objectCount < 1 || objectCount > 128) continue;
    if (base + K_HEAD + 8 * objectCount > b.length) continue;
    const table = [];
    const cores = [];
    let pos = base + K_HEAD + 8 * objectCount;
    let ok = true;
    for (let o = 0; o < objectCount; o++) {
      table.push({ fileHash: u32(b, base + K_HEAD + 8 * o), dataOffset: u32(b, base + K_HEAD + 8 * o + 4) });
      if (pos + K_CORE + 4 > b.length) { ok = false; break; }
      cores.push({
        vertexCount: u32(b, pos),
        triCount0: u32(b, pos + 8),
        triCount1: u32(b, pos + 16),
        triCount2: u32(b, pos + 24),
      });
      pos += K_CORE;
      const combined = u32(b, pos);
      pos += 4;
      if (combined < 1 || combined > 32 || pos + K_REC * combined + K_SUFFIX > b.length) { ok = false; break; }
      pos += K_REC * combined + K_SUFFIX;
    }
    if (!ok || table.some(entry => entry.dataOffset >= 1e9)) continue;
    found.push({ base, table, cores });
    base = pos;
  }
  return found;
}

const stride = row => (row.descLow & 0xf) * 4;
const streamBytes = row => row.vertexCount * stride(row) + row.indexCount * 2;

function containerFindings(csg) {
  const found = [];
  if (csg.rows.length === 0) found.push('the container holds no rows');
  csg.inflated.forEach((chunk, i) => {
    const last = i === csg.inflated.length - 1;
    const ok = last ? chunk.length >= 1 && chunk.length <= INFLATED : chunk.length === INFLATED;
    if (!ok) found.push(`chunk ${i} inflates to ${chunk.length} bytes, expected ${last ? `1..${INFLATED}` : INFLATED}`);
  });
  let broken = 0;
  for (let i = 0; i + 1 < csg.rows.length; i++) {
    if (csg.rows[i].dataOffset + streamBytes(csg.rows[i]) !== csg.rows[i + 1].dataOffset) broken++;
  }
  if (broken > 0) found.push(`${broken} of ${csg.rows.length - 1} rows do not chain at vertex_count*stride + 2*index_count`);
  const last = csg.rows[csg.rows.length - 1];
  if (last && last.dataOffset + streamBytes(last) > csg.stream.length) {
    found.push(`the last row ends at ${last.dataOffset + streamBytes(last)}, past the ${csg.stream.length} stream bytes`);
  }
  return found;
}

function addressingFindings(csg, derived, objectTables) {
  const found = [];
  const rowByOffset = new Map(csg.rows.map(row => [row.dataOffset, row]));
  const foreign = new Set();
  let addressed = 0;
  let onRow = 0;
  let sameCounts = 0;
  for (const { table, cores } of objectTables) {
    table.forEach((entry, o) => {
      if (entry.fileHash !== derived) { foreign.add(entry.fileHash); return; }
      addressed++;
      const hit = rowByOffset.get(entry.dataOffset);
      if (!hit) return;
      onRow++;
      const core = cores[o];
      const triangles = core.triCount0 + core.triCount1 + core.triCount2;
      if (core.vertexCount === hit.vertexCount && triangles * 3 === hit.indexCount) sameCounts++;
    });
  }
  const hex = h => `0x${h.toString(16).padStart(8, '0')}`;
  if (foreign.size > 0) found.push(`object tables carry hashes other than ${hex(derived)}: ${[...foreign].map(hex).join(' ')}`);
  if (addressed === 0) found.push('no _OC object names this container');
  if (onRow !== addressed) found.push(`only ${onRow} of ${addressed} object offsets land on a row start`);
  if (sameCounts !== addressed) found.push(`only ${sameCounts} of ${addressed} objects agree with their row on vertex and triangle counts`);
  return { found, addressed, onRow, sameCounts };
}

function missingOcDirFindings(ocDir) {
  return ocDir && !fs.existsSync(ocDir) ? [`the _OC folder ${ocDir} does not exist`] : [];
}

function measure(csgPath, ocDir, pluginName) {
  const csg = parseCsg(fs.readFileSync(csgPath));
  console.log(`${path.basename(csgPath)}: ${csg.bytes} bytes, ${csg.rowCount} rows, ${csg.chunkCount} chunks, ${csg.stream.length} inflated bytes`);
  const found = containerFindings(csg);
  const chained = csg.rows.slice(1).filter((row, i) => csg.rows[i].dataOffset + streamBytes(csg.rows[i]) === row.dataOffset).length;
  console.log(`  rows chained: ${chained}/${Math.max(csg.rows.length - 1, 0)}`);
  found.push(...missingOcDirFindings(ocDir));
  if (pluginName) {
    const stem = path.parse(pluginName).name.toLowerCase();
    const derived = ba2Crc32(`${stem} - geometry`);
    console.log(`  file hash ba2_crc32("${stem} - geometry") = 0x${derived.toString(16).padStart(8, '0')}`);
    if (ocDir && fs.existsSync(ocDir)) {
      const tables = [];
      for (const name of fs.readdirSync(ocDir).filter(n => n.toLowerCase().endsWith('.nif'))) {
        tables.push(...sharedObjectTables(fs.readFileSync(path.join(ocDir, name))));
      }
      const result = addressingFindings(csg, derived, tables);
      console.log(`  objects naming this container: ${result.addressed}, offsets on a row start: ${result.onRow}/${result.addressed}, counts agree: ${result.sameCounts}/${result.addressed}`);
      found.push(...result.found);
    }
  }
  for (const finding of found) console.log(`  FAILED: ${finding}`);
  console.log(found.length === 0 ? '  all measured properties hold' : `  ${found.length} measured propert${found.length === 1 ? 'y' : 'ies'} violated`);
  return found.length === 0;
}

function syntheticContainer({ shapes, chunkSize = INFLATED, gap = 0, cut = 0 }) {
  const rows = [];
  const parts = [];
  let offset = 0;
  shapes.forEach((shape, i) => {
    if (i === 1) offset += gap;
    rows.push({ dataOffset: offset, descLow: shape.stride / 4, vertexCount: shape.vertices, indexCount: shape.indices });
    const length = shape.vertices * shape.stride + shape.indices * 2;
    parts.push(Buffer.alloc(length, i + 1));
    offset += length;
  });
  const whole = Buffer.concat([parts[0], Buffer.alloc(gap), ...parts.slice(1)]);
  const stream = whole.subarray(0, whole.length - cut);
  const chunks = [];
  for (let at = 0; at < stream.length; at += chunkSize) chunks.push(zlib.deflateSync(stream.subarray(at, at + chunkSize)));
  const header = Buffer.alloc(12 + chunks.length * 8);
  header.write('bcsg', 0, 'ascii');
  header.writeUInt32LE(rows.length, 4);
  header.writeUInt32LE(chunks.length, 8);
  let payload = header.length + rows.length * 20;
  chunks.forEach((chunk, i) => {
    header.writeUInt32LE(chunk.length, 12 + i * 8);
    header.writeUInt32LE(payload, 16 + i * 8);
    payload += chunk.length;
  });
  const table = Buffer.alloc(rows.length * 20);
  rows.forEach((row, i) => {
    table.writeUInt32LE(row.dataOffset, i * 20);
    table.writeUInt32LE(row.descLow, i * 20 + 4);
    table.writeUInt32LE(row.vertexCount, i * 20 + 12);
    table.writeUInt32LE(row.indexCount, i * 20 + 16);
  });
  return Buffer.concat([header, table, ...chunks]);
}

function syntheticObjectTable(objects) {
  const out = Buffer.alloc(K_HEAD + 8 * objects.length + objects.length * (K_CORE + 4 + K_REC + K_SUFFIX));
  out.writeUInt32LE(objects.length, 24);
  objects.forEach((object, i) => {
    out.writeUInt32LE(object.hash, K_HEAD + 8 * i);
    out.writeUInt32LE(object.offset, K_HEAD + 8 * i + 4);
  });
  let pos = K_HEAD + 8 * objects.length;
  for (const object of objects) {
    out.writeUInt32LE(object.vertices, pos);
    out.writeUInt32LE(object.triangles, pos + 8);
    out.writeUInt32LE(1, pos + K_CORE);
    pos += K_CORE + 4 + K_REC + K_SUFFIX;
  }
  return out;
}

function selfTest() {
  const shapes = [{ stride: 24, vertices: 2000, indices: 600 }, { stride: 16, vertices: 1500, indices: 300 }, { stride: 24, vertices: 900, indices: 90 }];
  const derived = ba2Crc32('myplugin - geometry');
  const cases = [];
  const expect = (name, found, wantFindings) => {
    const ok = wantFindings === 0 ? found.length === 0 : found.length >= wantFindings;
    cases.push(ok);
    console.log(`  ${name.padEnd(64)} ${ok ? 'ok' : `FAILED (${found.length} findings: ${found.join('; ')})`}`);
  };
  const valid = parseCsg(syntheticContainer({ shapes }));
  console.log(`synthetic container: ${valid.chunkCount} chunks, ${valid.stream.length} inflated bytes`);
  expect('a well-formed container has no findings', containerFindings(valid), 0);
  expect('a row off its chain position is reported', containerFindings(parseCsg(syntheticContainer({ shapes, gap: 8 }))), 1);
  expect('chunks inflating below 65536 before the last are reported', containerFindings(parseCsg(syntheticContainer({ shapes, chunkSize: 32768 }))), 1);
  expect('a last row running past the stream is reported', containerFindings(parseCsg(syntheticContainer({ shapes, cut: 16 }))), 1);

  const starts = valid.rows.map(row => row.dataOffset);
  const object = (i, patch = {}) => ({ hash: derived, offset: starts[i], vertices: shapes[i].vertices, triangles: shapes[i].indices / 3, ...patch });
  const tables = objects => sharedObjectTables(syntheticObjectTable(objects));
  const good = addressingFindings(valid, derived, tables([object(0), object(1), object(2)]));
  expect('objects on row starts with matching counts have no findings', good.found, 0);
  cases.push(good.addressed === 3 && good.onRow === 3 && good.sameCounts === 3);
  expect('an object off any row start is reported', addressingFindings(valid, derived, tables([object(0), object(1, { offset: starts[1] + 4 })])).found, 1);
  expect('an object whose vertex count disagrees is reported', addressingFindings(valid, derived, tables([object(0, { vertices: 1999 })])).found, 1);
  expect('an object whose triangle count disagrees is reported', addressingFindings(valid, derived, tables([object(2, { triangles: 31 })])).found, 1);
  expect('an object naming another container is reported', addressingFindings(valid, derived, tables([object(0), object(1, { hash: (derived ^ 1) >>> 0 })])).found, 1);
  expect('no object naming the container is reported', addressingFindings(valid, derived, tables([object(0, { hash: (derived ^ 1) >>> 0 })])).found, 1);
  expect('a missing _OC folder is reported', missingOcDirFindings(path.join(process.argv[1], 'missing')), 1);
  expect('an existing _OC folder has no finding', missingOcDirFindings('.'), 0);
  const failed = cases.filter(ok => !ok).length;
  console.log(failed === 0 ? 'measure_csg self-test passed' : `measure_csg self-test: ${failed} case(s) failed`);
  return failed === 0;
}

const args = process.argv.slice(2);
if (args[0] === '--self-test') process.exit(selfTest() ? 0 : 1);
if (!args[0]) {
  console.error('usage: measure_csg.mjs <csg> [oc-dir] [plugin-name] | --self-test');
  process.exit(2);
}
process.exit(measure(args[0], args[1], args[2]) ? 0 : 1);
