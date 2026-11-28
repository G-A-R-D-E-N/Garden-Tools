#!/usr/bin/env node
// Constraint: the shipped index lists are the only ground truth for what the CK keeps at each LOD level.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { ba2Entries } from './uvd_tome.mjs';

const dataDir = process.argv[2];
const csgName = process.argv[3] ?? 'Fallout4 - Geometry.csg';
const archiveName = process.argv[4] ?? 'Fallout4 - MeshesExtra.ba2';
const wanted = Number(process.argv[5] ?? 300);
if (!dataDir) {
  console.error('usage: lod_rule.mjs <Data> [csg] [archive] [object-sample]');
  process.exit(2);
}

const u32 = (b, o) => b.readUInt32LE(o);
const u64 = (b, o) => Number(b.readBigUInt64LE(o));
const HEAD = 28;
const CORE = 32;
const RECORD = 72;
const DESC = 8;
const INFLATED = 65536;

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
  const readWords = (offset, count) => {
    const out = new Uint16Array(count);
    let written = 0;
    while (written < count) {
      const absolute = offset + written * 2;
      const index = Math.floor(absolute / INFLATED);
      if (index >= chunks.length) throw new Error('index list runs past the stream');
      const within = absolute % INFLATED;
      const buffer = chunk(index);
      const words = Math.min(count - written, Math.floor((INFLATED - within) / 2));
      for (let i = 0; i < words; i++) out[written + i] = buffer.readUInt16LE(within + i * 2);
      written += words;
    }
    return out;
  };
  return { rows, readWords, rowCount, chunkCount };
}

