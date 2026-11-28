// Constraint: a page-internal vertex/index layout survives only if one parameter set reproduces the page's own length, makes every index land on a vertex it also references, and makes the page's own declared bounds tight to one quantisation step.
import fs from 'node:fs';
import { ba2Entries, buildTome, crc32c } from './uvd_tome.mjs';
import { findPages } from './uvd_sites.mjs';

const u32 = (b, o) => b.readUInt32LE(o);
const f32 = (b, o) => b.readFloatLE(o);
const i16 = (b, o) => b.readInt16LE(o);
const u16 = (b, o) => b.readUInt16LE(o);

// Constraint: the writer quantises to +-16384 over the page extent, so a decode inverts exactly that map.
const k_vertexModels = [
  { label: 'int16 xyz', stride: 6, mode: 'int16' },
  { label: 'int16 xyz + pad', stride: 8, mode: 'int16' },
  { label: 'u16 xyz', stride: 6, mode: 'u16' },
  { label: 'u16 xyz + pad', stride: 8, mode: 'u16' },
  { label: 'float xyz', stride: 12, mode: 'float' },
  { label: 'float xyz + pad', stride: 16, mode: 'float' },
  { label: 'int16 xyz, 20B stride', stride: 20, mode: 'int16' },
  { label: 'int16 xyz, 24B stride', stride: 24, mode: 'int16' },
  { label: 'int16 xyz, 4B stride', stride: 4, mode: 'int16' },
];
const k_wordSlots = [0x18, 0x1c, 0x20, 0x24, 0x28, 0x34, 0x38, 0x3c, 0x40, 0x54, 0x58, 0x5c];
const k_indexWidths = [1, 2, 4];
const k_terminators = [0, 2];

function pageBounds(bytes, at) {
  return {
    min: [0, 1, 2].map(axis => f32(bytes, at + axis * 4)),
    max: [0, 1, 2].map(axis => f32(bytes, at + 0xc + axis * 4)),
  };
}

function orderedBox(box) {
  const values = [...box.min, ...box.max];
  if (!values.every(value => Number.isFinite(value) && Math.abs(value) < 1e7)) return false;
  return [0, 1, 2].every(axis => box.min[axis] < box.max[axis]);
}

function codeAt(bytes, at, index, model) {
  // Constraint: `at` is the first vertex of the candidate run, already offset by the block start.
  const base = at + index * model.stride;
  if (model.mode === 'float') return [f32(bytes, base), f32(bytes, base + 4), f32(bytes, base + 8)];
  if (model.mode === 'u16') return [u16(bytes, base), u16(bytes, base + 2), u16(bytes, base + 4)];
  return [i16(bytes, base), i16(bytes, base + 2), i16(bytes, base + 4)];
}

function dequantise(code, min, max, mode) {
  const extent = max - min;
  if (mode === 'float') return code;
  if (mode === 'u16') return min + (code / 65535) * extent;
  return min + ((code / 16384 + 1) / 2) * extent;
}

// Clause 1: the run must close the block with at most one 16-byte pad and a zero pad, and nothing outside it.
function consumes(blockLength, model, vertices, indices, indexWidth, terminator) {
  const used = model.stride * vertices + indexWidth * indices + terminator;
  if (used > blockLength) return false;
  return blockLength - used < 16;
}

// Clause 1b: whatever the run leaves at the end of the block must be zero padding, so a layout cannot hide slack.
function padIsZero(bytes, at, blockEnd, model, vertices, indices, indexWidth, terminator) {
  const used = model.stride * vertices + indexWidth * indices + terminator;
  for (let o = at + used; o < blockEnd; o++) if (bytes[o] !== 0) return false;
  return true;
}

// Constraint: clause 2: every index addresses a vertex and every vertex is referenced - a byte run is unlikely to do both.
function indicesCover(bytes, at, blockEnd, model, vertices, indices, indexWidth) {
  const start = at + model.stride * vertices;
  if (start + indexWidth * indices > blockEnd) return false;
  const seen = new Uint8Array(vertices);
  for (let i = 0; i < indices; i++) {
    const value = indexWidth === 2 ? u16(bytes, start + i * 2) : u32(bytes, start + i * 4);
    if (value >= vertices) return false;
    seen[value] = 1;
  }
  return seen.every(flag => flag === 1);
}

