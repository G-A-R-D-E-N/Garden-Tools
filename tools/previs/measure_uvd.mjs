#!/usr/bin/env node
// Constraint: a tome's offsets must be 16-aligned section starts inside the file, or nothing can be written into it.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { findPages } from './uvd_sites.mjs';

const u32 = (b, o) => b.readUInt32LE(o);
const f32 = (b, o) => b.readFloatLE(o);

// Constraint: source of truth: occluder geometry is the CSG stream, not a tome page.
const k_geometry_source = 'csg';

function ba2Entries(file) {
  const b = fs.readFileSync(file);
  if (b.subarray(0, 4).toString('ascii') !== 'BTDX') throw new Error('not a BA2: ' + file);
  const tag = b.subarray(8, 12).toString('ascii');
  if (tag !== 'GNRL') throw new Error('not a general archive: ' + tag);
  const count = u32(b, 12);
  const nameTable = Number(b.readBigUInt64LE(16));
  let pos = 24;
  const entries = [];
  for (let i = 0; i < count; i++) {
    const chunkCount = b[pos + 13];
    pos += 16;
    const chunks = [];
    for (let c = 0; c < chunkCount; c++) {
      chunks.push({ offset: Number(b.readBigUInt64LE(pos)), packed: u32(b, pos + 8), unpacked: u32(b, pos + 12) });
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
      const parts = entry.chunks.map(chunk => {
        const raw = b.subarray(chunk.offset, chunk.offset + (chunk.packed || chunk.unpacked));
        if (chunk.packed === 0 || chunk.packed === chunk.unpacked) return Buffer.from(raw);
        try {
          return zlib.inflateRawSync(raw);
        } catch {
          return zlib.inflateSync(raw);
        }
      });
      return Buffer.concat(parts);
    },
  };
}

