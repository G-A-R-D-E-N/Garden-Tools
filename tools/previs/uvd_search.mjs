#!/usr/bin/env node
// Constraint: a field model survives only if it predicts the stored bytes or values of every tome, and the scorer must first recover fields from a tome it built itself.
import fs from 'node:fs';
import { ba2Entries, buildTome, crc32c } from './uvd_tome.mjs';
import { boundSiteAt, findPages, worldBox } from './uvd_sites.mjs';

const dataDir = process.argv[2];
const ARCHIVES = [['Fallout4 - MeshesExtra.ba2', 25], ['DLCCoast - Main.ba2', 15]];
const PADDING = 16;

const u32 = (b, o) => b.readUInt32LE(o);
const f32 = (b, o) => b.readFloatLE(o);
const bits = value => {
  const buf = Buffer.alloc(4);
  buf.writeFloatLE(value, 0);
  return buf.readUInt32LE(0);
};
const sameBits = (a, b) => bits(a) === bits(b);

function loadRetail() {
  const tomes = [];
  for (const [name, limit] of ARCHIVES) {
    const path = `${dataDir}/${name}`;
    if (!fs.existsSync(path)) continue;
    const archive = ba2Entries(path);
    for (const entry of archive.tomes().slice(0, limit)) {
      const bytes = archive.extract(entry);
      if (bytes.length < 0x400) continue;
      if (crc32c(bytes.subarray(8)) !== u32(bytes, 4)) continue;
      tomes.push({ name: entry.path.split(/[\\/]/).pop(), archive: name, bytes });
    }
  }
  return tomes;
}

// Constraint: regions are the nine in-file offsets plus 0x150 and EOF, and they must tile the file with no gap or overlap.
function deriveRegions(bytes) {
  const size = bytes.length;
  const pointers = [0x30, 0x3c, 0x80, 0x88, 0x98, 0x9c, 0xa0, 0xa4, 0xa8]
    .map(o => u32(bytes, o))
    .filter(v => v >= 0x150 && v < size && v % 16 === 0);
  const bounds = [...new Set([0x150, ...pointers, size])].sort((a, b) => a - b);
  const regions = [];
  for (let i = 0; i + 1 < bounds.length; i++) {
    regions.push({ start: bounds[i], end: bounds[i + 1], length: bounds[i + 1] - bounds[i] });
  }
  const tiled = regions.length > 0 && regions[0].start === 0x150 && regions[regions.length - 1].end === size &&
    regions.every((r, i) => i === 0 || r.start === regions[i - 1].end);
  return { regions, tiled, pointers };
}

// Constraint: the page payload carries bound sites, so indirection is resolved by the site detector - a run must both be
// ordered and nest inside this tome's world box - rather than by position or by ordering alone.

function pageOf(bytes, regions, retail) {
  // Constraint: on retail tomes the page is the first page the 0xA0 table owns, never the table pointer itself.
  if (retail) {
    const pages = findPages(bytes).pages;
    if (!pages.length) return { page: -1, pageEnd: 0, pageLength: 0 };
    const page = pages[0].at;
    const inside = regions.find(r => page >= r.start && page < r.end);
    const end = inside ? inside.end : bytes.length;
    const next = pages.map(p => p.at).find(at => at > page && at < end) ?? end;
    return { page, pageEnd: next, pageLength: next - page };
  }
  const pointer = u32(bytes, 0xa0);
  const direct = u32(bytes, pointer);
  const candidates = [direct, pointer, ...Array.from({ length: 16 }, (_, i) => u32(bytes, pointer + i * 4))]
    .filter(v => v >= 0x150 && v + 0x50 < bytes.length && v % 16 === 0);
  const world = worldBox(bytes);
  const page = candidates.find(v => boundSiteAt(bytes, v, world)) ?? (candidates.length ? candidates[0] : pointer);
  const inside = regions.find(r => page >= r.start && page < r.end);
  return { page, pageEnd: inside ? inside.end : bytes.length, pageLength: (inside ? inside.end : bytes.length) - page };
}