// Clause 3: the decoded vertices must attain the page's own declared bounds to within two quantisation steps.
function boundsTight(bytes, at, box, model, vertices) {
  const observed = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
  for (let i = 0; i < vertices; i++) {
    const code = codeAt(bytes, at, i, model);
    for (let axis = 0; axis < 3; axis++) {
      const value = dequantise(code[axis], box.min[axis], box.max[axis], model.mode);
      if (value < observed.min[axis]) observed.min[axis] = value;
      if (value > observed.max[axis]) observed.max[axis] = value;
    }
  }
  const tolerance = axis => Math.max(Math.abs(box.max[axis] - box.min[axis]), 1) / 8192;
  return [0, 1, 2].every(axis =>
    Math.abs(observed.min[axis] - box.min[axis]) <= tolerance(axis) &&
    Math.abs(observed.max[axis] - box.max[axis]) <= tolerance(axis));
}

// Constraint: clause 4: whole triangles with real area, so a run of values on one lattice plane is rejected.
function trianglesSound(bytes, at, box, model, vertices, indices, indexWidth) {
  if (indices % 3 !== 0) return false;
  const start = at + model.stride * vertices;
  const points = [];
  const limit = Math.min(vertices, 4096);
  for (let i = 0; i < limit; i++) {
    const code = codeAt(bytes, at, i, model);
    points.push([0, 1, 2].map(axis => dequantise(code[axis], box.min[axis], box.max[axis], model.mode)));
  }
  let real = 0;
  let total = 0;
  const triangles = Math.min(indices / 3, 2048);
  for (let t = 0; t < triangles; t++) {
    const a = indexWidth === 2 ? u16(bytes, start + t * 6) : u32(bytes, start + t * 12);
    const b = indexWidth === 2 ? u16(bytes, start + t * 6 + 2) : u32(bytes, start + t * 12 + 4);
    const c = indexWidth === 2 ? u16(bytes, start + t * 6 + 4) : u32(bytes, start + t * 12 + 8);
    if (a >= points.length || b >= points.length || c >= points.length) return false;
    const u = [0, 1, 2].map(axis => points[b][axis] - points[a][axis]);
    const v = [0, 1, 2].map(axis => points[c][axis] - points[a][axis]);
    const cross = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
    total++;
    if (Math.hypot(...cross) > 1e-6) real++;
  }
  if (process.env.UVD_GEOMETRY_DEBUG) console.log(`    debug tri: real ${real}/${total}, points ${points.length}`);
  return total > 0 && real / total >= 0.95;
}

// Constraint: one parameter set must satisfy every clause on one page before the page counts at all.
function evaluate(bytes, pageAt, blockStart, blockEnd) {
  const at = blockStart;
  if (blockEnd <= at) return null;
  const blockLength = blockEnd - at;
  const box = pageBounds(bytes, pageAt);
  if (!orderedBox(box)) return null;
  const words = new Map(k_wordSlots.map(slot => [slot, u32(bytes, pageAt + slot)]));
  for (const model of k_vertexModels) {
    for (const vSlot of k_wordSlots) {
      const vertices = words.get(vSlot);
      if (vertices < 3 || vertices > 262143) continue;
      for (const iSlot of k_wordSlots) {
        if (iSlot === vSlot) continue;
        const indices = words.get(iSlot);
        if (indices < 3 || indices > 1048575) continue;
        for (const indexWidth of k_indexWidths) {
          for (const terminator of k_terminators) {
            if (!consumes(blockLength, model, vertices, indices, indexWidth, terminator)) continue;
            if (!padIsZero(bytes, at, blockEnd, model, vertices, indices, indexWidth, terminator)) continue;
            if (!indicesCover(bytes, at, blockEnd, model, vertices, indices, indexWidth)) continue;
            if (!boundsTight(bytes, at, box, model, vertices)) continue;
            if (!trianglesSound(bytes, at, box, model, vertices, indices, indexWidth)) continue;
            return { model: model.label, vSlot, iSlot, indexWidth, terminator, vertices, indices };
          }
        }
      }
    }
  }
  return null;
}

