// Constraint: the writer's rule is calibrated and checked against shipped objects, so these dumps carry their vanilla counts.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { ba2Entries } from './uvd_tome.mjs';

const dataDir = process.argv[2];
const outPath = process.argv[3];
const count = Number(process.argv[4] ?? 1);
const minVertices = Number(process.argv[5] ?? 600);
const maxVertices = Number(process.argv[6] ?? 4000);
const csgName = 'Fallout4 - Geometry.csg';
const archiveName = 'Fallout4 - MeshesExtra.ba2';

if (!dataDir || !outPath) {
  console.error('usage: lod_contract_object.mjs <Data> <file.bin|directory> [count] [minVertices] [maxVertices]');
  process.exit(2);
}

const u32 = (b, o) => b.readUInt32LE(o);
const u64 = (b, o) => Number(b.readBigUInt64LE(o));
const HEAD = 28;
const CORE = 32;
const RECORD = 72;
const DESC = 8;
const INFLATED = 65536;
const BLOB_HEADER = 52;

function ba2Crc32(text) {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[i] = c >>> 0;
  }
  let crc = 0;
  for (const byte of Buffer.from(text.toLowerCase(), 'latin1')) crc = (crc >>> 8) ^ table[(crc ^ byte) & 0xff];
  return crc >>> 0;
}

function loadCsg(file) {
  const bytes = fs.readFileSync(file);
  if (bytes.subarray(0, 4).toString('ascii') !== 'bcsg') throw new Error('missing bcsg magic');
  const rowCount = u32(bytes, 4);
  const chunkCount = u32(bytes, 8);
  const chunks = [];
  for (let i = 0; i < chunkCount; i++) {
    chunks.push({ compressed: u32(bytes, 12 + i * 8), offset: u32(bytes, 16 + i * 8) });
  }
  const table = 12 + chunkCount * 8;
  const rows = [];
  for (let i = 0; i < rowCount; i++) {
    const at = table + i * 20;
    rows.push({
      dataOffset: u32(bytes, at),
      vertexCount: u32(bytes, at + 12),
      indexCount: u32(bytes, at + 16),
      stride: (u32(bytes, at + 4) & 0xf) * 4,
    });
  }
  const cache = new Map();
  const chunk = index => {
    if (!cache.has(index)) {
      const entry = chunks[index];
      cache.set(index, zlib.inflateSync(bytes.subarray(entry.offset, entry.offset + entry.compressed)));
    }
    return cache.get(index);
  };
  const readBytes = (offset, length) => {
    const out = Buffer.alloc(length);
    let written = 0;
    while (written < length) {
      const absolute = offset + written;
      const index = Math.floor(absolute / INFLATED);
      if (index >= chunks.length) throw new Error('read runs past the stream');
      const within = absolute % INFLATED;
      const buffer = chunk(index);
      const take = Math.min(length - written, INFLATED - within);
      buffer.copy(out, written, within, within + take);
      written += take;
    }
    return out;
  };
  return { readBytes, rowAt: new Map(rows.map(row => [row.dataOffset, row])) };
}

function objectsOf(bytes) {
  for (let base = 0; base + HEAD <= bytes.length; base += 4) {
    const objectCount = u32(bytes, base + 24);
    if (objectCount < 1 || objectCount > 64) continue;
    if (base + HEAD + 8 * objectCount > bytes.length) continue;
    let pos = base + HEAD + 8 * objectCount;
    const objects = [];
    let ok = true;
    for (let i = 0; i < objectCount; i++) {
      if (pos + CORE + 4 > bytes.length) { ok = false; break; }
      const vertexCount = u32(bytes, pos);
      const triangleCount = [u32(bytes, pos + 8), u32(bytes, pos + 16), u32(bytes, pos + 24)];
      const triangleOffset = [u32(bytes, pos + 12), u32(bytes, pos + 20), u32(bytes, pos + 28)];
      pos += CORE;
      const combined = u32(bytes, pos);
      pos += 4;
      if (combined < 1 || combined > 64) { ok = false; break; }
      if (pos + RECORD * combined + DESC > bytes.length) { ok = false; break; }
      pos += RECORD * combined;
      const stride = (u64(bytes, pos) & 0xf) * 4;
      pos += DESC;
      if (stride < 8 || stride > 64) { ok = false; break; }
      objects.push({
        fileHash: u32(bytes, base + HEAD + 8 * i),
        dataOffset: u32(bytes, base + HEAD + 8 * i + 4),
        vertexCount,
        triangleCount,
        triangleOffset,
        stride,
      });
    }
    if (ok && objects.length) return objects;
  }
  return null;
}