function objectsOf(bytes) {
  for (let base = 0; base + HEAD <= bytes.length; base += 4) {
    const count = u32(bytes, base + 24);
    if (count < 1 || count > 64) continue;
    if (base + HEAD + 8 * count > bytes.length) continue;
    let pos = base + HEAD + 8 * count;
    const objects = [];
    let ok = true;
    for (let i = 0; i < count; i++) {
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
const csgHash = ba2Crc32(path.basename(csgName).replace(/\.[^.]+$/, '').replace(' - Geometry', ' - geometry'));
const archive = ba2Entries(path.join(dataDir, archiveName));
const files = archive.entries.filter(entry => /_OC\.nif$/i.test(entry.path));

const stats = {
  tested: 0,
  lod1present: 0,
  lod2present: 0,
  level1SubsetOfLevel0: 0,
  level2SubsetOfLevel1: 0,
  level2SubsetOfLevel0: 0,
  level1PrefixOfLevel0: 0,
  level1Ordered: 0,
  blocksContiguous: 0,
  withAllThree: 0,
  blocksCoverBuffer: 0,
  rangesAscend: 0,
  level1CoarserThanLevel2: 0,
};
const vertices = [[], [], []];
const triangles = [[], [], []];

for (const file of files) {
  if (stats.tested >= wanted) break;
  if (stats.rejected === undefined) stats.rejected = 0;
  const bytes = archive.extract(file);
  const objects = objectsOf(bytes);
  if (!objects) continue;
  for (const object of objects) {
    if (stats.tested >= wanted) break;
    if (object.fileHash !== csgHash) continue;
    if (object.triangleCount[0] === 0) continue;
    const base = object.dataOffset + object.vertexCount * object.stride;
    let lists;
    try {
      lists = [0, 1, 2].map(level =>
        object.triangleCount[level] === 0
          ? null
          : csg.readWords(base + object.triangleOffset[level] * 2, object.triangleCount[level] * 3));
    } catch {
      stats.rejected++;
      continue;
    }
    stats.tested++;
    const [lod0, lod1, lod2] = lists;
    const set0 = new Set(lod0);
    const set1 = lod1 ? new Set(lod1) : new Set();
    const rangeOf = list => {
      if (!list) return null;
      let min = 65535;
      let max = 0;
      for (const value of list) {
        if (value < min) min = value;
        if (value > max) max = value;
      }
      return { min, max };
    };
    const ranges = lists.map(rangeOf);
    if (lod1) {
      stats.lod1present++;
      let subset = true;
      for (let i = 0; i < lod1.length; i += 3) {
        if (!set0.has(lod1[i]) && !set0.has(lod1[i + 1]) && !set0.has(lod1[i + 2])) subset = false;
      }
      if (subset) stats.level1SubsetOfLevel0++;
      if (lod1.every((value, index) => lod0[index] === value)) stats.level1PrefixOfLevel0++;
      const positions = [];
      for (let i = 0; i < lod1.length; i += 3) {
        let found = -1;
        for (let j = 0; j + 2 < lod0.length; j += 3) {
          if (lod0[j] === lod1[i] && lod0[j + 1] === lod1[i + 1] && lod0[j + 2] === lod1[i + 2]) { found = j; break; }
        }
        positions.push(found);
      }
      if (positions.every((p, i) => p >= 0 && (i === 0 || p > positions[i - 1]))) stats.level1Ordered++;
    }
    if (lod2) {
      stats.lod2present++;
      let subset = true;
      for (let i = 0; i < lod2.length; i += 3) {
        if (!set1.has(lod2[i]) && !set1.has(lod2[i + 1]) && !set1.has(lod2[i + 2])) subset = false;
      }
      if (subset) stats.level2SubsetOfLevel1++;
      let subset0 = true;
      for (let i = 0; i < lod2.length; i += 3) {
        if (!set0.has(lod2[i]) && !set0.has(lod2[i + 1]) && !set0.has(lod2[i + 2])) subset0 = false;
      }
      if (subset0) stats.level2SubsetOfLevel0++;
    }
    if (ranges.every(Boolean)) {
      stats.withAllThree += 1;
      if (ranges[1].min === ranges[0].max + 1 && ranges[2].min === ranges[1].max + 1) stats.blocksContiguous++;
      if (ranges[2].max + 1 === object.vertexCount) stats.blocksCoverBuffer++;
      if (ranges[0].min === 0 && ranges[0].max < ranges[1].min && ranges[1].max < ranges[2].min) stats.rangesAscend++;
      if (object.triangleCount[1] < object.triangleCount[2]) stats.level1CoarserThanLevel2++;
    }
    ranges.forEach((range, level) => {
      if (range) vertices[level].push(range.max - range.min + 1);
    });
    if (object.triangleCount[0] > 0) triangles[0].push(object.triangleCount[0]);
    if (object.triangleCount[1] > 0) triangles[1].push(object.triangleCount[1]);
    if (object.triangleCount[2] > 0) triangles[2].push(object.triangleCount[2]);
  }
}

const pct = (value, total) => `${((100 * value) / Math.max(1, total)).toFixed(1)}%`;
const quantile = (values, q) => (values.length ? [...values].sort((a, b) => a - b)[Math.floor(q * (values.length - 1))] : 0);
const median = values => quantile(values, 0.5);
const total = stats.tested;
console.log(`== ${csgName} (file hash 0x${csgHash.toString(16)}) against ${archiveName} ==`);
console.log(`  objects ${total}, skipped ${stats.rejected ?? 0}; LOD1 list on ${stats.lod1present}, LOD2 on ${stats.lod2present}`);
console.log(`  ADOPTED  one contiguous welded vertex block per level: ${stats.blocksContiguous}/${stats.withAllThree} (${pct(stats.blocksContiguous, stats.withAllThree)})`);
console.log('  -- candidates that assume the levels share one vertex buffer --');
console.log(`  LOD1 triangles drawn from LOD0 vertices: ${stats.level1SubsetOfLevel0} (${pct(stats.level1SubsetOfLevel0, stats.lod1present)})`);
console.log(`  LOD2 triangles drawn from LOD1 vertices: ${stats.level2SubsetOfLevel1} (${pct(stats.level2SubsetOfLevel1, stats.lod2present)})`);
console.log(`  LOD2 triangles drawn from LOD0 vertices: ${stats.level2SubsetOfLevel0} (${pct(stats.level2SubsetOfLevel0, stats.lod2present)})`);
console.log(`  LOD1 is a prefix of LOD0: ${stats.level1PrefixOfLevel0} (${pct(stats.level1PrefixOfLevel0, stats.lod1present)})`);
console.log(`  LOD1 keeps LOD0 order: ${stats.level1Ordered} (${pct(stats.level1Ordered, stats.lod1present)})`);
console.log('  -- candidates that assume each level owns a vertex block --');
console.log(`  vertex blocks contiguous (level k starts at level k-1 end + 1): ${stats.blocksContiguous} (${pct(stats.blocksContiguous, stats.withAllThree)})`);
console.log(`  last block ends at vertex_count: ${stats.blocksCoverBuffer} (${pct(stats.blocksCoverBuffer, stats.withAllThree)})`);
console.log(`  blocks ascend from 0: ${stats.rangesAscend} (${pct(stats.rangesAscend, stats.withAllThree)})`);
console.log(`  level 1 has fewer triangles than level 2: ${stats.level1CoarserThanLevel2} (${pct(stats.level1CoarserThanLevel2, stats.withAllThree)})`);
console.log('  -- measured sizes per level (vertices p10/median/p90, triangles median) --');
for (let level = 0; level < 3; level++) {
  console.log(`  level ${level}: vertices ${quantile(vertices[level], 0.1)}/${median(vertices[level])}/${quantile(vertices[level], 0.9)}, triangles ${median(triangles[level])}`);
}
console.log(`  median triangle ratio level1/level0 ${(median(triangles[1]) / Math.max(1, median(triangles[0]))).toFixed(3)}, level2/level0 ${(median(triangles[2]) / Math.max(1, median(triangles[0]))).toFixed(3)}`);