function shape(bytes, retail = false) {
  const { regions, tiled } = deriveRegions(bytes);
  const { page, pageEnd, pageLength } = pageOf(bytes, regions, retail);
  const countsAt = () => [0x2c, 0x38, 0x7c, 0x8c, 0x90, 0x94, 0xac].map(o => u32(bytes, o));
  if (page < 0) {
    return { size: bytes.length, regions, tiled, page, pageEnd, pageLength, bounds6: [], ordered: false, extent: 0, leafAt: u32(bytes, 0xa4), childAt: u32(bytes, 0xa8), counts: countsAt() };
  }
  // Constraint: retail bounds are three minimums then three maximums at the page base, the writer interleaves pairs at +0x10.
  const bounds6 = retail
    ? [...[0, 1, 2].map(i => f32(bytes, page + i * 4)), ...[0, 1, 2].map(i => f32(bytes, page + 0xc + i * 4))]
    : [0, 1, 2, 3, 4, 5].map(i => f32(bytes, page + 0x10 + i * 4));
  // Constraint: a page counts as located only when its run nests inside the world box, so the non-retail branch scores the
  // same detector the scan uses rather than ordering alone.
  const ordered = retail
    ? [0, 1, 2].every(i => bounds6[i] < bounds6[i + 3])
    : Boolean(boundSiteAt(bytes, page, worldBox(bytes)));
  const spans = retail
    ? [0, 1, 2].map(i => Math.abs(bounds6[i + 3] - bounds6[i]))
    : [0, 1, 2].map(i => Math.abs(bounds6[i * 2 + 1] - bounds6[i * 2]));
  return {
    size: bytes.length, regions, tiled, page, pageEnd, pageLength, bounds6, ordered,
    extent: ordered && spans.every(Number.isFinite) ? Math.max(...spans) : 0,
    leafAt: u32(bytes, 0xa4), childAt: u32(bytes, 0xa8),
    counts: countsAt(),
  };
}

// Constraint: the payload is padded to 16 bytes, so a length equation holds only within one padding quantum.
function lengthModelFits(bytes, slot, w, vSlot, iSlot) {
  const s = slot;
  const V = u32(bytes, s.page + vSlot);
  const I = u32(bytes, s.page + iSlot);
  if (V === 0 || V > 65535 || I === 0 || I > 400000) return false;
  const exact = w * V + 2 * I + 2;
  const payload = s.pageLength - 0x50;
  if (payload < exact || payload >= exact + PADDING) return false;
  const indexStart = s.page + 0x50 + w * V;
  let maxIndex = 0;
  for (let i = 0; i < I; i++) {
    const value = bytes.readUInt16LE(indexStart + i * 2);
    if (value > maxIndex) maxIndex = value;
  }
  if (maxIndex >= V) return false;
  if (bytes.readUInt16LE(indexStart + I * 2) !== 0x7fff) return false;
  for (let p = indexStart + I * 2 + 2; p < s.page + s.pageLength; p++) if (bytes[p] !== 0) return false;
  return true;
}

// Constraint: the page scale is measured at +0x30, so the float score must read that word and no other.
function scoreFloat(tomes, model) {
  return tomes.filter(t => {
    if (t.s.extent <= 0) return false;
    return sameBits(f32(t.bytes, t.s.page + 0x30), model(t.s));
  }).length;
}