const csg = loadCsg(path.join(dataDir, csgName));
const csgHash = ba2Crc32(csgName.replace(/\.[^.]+$/, '').replace(' - Geometry', ' - geometry'));
const archive = ba2Entries(path.join(dataDir, archiveName));

const bands = count === 1
  ? [[minVertices, maxVertices]]
  : [[1, 300], [301, 700], [701, 1500], [1501, 4000], [4001, 65535]];
const quota = count === 1 ? 1 : Math.max(1, Math.ceil(count / bands.length));
const picked = bands.map(() => []);
const seen = new Set();

for (const file of archive.entries) {
  if (!/_OC\.nif$/i.test(file.path)) continue;
  if (picked.every(band => band.length >= quota)) break;
  const objects = objectsOf(archive.extract(file));
  if (!objects) continue;
  for (const object of objects) {
    const key = `${object.dataOffset}:${object.vertexCount}`;
    if (seen.has(key)) continue;
    const row = csg.rowAt.get(object.dataOffset);
    const words = (object.triangleCount[0] + object.triangleCount[1] + object.triangleCount[2]) * 3;
    if (object.fileHash !== csgHash || !row || row.vertexCount !== object.vertexCount ||
        row.indexCount !== words || row.stride !== object.stride) continue;
    const bandIndex = bands.findIndex(([low, high]) => object.vertexCount >= low && object.vertexCount <= high);
    if (bandIndex < 0) continue;
    if (picked[bandIndex].length >= quota) continue;
    const counts = object.triangleCount;
    if (!counts[0] || !counts[1] || !counts[2]) continue;
    seen.add(key);
    picked[bandIndex].push({ file, object });
  }
}

function writeBlob(target, object) {
  const words = (object.triangleCount[0] + object.triangleCount[1] + object.triangleCount[2]) * 3;
  const vertexBytes = csg.readBytes(object.dataOffset, object.vertexCount * object.stride);
  const indexBytes = csg.readBytes(object.dataOffset + object.vertexCount * object.stride, words * 2);
  const levels = [0, 1, 2].map(level => {
    const start = object.triangleOffset[level] * 2;
    const slice = indexBytes.subarray(start, start + object.triangleCount[level] * 6);
    let low = Infinity;
    let high = -1;
    for (let at = 0; at < slice.length; at += 2) {
      const index = slice.readUInt16LE(at);
      low = Math.min(low, index);
      high = Math.max(high, index);
    }
    return { low, high, vertices: high >= 0 ? high - low + 1 : 0 };
  });
  const header = Buffer.alloc(BLOB_HEADER);
  header.write('LODC', 0, 'ascii');
  header.writeUInt32LE(object.stride, 4);
  header.writeUInt32LE(object.vertexCount, 8);
  header.writeUInt32LE(object.triangleCount[0] + object.triangleCount[1] + object.triangleCount[2], 12);
  for (let level = 0; level < 3; level++) {
    header.writeUInt32LE(object.triangleCount[level], 16 + 4 * level);
    header.writeUInt32LE(object.triangleOffset[level], 28 + 4 * level);
    header.writeUInt32LE(levels[level].vertices, 40 + 4 * level);
  }
  fs.writeFileSync(target, Buffer.concat([header, vertexBytes, indexBytes]));
  return {
    path: path.basename(target),
    vertexCount: object.vertexCount,
    triangles: object.triangleCount,
    levelVertices: levels.map(level => level.vertices),
  };
}

const dumped = [];
if (count === 1) {
  const chosen = picked.flat().sort((a, b) => b.object.vertexCount - a.object.vertexCount)[0];
  if (!chosen) throw new Error('no shipped object matched the filter');
  const info = writeBlob(outPath, chosen.object);
  console.log(`object from ${chosen.file.path}`);
  console.log(`  fileHash 0x${chosen.object.fileHash.toString(16)} ${JSON.stringify(info)}`);
} else {
  fs.mkdirSync(outPath, { recursive: true });
  picked.forEach((band, bandIndex) => {
    band.forEach(({ object }, index) => {
      const target = path.join(outPath, `object_${bandIndex}_${index}.bin`);
      dumped.push(writeBlob(target, object));
    });
  });
  if (dumped.length === 0) throw new Error('no shipped object matched the filter');
  console.log(`dumped ${dumped.length} shipped objects into ${outPath}`);
  for (const info of dumped) {
    console.log(`  ${info.path} vertices ${info.vertexCount} triangles ${info.triangles.join('/')} level vertices ${info.levelVertices.join('/')}`);
  }
}
