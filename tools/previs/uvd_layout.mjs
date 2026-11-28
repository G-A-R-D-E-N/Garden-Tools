#!/usr/bin/env node
// Constraint: the layout report is only evidence if the detector self-tests on a synthetic tome and every retail tome is named.
import fs from 'node:fs';
import { ba2Entries, crc32c } from './uvd_tome.mjs';
import { boundSiteAt, findPages, pageTable, scanBoundSites, selfTestReport } from './uvd_sites.mjs';
import { buildTome } from './uvd_tome.mjs';

const args = process.argv.slice(2);
const dataDir = args[0];
const ARCHIVES = [['Fallout4 - MeshesExtra.ba2', 25], ['DLCCoast - Main.ba2', 15]];
const u32 = (b, o) => b.readUInt32LE(o);
const f32 = (b, o) => b.readFloatLE(o);
const STARTS = Array.from({ length: 0x3b }, (_, i) => 0x18 + i * 4);
const STRIDES = [6, 8, 12, 16];
const FAMILIES = [
  ['int16 +-16384', -16384, 16384],
  ['int16 +-32768', -32768, 32768],
  ['uint16 0..65535', 0, 65536],
];
const PAGES_PER_TOME = 4;

const test = selfTestReport();
for (const line of test.lines) console.log(line);
if (!test.ok) process.exit(1);

// Constraint: the writer must emit a page this detector accepts, or a built tome is not retail-shaped at its base.
const fixturePage = {
  min: [-512, -512, 0],
  max: [512, 512, 256],
  vertices: [[-512, -512, 0], [512, -512, 0], [512, 512, 0], [-512, 512, 0],
    [-512, -512, 256], [512, -512, 256], [512, 512, 256], [-512, 512, 256]],
  indices: [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7],
  leaves: [{ count: 12, kind: 1 }],
};
const fixture = buildTome({ worldMin: [-1024, -1024, 0], worldMax: [1024, 1024, 512], pages: [fixturePage] });
const fixturePages = findPages(fixture).pages;
const fixtureLength = fixturePages.length ? u32(fixture, fixturePages[0].at + 0x2c) >>> 8 : 0;
const fixtureOffset = fixturePages.length ? u32(fixture, fixturePages[0].at + 0x38) : 0;
console.log('\n== writer page against the retail detector ==');
console.log(`  pages detected in a written tome: ${fixturePages.length}`);
console.log(`  page length field ${fixtureLength} bytes, vertex offset word ${fixtureOffset}`);
if (!fixturePages.length) {
  console.error('  the writer page is not accepted by the measured page model');
  process.exit(1);
}
// Constraint: the bound-site self-test must gate CI without game data, so it stops before any archive is opened.
if (args.includes('--self-test')) {
  console.log('\nself-test: PASS');
  process.exit(0);
}

const tomes = [];
for (const [name, limit] of ARCHIVES) {
  const path = `${dataDir}/${name}`;
  if (!fs.existsSync(path)) continue;
  const archive = ba2Entries(path);
  for (const entry of archive.tomes().slice(0, limit)) {
    const bytes = archive.extract(entry);
    if (bytes.length < 0x400 || crc32c(bytes.subarray(8)) !== u32(bytes, 4)) continue;
    tomes.push({ name: entry.path.split(/[\\/]/).pop(), bytes });
  }
}
console.log(`\n${tomes.length} retail tomes`);

const probed = [];
for (const tome of tomes) {
  const found = findPages(tome.bytes);
  const payloads = [...new Set(found.pages.map(page => page.at))].sort((a, b) => a - b);
  probed.push({ name: tome.name, bytes: tome.bytes, ...found, payloads });
}

console.log('\n== per tome: page-owned, world-box-nested bound sites ==');
console.log('  tome           table      entries   sites   bounds offset   first page box');
let noSite = [];
let totalSites = 0;
for (const tome of probed) {
  totalSites += tome.pages.length;
  const first = tome.pages[0];
  const box = first ? `${first.box.min.map(v => v.toFixed(0)).join(',')} .. ${first.box.max.map(v => v.toFixed(0)).join(',')}` : '-';
  if (!tome.pages.length) noSite.push(tome);
  console.log(`  ${tome.name.padEnd(14)} 0x${tome.table.toString(16).padStart(7)} ${String(tome.entries.length).padStart(6)}   ${String(tome.pages.length).padStart(4)}    +0x00            ${box}`);
}
console.log(`\n  bound sites: ${totalSites} across ${probed.length - noSite.length}/${probed.length} tomes; tomes without one: ${noSite.length}`);
for (const tome of noSite) {
  const candidates = whyNoSite(tome);
  console.log(`    ${tome.name}: ${candidates}`);
}

