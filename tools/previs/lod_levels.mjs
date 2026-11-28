#!/usr/bin/env node
// Constraint: an authored object stores three triangle lists whose offsets count the words below each level, so the writer must reproduce them.
import fs from 'node:fs';
import path from 'node:path';
import { ba2Entries } from './uvd_tome.mjs';

const dataDir = process.argv[2];
if (!dataDir) {
  console.error('usage: lod_levels.mjs <Data> [archive]');
  process.exit(2);
}
const archiveName = process.argv[3] ?? 'Fallout4 - MeshesExtra.ba2';
const u32 = (b, o) => b.readUInt32LE(o);
const u64 = (b, o) => Number(b.readBigUInt64LE(o));
const HEAD = 28;
const CORE = 32;
const RECORD = 72;
const DESC = 8;

function walkExtra(bytes, base, count) {
  const objects = [];
  let pos = base + HEAD + 8 * count;
  for (let i = 0; i < count; i++) {
    if (pos + CORE + 4 > bytes.length) return null;
    const vertexCount = u32(bytes, pos);
    const lodWord = u32(bytes, pos + 4);
    const triangleCount = [u32(bytes, pos + 8), u32(bytes, pos + 16), u32(bytes, pos + 24)];
    const triangleOffset = [u32(bytes, pos + 12), u32(bytes, pos + 20), u32(bytes, pos + 28)];
    pos += CORE;
    const combined = u32(bytes, pos);
    pos += 4;
    if (combined < 1 || combined > 64) return null;
    if (pos + RECORD * combined + DESC > bytes.length) return null;
    pos += RECORD * combined;
    const stride = (u64(bytes, pos) & 0xf) * 4;
    pos += DESC;
    if (stride < 8 || stride > 64) return null;
    objects.push({ vertexCount, lodWord, triangleCount, triangleOffset, combined });
  }
  return objects;
}

const archive = ba2Entries(path.join(dataDir, archiveName));
const files = archive.entries.filter(entry => /_OC\.nif$/i.test(entry.path));
let extras = 0;
let extrasWithTotals = 0;
let objects = 0;
let lod1 = 0;
let lod2 = 0;
let headerWord = 0;
let wordIsThree = 0;
let offsetsToLast = 0;
let offsetsAlwaysPrefix = 0;
let offsetsNeither = 0;
const ratios = [[], []];

for (const file of files) {
  const bytes = archive.extract(file);
  for (let base = 0; base + HEAD <= bytes.length; base += 4) {
    const count = u32(bytes, base + 24);
    if (count < 1 || count > 64) continue;
    if (base + HEAD + 8 * count > bytes.length) continue;
    let tableOk = true;
    for (let i = 0; i < count; i++) {
      if (u32(bytes, base + HEAD + 8 * i + 4) >= 1 << 30) tableOk = false;
    }
    if (!tableOk) continue;
    const found = walkExtra(bytes, base, count);
    if (found === null || found.length === 0) continue;
    if (found.some(object => object.vertexCount === 0 || object.vertexCount > 200000 || object.lodWord > 3)) continue;
    extras++;
    let verticesSum = 0;
    let trianglesSum = 0;
    for (const object of found) {
      verticesSum += object.vertexCount * object.combined;
      trianglesSum += (object.triangleCount[0] + object.triangleCount[1] + object.triangleCount[2]) * object.combined;
    }
    if (u32(bytes, base + 8) === verticesSum && u32(bytes, base + 12) === trianglesSum) extrasWithTotals++;
    for (const object of found) {
      objects++;
      headerWord += object.count ?? 1;
      void headerWord;
      if (object.lodWord === 3) wordIsThree++;
      if (object.triangleCount[1] > 0) lod1++;
      if (object.triangleCount[2] > 0) lod2++;
      let prefix = 0;
      let last = 0;
      for (let level = 0; level < 3; level++) if (object.triangleCount[level] !== 0) last = level;
      let toLast = true;
      let alwaysPrefix = true;
      for (let level = 0; level < 3; level++) {
        if (object.triangleOffset[level] !== (level <= last ? 3 * prefix : 0)) toLast = false;
        if (object.triangleOffset[level] !== 3 * prefix) alwaysPrefix = false;
        prefix += object.triangleCount[level];
      }
      if (toLast) offsetsToLast++;
      if (alwaysPrefix) offsetsAlwaysPrefix++;
      if (!toLast && !alwaysPrefix) offsetsNeither++;
      if (object.triangleCount[0] > 0) {
        if (object.triangleCount[1] > 0) ratios[0].push(object.triangleCount[1] / object.triangleCount[0]);
        if (object.triangleCount[2] > 0) ratios[1].push(object.triangleCount[2] / object.triangleCount[0]);
      }
    }
  }
}

const median = values => {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.floor(sorted.length / 2)];
};

console.log(`== ${archiveName}: ${files.length} _OC.NIF files ==`);
console.log(`  extras ${extras}, with header totals matching their objects ${extrasWithTotals}`);
console.log(`  objects ${objects}`);
console.log(`  lod word 3 on ${wordIsThree}/${objects}`);
console.log(`  LOD1 triangles present on ${lod1}/${objects}, LOD2 on ${lod2}/${objects}`);
console.log(`  offsets equal 3 x triangles below the level, zero past the last level, on ${offsetsToLast}/${objects}`);
console.log(`  offsets equal 3 x triangles below the level at every level on ${offsetsAlwaysPrefix}/${objects}; neither rule on ${offsetsNeither}/${objects}`);
console.log(`  median LOD1/LOD0 triangle ratio ${median(ratios[0]).toFixed(3)}, LOD2/LOD0 ${median(ratios[1]).toFixed(3)}`);