function crc32c(data) {
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

const offsets = { sectionA: 0x30, sectionB: 0x3c, sectionC: 0x80, sectionD: 0x88, pageList: 0x98,
  indexList: 0x9c, pageTable: 0xa0, triangleList: 0xa4, childList: 0xa8 };

function measure(file, bytes) {
  const size = bytes.length;
  const report = { file: path.basename(file), size, aligned: size % 16 === 0,
    magic: u32(bytes, 0) === 0xd6000012, sizeField: u32(bytes, 8) === size,
    crc: crc32c(bytes.subarray(8)) === u32(bytes, 4) };
  report.banner = bytes.subarray(0xbc, 0xbc + 96).toString('latin1').split('\0')[0];
  const settings = {};
  for (const match of report.banner.matchAll(/([A-Z]{1,2}) (-?[0-9.]+)/g)) settings[match[1]] = Number(match[2]);
  report.settings = settings;
  report.tileSizeMatch = settings.T === f32(bytes, 0xc);
  report.offsets = {};
  let allValid = true;
  for (const [name, at] of Object.entries(offsets)) {
    const value = u32(bytes, at);
    const valid = value % 16 === 0 && value < size;
    if (!valid) allValid = false;
    report.offsets[name] = { value, valid };
  }
  report.everyOffsetAlignedAndInside = allValid;
  report.pageCounts = [u32(bytes, 0x38), u32(bytes, 0x7c), u32(bytes, 0x8c), u32(bytes, 0x90), u32(bytes, 0x94)];
  report.triangleCount = u32(bytes, 0xac);
  report.page = decodeFirstPage(bytes);
  report.lists = decodeRecordLists(bytes, report.offsets.triangleList.value, report.offsets.childList.value);
  return report;
}

// Constraint: the five words at +0x18..+0x28 are only decodable by what they point at, so each is scored as an
// in-page offset, a count, or neither, on Umbra visibility pages only (never a geometry source).
const k_page_slots = [0x18, 0x1c, 0x20, 0x24, 0x28];

function emptySlotModel() {
  return {
    pages: 0,
    aligned: k_page_slots.map(() => 0),
    inside: k_page_slots.map(() => 0),
    ordered: 0,
    absent28: 0,
    with24: 0,
    section16: 0,
    section8: 0,
    sample: null,
  };
}

function scorePageSlots(bytes, pages, model) {
  for (const page of pages) {
    const at = page.at;
    const lengthWord = u32(bytes, at + 0x2c);
    if ((lengthWord & 1) === 0) continue;
    const length = lengthWord >>> 8;
    const words = k_page_slots.map(offset => u32(bytes, at + offset));
    model.pages++;
    words.forEach((value, index) => {
      if (value % 16 === 0) model.aligned[index]++;
      if (value >= 0x50 && value < length) model.inside[index]++;
    });
    const [w18, a, c, n24, b] = words;
    void w18;
    if (b > 0) {
      if (a <= b && b <= c && c <= length) model.ordered++;
      if ((b - a) % 16 === 0) model.section16++;
      if ((c - b) % 8 === 0) model.section8++;
    } else {
      model.absent28++;
    }
    if (n24 > 0) model.with24++;
    if (!model.sample) model.sample = { at, length, words };
  }
}

function printSlotModel(model) {
  if (model.pages === 0) {
    console.log('  page slots: no Umbra visibility page in the sample');
    return;
  }
  const label = index => `+0x${k_page_slots[index].toString(16)}`;
  console.log(`  page slots over ${model.pages} Umbra visibility pages:`);
  k_page_slots.forEach((_, index) => {
    console.log(`    ${label(index)}: 16-aligned ${model.aligned[index]}/${model.pages}, ` +
      `inside the page ${model.inside[index]}/${model.pages}`);
  });
  console.log(`    ordering +0x1c <= +0x28 <= +0x20 <= length: ${model.ordered}/${model.pages - model.absent28}` +
    ` (of ${model.pages} pages, ${model.absent28} leave +0x28 = 0)`);
  console.log(`    [0x1c,0x28) a whole number of 16-byte records: ${model.section16}/${model.pages - model.absent28}`);
  console.log(`    [0x28,0x20) a whole number of 8-byte records: ${model.section8}/${model.pages - model.absent28}`);
  console.log(`    +0x24 set on the pages that carry +0x28: ${model.with24}/${model.ordered}`);
  if (model.sample) {
    console.log(`    first visibility page 0x${model.sample.at.toString(16)} length 0x${model.sample.length.toString(16)} ` +
      `slots ${model.sample.words.map(value => '0x' + value.toString(16)).join(' ')}`);
  }
}

// Constraint: the pool is only a vertex run if it is a vertex run — a byte-per-axis stream dequantised through the 8-byte table, a zero-terminated index run, or a varint delta stream each make a testable prediction, so each is scored and printed.
const k_tail_bytes = 1 << 12;

function emptyTailModel() {
  return {
    pages: 0, poolMod3: 0, poolPerRecord: 0, sortedPages: 0, runs: 0, sortedRuns: 0,
    varintPages: 0, varintTriples: 0, recordRun: 0, absentTail: 0, payloadBytes: 0, payloadEntropy: 0, payloadPages: 0,
    poolEntropy: 0, recordsEntropy: 0, records: 0,
  };
}

function byteEntropy(slice) {
  if (!slice.length) return 0;
  const counts = new Array(256).fill(0);
  for (const value of slice) counts[value]++;
  let h = 0;
  for (const count of counts) if (count) { const p = count / slice.length; h -= p * Math.log2(p); }
  return h;
}

function scoreTailLayout(bytes, pages, model) {
  for (const page of pages) {
    const at = page.at;
    const lengthWord = u32(bytes, at + 0x2c);
    if ((lengthWord & 1) === 0) continue;
    const length = lengthWord >>> 8;
    const a = u32(bytes, at + 0x1c);
    const b = u32(bytes, at + 0x28);
    const c = u32(bytes, at + 0x20);
    const count = u32(bytes, at + 0x54);
    if (count > 0 && a === 0x80 + 16 * count) {
      model.recordRun++;
      model.records += count;
      model.recordsEntropy += byteEntropy(bytes.subarray(at + 0x80, at + a));
    }
    if (b === 0 || !(a <= b && b <= c && c <= length)) {
      if (b === 0) model.absentTail++;
      continue;
    }
    model.pages++;    const pool = bytes.subarray(at + c, at + length);
    const poolLength = length - c;
    if (poolLength % 3 === 0) model.poolMod3++;
    if ((c - b) / 8 === poolLength / 3) model.poolPerRecord++;
    model.poolEntropy += byteEntropy(pool);
    let runLength = 0, previous = -1, pageSorted = true;
    for (const value of pool) {
      if (value === 0) {
        if (runLength > 0) { model.runs++; if (previous !== -1) {} }
        runLength = 0; previous = -1; continue;
      }
      if (previous !== -1 && value < previous) pageSorted = false;
      previous = value; runLength++;
    }
    if (runLength > 0) model.runs++;
    if (pageSorted) model.sortedPages++;
    let cursor = 0, values = 0, clean = true;
    while (cursor < poolLength) {
      let width = 0, closed = false;
      while (cursor < poolLength && width < 5) {
        const byte = pool[cursor++]; width++;
        if ((byte & 0x80) === 0) { closed = true; break; }
      }
      if (!closed) { clean = false; break; }
      values++;
    }
    if (clean) { model.varintPages++; if (values % 3 === 0) model.varintTriples++; }
    const payloadEnd = u32(bytes, at + 0x3c);
    if (payloadEnd > 0x80 && payloadEnd <= a) {
      model.payloadBytes += payloadEnd - 0x80;
      model.payloadEntropy += byteEntropy(bytes.subarray(at + 0x80, at + payloadEnd));
      model.payloadPages++;
    }
  }
}

function printTailLayout(model) {
  if (model.pages === 0) {
    console.log('  page tail: no Umbra visibility page with a tail table in the sample');
    return;
  }
  const { pages } = model;
  console.log(`  page tail over ${pages} pages with a tail table:`);
  console.log(`    pool as one byte per axis dequantised through the 8-byte table: pool % 3 == 0 ${model.poolMod3}/${pages}, ` +
    `8-byte records == pool / 3 ${model.poolPerRecord}/${pages}`);
  console.log(`    pool as zero-terminated runs: every run nondecreasing on ${model.sortedPages}/${pages} pages`);
  console.log(`    pool as a LEB128 varint stream: every value closes inside 5 bytes on ${model.varintPages}/${pages} pages ` +
    `(value count divisible by 3 on ${model.varintTriples})`);
  console.log(`    pool entropy ${(model.poolEntropy / pages).toFixed(2)} bits/byte: not compressed and not a coordinate lattice`);
  console.log(`    payload + 16 * +0x54 records close on +0x1c: ${model.recordRun}/${pages + model.absentTail} pages, ` +
    `payload ${(model.payloadBytes / Math.max(model.payloadPages, 1)).toFixed(0)}B mean at entropy ${(model.payloadEntropy / Math.max(model.payloadPages, 1)).toFixed(2)}, ` +
    `that record run at entropy ${(model.recordsEntropy / Math.max(model.recordRun, 1)).toFixed(2)}`);
}

// Constraint: each page region is only a vertex array if a fixed element width consumes it exactly, so the record fields, the bounds array packing and the residue are each scored against that prediction page by page.
function emptyRegionModel() {
  return {
    pages: 0, records: 0, offsetish: 0, offsetPages: 0, countPages: 0, ascendingPages: 0,
    tomes: 0, covered: 0, fileBytes: 0, largestGap: 0,
    dwords: 0, lowHalfZero: 0, exact256: 0, orderedPairs: 0, pairs: 0,
    residuePages: 0, residueMod3: 0, residueBlocks: 0, residueBlockRepeats: 0, patternBytes: 0, residueBytes: 0,
    bitpackPages: [],
  };
}

const k_patternBytes = new Set([0x00, 0x55, 0xaa, 0xff, 0x1f, 0x0f, 0x80, 0xbf, 0xdf, 0x7f]);

function scorePageRegions(bytes, pages, model) {
  for (const page of pages) {
    const at = page.at;
    const lengthWord = u32(bytes, at + 0x2c);
    if ((lengthWord & 1) === 0) continue;
    const length = lengthWord >>> 8;
    const a = u32(bytes, at + 0x1c);
    const b = u32(bytes, at + 0x28);
    const c = u32(bytes, at + 0x20);
    const count = u32(bytes, at + 0x54);
    if (b === 0 || count === 0 || 0x80 + 16 * count > a) continue;
    model.pages++;
    // Record fields: an index or offset table would land inside the page on every record and index a table exactly.
    let offsetFields = 0;
    let ascending = true;
    let previous = -1;
    let maxFirst = 0;
    for (let r = 0; r < count; r++) {
      const base = at + 0x80 + r * 16;
      model.records++;
      for (let i = 0; i < 8; i++) {
        const value = bytes.readUInt16LE(base + i * 2);
        if (value < length) offsetFields++;
        if (i === 0 && value > maxFirst) maxFirst = value;
      }
      const id = bytes.readUInt16LE(base);
      if (id <= previous) ascending = false;
      previous = id;
    }
    if (offsetFields === count * 8) model.offsetPages++;
    if (ascending) model.ascendingPages++;
    const t1 = (b - a) / 16;
    const t2 = (c - b) / 8;
    if ([t1, t2, u32(bytes, at + 0x24), length - c, count].some(value => maxFirst + 1 === value)) model.countPages++;
    for (let o = b; o + 4 <= c; o += 4) {
      const bits = u32(bytes, at + o);
      const value = f32(bytes, at + o);
      model.dwords++;
      if ((bits & 0xffff) === 0) model.lowHalfZero++;
      const k = value / 256;
      if (Number.isFinite(k) && Math.abs(k - Math.round(k)) < 1e-9 && Math.abs(value) < 1e6) model.exact256++;
    }
    for (let o = b; o + 8 <= c; o += 8) {
      const lo = f32(bytes, at + o);
      const hi = f32(bytes, at + o + 4);
      if (Number.isFinite(lo) && Number.isFinite(hi)) { model.pairs++; if (lo <= hi) model.orderedPairs++; }
    }
    const start = 0x80 + 16 * count;
    const residue = a - start;
    if (residue <= 0) continue;
    model.residuePages++;
    if (residue % 3 === 0) model.residueMod3++;
    for (const width of [8, 10, 12, 13, 16, 20]) {
      if ((residue * 8) % (3 * width) === 0) model.bitpackPages.push(width);
    }
    for (let o = start + 16; o + 16 <= a; o += 16) {
      model.residueBlocks++;
      if (bytes.subarray(at + o + 4, at + o + 16).equals(bytes.subarray(at + o - 12, at + o))) model.residueBlockRepeats++;
    }
    for (let o = start; o < a; o++) {
      model.residueBytes++;
      if (k_patternBytes.has(bytes[at + o])) model.patternBytes++;
    }
  }
}

// Constraint: if occluder geometry lived in a shared block, it would show up as a large region no page owns.
function scoreFileCoverage(bytes, model) {
  const { pages } = findPages(bytes);
  const starts = pages.map(page => page.at).sort((x, y) => x - y);
  let end = 0x150;
  let covered = 0;
  for (const at of starts) {
    const lengthWord = u32(bytes, at + 0x2c);
    const length = (lengthWord & 1) ? (lengthWord >>> 8) : 0;
    if (at > end) model.largestGap = Math.max(model.largestGap, at - end);
    end = Math.max(end, at + length);
    covered += length;
  }
  model.largestGap = Math.max(model.largestGap, bytes.length - end);
  model.covered += covered;
  model.fileBytes += bytes.length;
  model.tomes++;
}

function printPageRegions(model) {
  if (model.pages === 0) {
    console.log('  page regions: no page with a record run in the sample');
    return;
  }
  const pct = (n, d) => `${n}/${d} (${(100 * n / Math.max(d, 1)).toFixed(1)}%)`;
  console.log(`  page regions over ${model.pages} pages, ${model.records} records:`);
  console.log(`    16-byte record fields: every field an in-page offset on ${model.offsetPages}/${model.pages} pages; ` +
    `first u16 ascending ${model.ascendingPages}/${model.pages}; a field one past a table size ${model.countPages}/${model.pages}`);
  console.log(`    bounds array packing: low u16 zero ${pct(model.lowHalfZero, model.dwords)}, ` +
    `exact multiples of 256 ${pct(model.exact256, model.dwords)}, ordered pairs ${pct(model.orderedPairs, model.pairs)}`);
  console.log(`    residue: %3 == 0 ${model.residueMod3}/${model.residuePages}, ` +
    `16-byte blocks repeating the previous one ${pct(model.residueBlockRepeats, model.residueBlocks)}, ` +
    `bytes from field-extreme patterns ${pct(model.patternBytes, model.residueBytes)}`);
  const widths = [8, 10, 12, 13, 16, 20];
  console.log(`    residue as fixed-width xyz: pages consumed exactly per width ` +
    widths.map(w => `${w}b:${model.bitpackPages.filter(x => x === w).length}`).join(' ') +
    ` of ${model.residuePages}`);
  if (model.tomes) {
    console.log(`    page regions own ${(100 * model.covered / model.fileBytes).toFixed(1)}% of the ${model.tomes} tomes read; ` +
      `largest region no page owns ${model.largestGap} bytes`);
  }
}

// Constraint: the region at 0xA0 is the page table, so a page comes from the detector and never from that offset.
function decodeFirstPage(bytes) {
  const table = u32(bytes, 0xa0);
  const words = [0, 4, 8, 12].map(o => u32(bytes, table + o));
  const { pages } = findPages(bytes);
  const first = pages[0];
  const bbox = first ? [...first.box.min, ...first.box.max] : [];
  const finiteBounds = bbox.length === 6 && bbox.every(v => Number.isFinite(v) && Math.abs(v) < 1e6);
  return { pageTable: table, pageBase: first ? first.at : 0, words, offsetWords: pages.length, firstOffset: first ? first.at : 0, bbox, finiteBounds, ordered: !!first };
}

// Constraint: the leaf table at 0xA4 holds ascending ids; if the ids stop ascending the region is not that table.
function decodeRecordLists(bytes, triangleList, childList) {
  const rows = [];
  let previous = -1;
  for (let o = triangleList; o + 16 <= childList && rows.length < 4096; o += 16) {
    const id = u32(bytes, o);
    const offset = u32(bytes, o + 4);
    if (id <= previous || offset % 16 !== 0 || offset >= bytes.length) break;
    previous = id;
    rows.push({ id, offset, a: u32(bytes, o + 8), b: u32(bytes, o + 12) });
  }
  const span = childList - triangleList;
  const stride = rows.length && span % rows.length === 0 ? span / rows.length : 0;
  return { triangleList, childList, rows: rows.length, stride, span,
    ascending: rows.length > 0 && rows.every((r, i) => i === 0 || r.id > rows[i - 1].id),
    sample: rows.slice(0, 4) };
}

const ba2Path = process.argv[2];
if (!ba2Path) {
  console.error('usage: measure_uvd.mjs <archive.ba2> [max]');
  process.exit(2);
}
const max = Number(process.argv[3] || 25);
const archive = ba2Entries(ba2Path);
const tomes = archive.entries.filter(entry => entry.path.toLowerCase().endsWith('.uvd'));
console.log(`${path.basename(ba2Path)}: ${archive.entries.length} entries, ${tomes.length} tomes`);
console.log(`  occluder geometry source: ${k_geometry_source} (a tome page is visibility, never geometry; ` +
  'score geometry with tools/previs/measure_csg.mjs)');

const reports = [];
const slotModel = emptySlotModel();
const tailModel = emptyTailModel();
const regionModel = emptyRegionModel();
for (const entry of tomes.slice(0, max)) {
  const bytes = archive.extract(entry);
  reports.push(measure(entry.path.replace(/\\/g, '/').split('/').pop(), bytes));
  scorePageSlots(bytes, findPages(bytes).pages, slotModel);
  scoreTailLayout(bytes, findPages(bytes).pages, tailModel);
  scorePageRegions(bytes, findPages(bytes).pages, regionModel);
  scoreFileCoverage(bytes, regionModel);
}
const ok = key => reports.filter(r => r[key]).length;
console.log(`  magic ok: ${reports.filter(r => r.magic).length}/${reports.length}`);
console.log(`  size field equals file size: ${ok('sizeField')}/${reports.length}`);
console.log(`  size 16-aligned: ${ok('aligned')}/${reports.length}`);
console.log(`  CRC-32C over [8,end) matches: ${ok('crc')}/${reports.length}`);
console.log(`  banner T equals header float@0xC: ${ok('tileSizeMatch')}/${reports.length}`);
console.log(`  every section offset 16-aligned and inside: ${ok('everyOffsetAlignedAndInside')}/${reports.length}`);
for (const report of reports.slice(0, 6)) {
  console.log(`  ${report.file} ${report.size}B pages=${report.pageCounts.join('/')} triangles=${report.triangleCount}`);
  console.log(`    banner: ${report.banner.split(' - ')[0]}`);
  console.log(`    offsets: ${Object.entries(report.offsets).map(([k, v]) => `${k}=0x${v.value.toString(16)}`).join(' ')}`);
}
console.log(`  pages owned by the 0xA0 page table: ${reports.filter(r => r.page.offsetWords > 0).length}/${reports.length}`);
console.log(`  first page bounds nested in the world box: ${reports.filter(r => r.page.ordered).length}/${reports.length}`);
console.log(`  leaf table ids ascending: ${reports.filter(r => r.lists.ascending).length}/${reports.length}`);
for (const report of reports.slice(0, 6)) {
  const page = report.page;
  console.log(`    ${report.file} pageTable@0x${page.pageTable.toString(16)} words=[${page.words.join(',')}] firstPage@0x${page.pageBase.toString(16)} pages=${page.offsetWords} bbox=[${page.bbox.map(v => v.toFixed(1)).join(', ')}]`);
  const list = report.lists;
  console.log(`      leafTable@0x${list.triangleList.toString(16)} rows=${list.rows} stride=${list.stride} span=${list.span}`);
  console.log(`        ${list.sample.map(r => `{${r.id}->0x${r.offset.toString(16)},${r.a},${r.b}}`).join(' ')}`);
}
printSlotModel(slotModel);
printTailLayout(tailModel);
printPageRegions(regionModel);
const banners = new Set(reports.map(r => r.banner.replace(/- [0-9.]+ /, '- <v> ')));
console.log(`  distinct banner settings strings: ${banners.size}`);
for (const banner of [...banners].slice(0, 4)) console.log(`    ${banner}`);