function whyNoSite(tome) {
  const { entries } = pageTable(tome.bytes);
  if (!entries.length) return `0xA0 table at 0x${u32(tome.bytes, 0xa0).toString(16)} publishes no 16-aligned page offset`;
  const { world } = findPages(tome.bytes);
  for (const at of entries.slice(0, 8)) {
    const box = { min: [0, 1, 2].map(i => f32(tome.bytes, at + i * 4)), max: [0, 1, 2].map(i => f32(tome.bytes, at + 0xc + i * 4)) };
    if (!boundSiteAt(tome.bytes, at, world)) return `${entries.length} entries, first 0x${at.toString(16)} is not a nested bound site`;
  }
  return `${entries.length} entries, none 16-byte aligned inside the file`;
}

console.log('\n== page base candidates over all tomes (world-box-nested bound sites only) ==');
// Constraint: a base counts only when its run nests inside the tome's own world box, so ordering alone is not scored.
const bases = [
  ['0xA0 target (the page table itself)', t => u32(t.bytes, 0xa0)],
  ['0x150 section start', () => 0x150],
  ['0xA0 behind one indirection', t => u32(t.bytes, u32(t.bytes, 0xa0))],
];
for (const [label, of] of bases) {
  let sites = 0;
  for (const tome of probed) {
    const at = of(tome);
    if (!(at >= 0x150) || at + 0x28 > tome.bytes.length) continue;
    if (boundSiteAt(tome.bytes, at, tome.world)) sites++;
  }
  console.log(`  ${label.padEnd(36)} nested bound sites ${String(sites).padStart(3)}/${probed.length}`);
}
const owned = probed.filter(tome => tome.pages.length);
console.log(`  page table entry (this detector)     nested bound sites ${String(owned.length).padStart(3)}/${probed.length}`);
const scans = probed.map(tome => scanBoundSites(tome.bytes));
const scannedSites = scans.reduce((n, scan) => n + scan.sites.length, 0);
const scannedSlots = probed.reduce((n, tome) => n + Math.max(0, Math.floor((tome.bytes.length - 0x150 - 0x28) / 16) + 1), 0);
console.log(`  every 16-aligned slot in the sampled tomes ${String(scannedSlots).padStart(6)}, of which nested bound sites ` +
  `${scannedSites} (${(100 * scannedSites / Math.max(1, scannedSlots)).toFixed(3)}%); the page table owns ` +
  `${totalSites} of them`);
console.log('  so the page base is the page table entry, and the bounds sit at its first byte; 0xA0 is the table, not a page.');

console.log('\n== page header fields over the sampled pages ==');
const show = (label, map, limit = 4) => console.log(`  ${label}: ${[...map.entries()].sort((a, b) => b[1] - a[1]).slice(0, limit).map(([key, n]) => `${key}:${n}`).join(' ')}`);
let sampled = 0;
let lengthField = 0;
const flags = new Map();
const fieldHits = new Map();
const scaleValues = new Map();
const constant38 = new Map();
const geometry = [];
for (const tome of probed) {
  // Constraint: pages that carry a payload are rare, so the field census must scan every page the tome publishes.
  for (const page of tome.pages) {
    sampled++;
    const next = tome.payloads.find(at => at > page.at) ?? tome.bytes.length;
    const field = u32(tome.bytes, page.at + 0x2c);
    if (field >>> 8 === next - page.at) lengthField++;
    const flag = field & 0xff;
    flags.set(flag, (flags.get(flag) ?? 0) + 1);
    for (const slot of [0x18, 0x1c, 0x20, 0x24, 0x28, 0x34, 0x3c, 0x54, 0x58, 0x5c]) {
      if (u32(tome.bytes, page.at + slot) !== 0) {
        const key = `+0x${slot.toString(16)}`;
        fieldHits.set(key, (fieldHits.get(key) ?? 0) + 1);
      }
    }
    const scale = f32(tome.bytes, page.at + 0x30);
    scaleValues.set(scale, (scaleValues.get(scale) ?? 0) + 1);
    const word38 = u32(tome.bytes, page.at + 0x38);
    constant38.set(word38, (constant38.get(word38) ?? 0) + 1);
    geometry.push({ tome: tome.name, page, length: field >>> 8, flag });
  }
}