function retailPages(dataDir, archiveName, limit) {
  const path = `${dataDir}/${archiveName}`;
  if (!fs.existsSync(path)) return [];
  const archive = ba2Entries(path);
  const out = [];
  for (const entry of archive.tomes().slice(0, limit)) {
    const bytes = archive.extract(entry);
    if (bytes.length < 0x400) continue;
    if (crc32c(bytes.subarray(8)) !== u32(bytes, 4)) continue;
    for (const page of findPages(bytes).pages) {
      const word = u32(bytes, page.at + 0x2c);
      if ((word & 1) === 0) continue;
      const length = word >>> 8;
      // Constraint: a retail page's block ends at its own length or at the first tail table (+0x1c), and its data may start right after the page header (0x50), after the 16-byte record run (0x80), or after that whole run.
      const tail = u32(bytes, page.at + 0x1c);
      const ends = [length];
      if (tail >= 0x50 && tail <= length) ends.push(tail);
      const records = u32(bytes, page.at + 0x54);
      const starts = [...new Set([0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0x80 + 16 * records])]
        .filter(start => start >= 0x50 && start < length);
      out.push({ file: entry.path, bytes, at: page.at, ends, starts });
    }
  }
  return out;
}

function selfTest() {
  const page = {
    min: [0, 0, 0],
    max: [64, 64, 16],
    vertices: [[0, 0, 0], [64, 0, 0], [64, 64, 0], [0, 64, 0], [0, 0, 16], [64, 0, 16], [64, 64, 16], [0, 64, 16]],
    indices: [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7],
    leaves: [{ count: 12, kind: 1 }],
  };
  const bytes = buildTome({ worldMin: [-128, -128, 0], worldMax: [128, 128, 64], pages: [page] });
  const at = findPages(bytes).pages[0].at;
  const length = u32(bytes, at + 0x2c) >>> 8;
  const found = evaluate(bytes, at, at + 0x50, at + length);
  // Negative control: the same header words with random vertex bytes must not pass.
  const scrambled = Buffer.from(bytes);
  let seed = 12345;
  for (let o = 0x50; o < 0x50 + page.vertices.length * 6; o++) {
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    scrambled[at + o] = seed & 0xff;
  }
  const scrambledFound = evaluate(scrambled, at, at + 0x50, at + length);
  return { found, scrambledFound, at, length, bytes };
}

// Control: the discriminator must also recover a layout planted into a real retail page, or a 0/4768 result could be its own blind spot rather than the format's shape.
function plantedControl(dataDir) {
  const pages = retailPages(dataDir, 'Fallout4 - MeshesExtra.ba2', 1);
  if (!pages.length) return { name: 'planted layout on a retail page', ok: false, note: 'no retail page available' };
  const page = pages[0];
  const bytes = Buffer.from(page.bytes);
  const box = pageBounds(bytes, page.at);
  if (!orderedBox(box)) return { name: 'planted layout on a retail page', ok: false, note: 'page bounds unusable' };
  const length = u32(bytes, page.at + 0x2c) >>> 8;
  for (let o = 0x50; o < length; o++) bytes[page.at + o] = 0;
  // Constraint: the run must reach the block end, so the plant fills the page instead of sitting in its first bytes.
  const blockLength = length - 0x50;
  const vertices = Math.min(1500, Math.floor(blockLength / 12));
  const indices = 3 * Math.floor((blockLength - 2 - 6 * vertices) / 6);
  const span = 6 * vertices + 2 * indices + 2;
  if (vertices < 8 || indices < 3 || span > blockLength) {
    return { name: 'planted layout on a retail page', ok: false, note: 'page too small' };
  }
  // Constraint: two vertices must sit on the box corners so the bounds are attained, and the rest must be spread so consecutive triples are not collinear - otherwise the plant tests the degeneracy clause instead of the layout.
  for (let i = 0; i < vertices; i++) {
    for (let axis = 0; axis < 3; axis++) {
      const code = i === 0 ? -16384
        : i === 1 ? 16384
          : ((Math.imul(i + 1, 2654435761) + Math.imul(axis + 1, 40503)) >>> 8) % 32768 - 16384;
      bytes.writeInt16LE(code, page.at + 0x50 + i * 6 + axis * 2);
    }
  }
  for (let t = 0; t < indices; t++) bytes.writeUInt16LE(t % vertices, page.at + 0x50 + 6 * vertices + t * 2);
  bytes.writeUInt16LE(0x7fff, page.at + 0x50 + 6 * vertices + 2 * indices);
  bytes.writeUInt32LE(vertices, page.at + 0x34);
  bytes.writeUInt32LE(indices, page.at + 0x3c);
  const hit = evaluate(bytes, page.at, page.at + 0x50, page.at + length);
  if (process.env.UVD_GEOMETRY_DEBUG && !hit) {
    const model = k_vertexModels[0];
    console.log(`  debug: blockLength ${blockLength}, V ${vertices}, I ${indices}, span ${span}, ` +
      `consumes ${consumes(blockLength, model, vertices, indices, 2, 2)}, ` +
      `pad ${padIsZero(bytes, page.at + 0x50, page.at + length, model, vertices, indices, 2, 2)}, ` +
      `cover ${indicesCover(bytes, page.at + 0x50, page.at + length, model, vertices, indices, 2)}, ` +
      `tight ${boundsTight(bytes, page.at + 0x50, box, model, vertices)}, ` +
      `tri ${trianglesSound(bytes, page.at + 0x50, box, model, vertices, indices, 2)}`);
  }
  return { name: `planted layout on retail page 0x${page.at.toString(16)}`, ok: hit !== null, hit, note: 'clauses rejected' };
}