function selfTest() {
  const cell = { worldMin: [-512, -512, 0], worldMax: [512, 512, 256] };
  const page = {
    min: [-512, -512, 0], max: [512, 512, 256],
    vertices: [[-512, -512, 0], [512, -512, 0], [512, 512, 0], [-512, 512, 0],
      [-512, -512, 256], [512, -512, 256], [512, 512, 256], [-512, 512, 256]],
    indices: [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7],
    leaves: [{ count: 12, kind: 1 }],
  };
  const bytes = buildTome({ ...cell, pages: [page] });
  const s = shape(bytes, true);
  const known = { V: page.vertices.length, I: page.indices.length, width: 6, extent: 1024 };
  // Constraint: the writer emits the measured page layout, so the self-test must read its counts from +0x34 and +0x3C.
  const recoveredLength = lengthModelFits(bytes, s, 6, 0x34, 0x3c);
  const recoveredFloat = sameBits(f32(bytes, s.page + 0x30), Math.round(s.extent / 64));
  const recoveredTiling = s.tiled;
  const boundsBack = [0, 1, 2].map(i => Math.round(f32(bytes, s.page + i * 4)))
    .concat([0, 1, 2].map(i => Math.round(f32(bytes, s.page + 0xc + i * 4))));
  const boundsExpected = [-512, -512, 0, 512, 512, 256];
  const ok = recoveredLength && recoveredFloat && recoveredTiling &&
    boundsBack.join(',') === boundsExpected.join(',') && s.extent === known.extent;
  console.log('== self-test on a synthetic tome ==');
  console.log(`  size ${bytes.length}, regions tiled: ${recoveredTiling}, extent recovered ${s.extent} (expected ${known.extent})`);
  console.log(`  bounds recovered: ${boundsBack.join(', ')} (expected ${boundsExpected.join(', ')})`);
  console.log(`  length equation w=6, V@0x34, I@0x3C with padding allowance: ${recoveredLength}`);
  console.log(`  scale float at +0x30 equals round(extent/64): ${recoveredFloat}`);
  console.log(`  self-test: ${ok ? 'PASS' : 'FAIL'}`);
  if (!ok) process.exit(1);
  return { bytes, s };
}

selfTest();
// Constraint: the field-model self-test must gate CI without game data, so the retail load is skipped when it is alone.
if (process.argv.slice(2).includes('--self-test')) {
  console.log('retail load skipped');
  process.exit(0);
}

const tomes = loadRetail().map(t => ({ ...t, s: shape(t.bytes, true) }));
if (tomes.length < 20) {
  console.error(`only ${tomes.length} usable tomes; pass the Fallout 4 Data folder`);
  process.exit(2);
}
console.log(`\n${tomes.length} retail tomes: ${[...new Set(tomes.map(t => t.archive))].join(', ')}`);
console.log(`  region tiling holds: ${tomes.filter(t => t.s.tiled).length}/${tomes.length}`);
console.log(`  page region derived: ${tomes.filter(t => t.s.pageLength > 0x50).length}/${tomes.length}`);

const show = (title, rows, n = tomes.length) => {
  console.log(`\n== ${title} ==`);
  for (const { label, fit } of rows.sort((a, b) => b.fit - a.fit).slice(0, 10)) console.log(`  ${label}: ${fit}/${n}`);
};

// Constraint: the payload sweep must score every page the table owns, not just the first page of each tome, and only sites the detector accepts as nested bound sites are eligible.
const pageShapes = [];
for (const tome of tomes) {
  const pages = findPages(tome.bytes).pages;
  for (let index = 0; index < pages.length; index++) {
    const at = pages[index].at;
    const next = pages[index + 1] ? pages[index + 1].at : tome.bytes.length;
    const length = u32(tome.bytes, at + 0x2c) >>> 8;
    if (length < 0x60) continue;
    pageShapes.push({ bytes: tome.bytes, page: at, pageLength: Math.min(length, next - at) });
  }
}
const pageVertexRows = [];
for (const w of [6, 8]) {
  for (let vSlot = 0; vSlot <= 0x4c; vSlot += 4) {
    for (let iSlot = 0; iSlot <= 0x4c; iSlot += 4) {
      if (iSlot === vSlot) continue;
      const fit = pageShapes.filter(page => lengthModelFits(page.bytes, page, w, vSlot, iSlot)).length;
      if (fit > 0) pageVertexRows.push({ label: `${w}B vertices, V@+0x${vSlot.toString(16)}, I@+0x${iSlot.toString(16)}`, fit });
    }
  }
}
show(`payload sweep over every owned nested page site (${pageShapes.length} pages): length equation + index bound + 0x7FFF terminator + zero padding`,
  pageVertexRows.length ? pageVertexRows : [{ label: 'no (V, I) pair reproduced a payload', fit: 0 }], pageShapes.length);