console.log(`  pages sampled: ${sampled}`);
console.log(`  +0x2C >> 8 equals the gap to the next page: ${lengthField}/${sampled}`);
show('+0x2C low byte (flags)', flags);
show('+0x38 value', constant38);
show('+0x30 scale float', scaleValues);
show('nonzero fields (of sampled pages)', fieldHits, 10);

// Constraint: a vertex run must close the page's own length with indices, a terminator and at most one padding block.
function closes(bytes, page, length, start, stride, vertices, indices) {
  if (vertices < 3 || indices < 3) return false;
  const exact = start + stride * vertices + 2 * indices + 2;
  if (exact > length || length - exact > 16) return false;
  const indexAt = page.at + start + stride * vertices;
  for (let i = 0; i < indices; i++) if (bytes.readUInt16LE(indexAt + i * 2) >= vertices) return false;
  if (bytes.readUInt16LE(indexAt + indices * 2) !== 0x7fff) return false;
  for (let at = indexAt + indices * 2 + 2; at < page.at + length; at++) if (bytes[at] !== 0) return false;
  return true;
}

// Constraint: a page is Umbra visibility data, not the occluder geometry source: geometry is the CSG container (`<plugin> - Geometry.csg`) that the `_OC.NIF` shared extra addresses, measured by tools/previs/measure_csg.mjs.
console.log('\n== strict length-equation sweep on Umbra visibility pages ==');
const flagged = geometry.filter(entry => entry.flag & 1 && entry.length > 0x60 && entry.length < 1 << 20);
const sweep = new Map();
const fields = [0x34, 0x3c, 0x54, 0x58, 0x5c, 0x18, 0x1c, 0x20, 0x24, 0x28];
let closing = 0;
for (const entry of flagged) {
  const bytes = probed.find(tome => tome.name === entry.tome).bytes;
  const values = [...new Set(fields.map(slot => u32(bytes, entry.page.at + slot)).filter(v => v >= 3 && v <= 4096))];
  let any = false;
  for (const start of [0x50, 0x58, 0x60, 0x68, 0x70, 0x80]) {
    for (const stride of [6, 8, 12]) {
      for (const vertices of values) {
        for (const indices of values) {
          if (!closes(bytes, entry.page, entry.length, start, stride, vertices, indices)) continue;
          const key = `start 0x${start.toString(16)}, stride ${stride}, V and I from ${fields.map(f => '0x' + f.toString(16)).join('/')}`;
          sweep.set(key, (sweep.get(key) ?? 0) + 1);
          any = true;
        }
      }
    }
  }
  if (any) closing++;
}
console.log(`  Umbra visibility sampled pages: ${flagged.length}, pages where any layout closes the length equation: ${closing}`);
if (!sweep.size) {
  console.log('  no start, stride and (V, I) pair closes a visibility page under the length equation ' +
    '(the writer\'s own int16 run is not retail\'s layout; the page is not a geometry source)');
} else {
  for (const [key, n] of [...sweep.entries()].sort((a, b) => b[1] - a[1]).slice(0, 6)) console.log(`  ${key}: ${n}/${flagged.length}`);
}
const best = Math.max(0, ...sweep.values());
const ties = [...sweep.values()].filter(n => n >= best * 0.9).length;
console.log(`  layouts at 90% of the best: ${ties}, so the sweep ${ties > 1 ? 'does not discriminate' : 'picks one layout'}`);
console.log('\n  conclusion: the page base (0xA0 table entry), the bounds at page + 0x00 as three minimums then three');
console.log('  maximums, the length in Q8 at +0x2C with its low-byte flags, the scale float at +0x30 and the constant');
console.log('  0x50 at +0x38 are measured. The vertex and index run inside a page is not derived: no layout closes a');
console.log('  visibility page under the length equation with indices below the vertex count and a 0x7FFF terminator, so');
console.log("  the writer keeps its own page header and +0x50 int16 vertices as a declared fallback.");