const dataDir = process.argv[2];
const selfTestOnly = process.argv.includes('--self-test');
const { found, scrambledFound, at, length } = selfTest();
const pass = found !== null && scrambledFound === null;
console.log('== page-local layout discriminator, self-test on a page built by the writer ==');
console.log(`  page 0x${at.toString(16)} length ${length}: declared layout recovered: ` +
  `${found ? `${found.model}, V@+0x${found.vSlot.toString(16)}, I@+0x${found.iSlot.toString(16)}, ` +
    `${found.indexWidth}B indices, ${found.terminator}B terminator, V=${found.vertices}, I=${found.indices}` : 'none'}`);
console.log(`  same page with random vertex bytes: ${scrambledFound ? 'ACCEPTED (discriminator is vacuous)' : 'rejected'}`);
const planted = selfTestOnly ? null : plantedControl(dataDir);
if (planted) console.log(`  ${planted.name}: ${planted.ok ? 'recovered ' + JSON.stringify(planted.hit) : 'not recovered (' + planted.note + ')'}`);
const allPass = pass && (!planted || planted.ok);
console.log(`  self-test: ${allPass ? 'PASS' : 'FAIL'}`);
if (selfTestOnly) process.exit(pass ? 0 : 1);
if (!dataDir) {
  console.error('usage: uvd_geometry.mjs <Data> | --self-test');
  process.exit(pass ? 2 : 1);
}

const pages = [...retailPages(dataDir, 'Fallout4 - MeshesExtra.ba2', 25),
  ...retailPages(dataDir, 'DLCCoast - Main.ba2', 15)];
let accepted = 0;
const combos = new Map();
for (const page of pages) {
  for (const end of page.ends) {
    for (const start of page.starts) {
    const hit = evaluate(page.bytes, page.at, page.at + start, page.at + end);
    if (!hit) continue;
    accepted++;
    const key = `${hit.model}|V@0x${hit.vSlot.toString(16)}|I@0x${hit.iSlot.toString(16)}|` +
      `${hit.indexWidth}B|term${hit.terminator}`;
    combos.set(key, (combos.get(key) ?? 0) + 1);
    }
  }
}
console.log(`\n== retail sweep, ${pages.length} visibility pages ==`);
console.log(`  pages where one parameter set satisfies every clause: ${accepted}/${pages.length}`);
for (const [key, count] of [...combos.entries()].sort((a, b) => b[1] - a[1]).slice(0, 5)) {
  console.log(`    ${key}: ${count}`);
}
if (accepted === 0) {
  console.log('  no page-local vertex/index layout reproduces the page length, covers every index and attains the');
  console.log("  page's own declared bounds, so the page body is not a byte-aligned geometry array");
}
process.exit(allPass ? 0 : 1);