// Constraint: the float candidate must reproduce the stored 32-bit word, so equal floats with different bits are rejected.
const floatModels = [];
for (let k = 1; k <= 10; k++) floatModels.push([`extent/2^${k}`, s => s.extent / 2 ** k]);
for (let k = 0; k <= 4; k++) floatModels.push([`extent*2^${k}`, s => s.extent * 2 ** k]);
floatModels.push(['1/extent', s => 1 / s.extent]);
floatModels.push(['round(extent/64)', s => Math.round(s.extent / 64)]);
show('page scale float at +0x30, bit-exact', floatModels.map(([label, model]) => ({ label, fit: scoreFloat(tomes, model) })));

// Constraint: a descriptor word survives only if a page quantity predicts its exact value on every tome.
const quantityModels = [
  ['page length', s => s.pageLength],
  ['leaf region', s => s.childAt - s.leafAt],
  ['child region', s => s.size - s.childAt],
  ['count 0x38', s => s.counts[1]],
  ['count 0x7C', s => s.counts[2]],
  ['count 0x8C', s => s.counts[3]],
  ['count 0x90', s => s.counts[4]],
  ['count 0x94', s => s.counts[5]],
  ['extent', s => Math.round(s.extent)],
];
const wordRows = [];
for (const slot of [0x28, 0x2c, 0x30, 0x34, 0x40, 0x44]) {
  for (const [label, model] of quantityModels) {
    wordRows.push({ label: `+0x${slot.toString(16)} = ${label}`, fit: tomes.filter(t => model(t.s) > 0 && u32(t.bytes, t.s.page + slot) === model(t.s)).length });
  }
}
show('descriptor words: slot x page quantity', wordRows);

// Constraint: a vertex layout survives only if the count pair reproduces the payload length and the indices stay below V.
const vertexRows = [];
for (const w of [6, 8]) {
  for (let vSlot = 0; vSlot <= 0x4c; vSlot += 4) {
    for (let iSlot = 0; iSlot <= 0x4c; iSlot += 4) {
      if (iSlot === vSlot) continue;
      const fit = tomes.filter(t => lengthModelFits(t.bytes, t.s, w, vSlot, iSlot)).length;
      if (fit > 0) vertexRows.push({ label: `${w}B vertices, V@+0x${vSlot.toString(16)}, I@+0x${iSlot.toString(16)}`, fit });
    }
  }
}
// Constraint: the run scored here is the writer's own page layout.
show('writer vertex run (fallback; retail pages are visibility, not geometry): length equation + index bound + 0x7FFF terminator + zero padding', vertexRows.length ? vertexRows : [{ label: 'no (V, I) pair reproduced the payload', fit: 0 }], tomes.length);

// Constraint: leaf rows must satisfy an ordering, offset or count predicate that predicts every row.
const leafRows = [];
for (const stride of [16, 20]) {
  leafRows.push({ label: `${stride}B rows: strictly ascending ids`, fit: tomes.filter(t => {
    const n = Math.floor((t.s.childAt - t.s.leafAt) / stride);
    if (n <= 0) return false;
    for (let i = 1; i < n; i++) if (u32(t.bytes, t.s.leafAt + i * stride) <= u32(t.bytes, t.s.leafAt + (i - 1) * stride)) return false;
    return true;
  }).length });
  leafRows.push({ label: `${stride}B rows: offsets in-file`, fit: tomes.filter(t => {
    const n = Math.floor((t.s.childAt - t.s.leafAt) / stride);
    if (n <= 0) return false;
    for (let i = 0; i < n; i++) {
      for (const o of [4, 8]) {
        const v = u32(t.bytes, t.s.leafAt + i * stride + o);
        if (v !== 0 && (v % 16 !== 0 || v >= t.s.size)) return false;
      }
    }
    return true;
  }).length });
}
show('leaf region: row predicates', leafRows);

const exact = tomes.filter(t => t.s.tiled && t.s.regions.every(r => r.length % 16 === 0) &&
  crc32c(t.bytes.subarray(8)) === u32(t.bytes, 4)).length;
console.log(`\n== exact-decode pass (tiling + 16B regions + body CRC): ${exact}/${tomes.length} ==`);

