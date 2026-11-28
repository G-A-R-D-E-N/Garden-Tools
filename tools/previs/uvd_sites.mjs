#!/usr/bin/env node
// Constraint: a retail tome page is owned by the 0xA0 page table and its bounds must nest inside the 0x14 world box.
// Constraint: retail stores three axis minimums then three axis maximums, both for the world box and for a page.
export const k_world_offset = 0x14;
export const k_page_table_offset = 0xa0;
export const k_page_bounds_size = 0x18;

const u32 = (b, o) => b.readUInt32LE(o);
const f32 = (b, o) => b.readFloatLE(o);

export function boxAt(bytes, at) {
  return {
    min: [0, 1, 2].map(i => f32(bytes, at + i * 4)),
    max: [0, 1, 2].map(i => f32(bytes, at + 0xc + i * 4)),
  };
}

export function worldBox(bytes) {
  return boxAt(bytes, k_world_offset);
}

// Constraint: a page for a floor or a wall slice is planar, so one axis at zero thickness is still a bound.
export function orderedBox(box) {
  if (![...box.min, ...box.max].every(x => Number.isFinite(x) && Math.abs(x) < 1e7)) return false;
  if (![0, 1, 2].every(axis => box.min[axis] <= box.max[axis])) return false;
  return [0, 1, 2].some(axis => box.min[axis] < box.max[axis]);
}

export function nestedIn(box, world) {
  return [0, 1, 2].every(axis => box.min[axis] >= world.min[axis] - 1 && box.max[axis] <= world.max[axis] + 1);
}

// Constraint: page offsets are the 16-aligned words the 0xA0 table publishes, so a zero slot is not a page.
export function pageTable(bytes) {
  const table = u32(bytes, k_page_table_offset);
  const entries = [];
  if (table < 0x150 || table + 4 > bytes.length) return { table, entries };
  const limit = Math.min(Math.floor((bytes.length - table) / 4), 1 << 16);
  for (let i = 0; i < limit; i++) {
    const value = u32(bytes, table + i * 4);
    // Constraint: a page needs its bounds and the 0x50 word at +0x38, so the slot must fit both.
    if (value < 0x150 || value % 16 !== 0 || value + 0x50 > bytes.length) continue;
    entries.push(value);
  }
  return { table, entries };
}

// Constraint: six floats are a bound site only if they are ordered AND nest inside the tome's own world box.
export const k_bound_layouts = ['separate', 'interleaved'];

export function boundSiteAt(bytes, at, world) {
  if (at < 0x150 || at + 0x28 > bytes.length) return null;
  // Constraint: retail and the page table store three minimums then three maximums.
  const separate = boxAt(bytes, at);
  if (orderedBox(separate) && nestedIn(separate, world)) return { at, box: separate, layout: 'separate' };
  // Constraint: the writer's own page interleaves minimum/maximum pairs from +0x10.
  const interleaved = {
    min: [0, 1, 2].map(i => f32(bytes, at + 0x10 + i * 8)),
    max: [0, 1, 2].map(i => f32(bytes, at + 0x14 + i * 8)),
  };
  if (orderedBox(interleaved) && nestedIn(interleaved, world)) return { at, box: interleaved, layout: 'interleaved' };
  return null;
}

// Constraint: a run only counts when it nests in the world box, so the scan is the strict form the sweeps score.
export function scanBoundSites(bytes, { from = 0x150, step = 16 } = {}) {
  const world = worldBox(bytes);
  const sites = [];
  for (let at = from; at + 0x28 <= bytes.length; at += step) {
    const site = boundSiteAt(bytes, at, world);
    if (site) sites.push(site);
  }
  return { world, sites };
}

// Constraint: a bound site must be a page payload start whose own six floats nest inside the tome world box.
export function findPages(bytes) {
  const world = worldBox(bytes);
  const { table, entries } = pageTable(bytes);
  const pages = [];
  for (const at of entries) {
    // Constraint: every retail page carries 0x50 at +0x38, so an entry without that word is not a page.
    if (u32(bytes, at + 0x38) !== 0x50) continue;
    const site = boundSiteAt(bytes, at, world);
    if (!site) continue;
    pages.push({ at, box: site.box, layout: site.layout });
  }
  return { table, entries, world, pages };
}

// Constraint: the self-test must know the right answer by construction before any retail tome is read.
function retailTome(world, pages, decoys) {
  const size = 0x400 + pages.length * 0x100;
  const bytes = Buffer.alloc(size);
  bytes.writeUInt32LE(0xd6000012, 0);
  bytes.writeUInt32LE(size, 8);
  bytes.writeFloatLE(512, 0x0c);
  for (let axis = 0; axis < 3; axis++) {
    bytes.writeFloatLE(world.min[axis], k_world_offset + axis * 4);
    bytes.writeFloatLE(world.max[axis], k_world_offset + 0xc + axis * 4);
  }
  const table = 0x150;
  bytes.writeUInt32LE(table, k_page_table_offset);
  const at = 0x200;
  pages.forEach((box, index) => {
    bytes.writeUInt32LE(at + index * 0x100, table + index * 4);
    for (let axis = 0; axis < 3; axis++) {
      bytes.writeFloatLE(box.min[axis], at + index * 0x100 + axis * 4);
      bytes.writeFloatLE(box.max[axis], at + index * 0x100 + 0xc + axis * 4);
    }
    bytes.writeUInt32LE(0x50, at + index * 0x100 + 0x38);
  });
  const base = at + pages.length * 0x100;
  decoys.forEach((box, index) => {
    bytes.writeUInt32LE(base + index * 0x100, table + (pages.length + index) * 4);
    for (let axis = 0; axis < 3; axis++) {
      bytes.writeFloatLE(box.min[axis], base + index * 0x100 + axis * 4);
      bytes.writeFloatLE(box.max[axis], base + index * 0x100 + 0xc + axis * 4);
    }
  });
  return bytes;
}

export function selfTest() {
  const world = { min: [-2048, -2048, 0], max: [2048, 2048, 512] };
  const pages = [
    { min: [-2048, -2048, 0], max: [0, 0, 512] },
    { min: [0, 0, 0], max: [2048, 2048, 512] },
    // Constraint: a floor slice is planar, so a zero-thickness axis must still count as a detected page.
    { min: [-512, -512, 0], max: [512, 512, 0] },
  ];
  const outside = { min: [-9000, -9000, 0], max: [-6000, -6000, 512] };
  const inverted = { min: [100, 100, 0], max: [-100, -100, 512] };
  const bytes = retailTome(world, pages, [outside, inverted]);
  // Constraint: the writer's interleaved run nests too, so the detector must recognize it at a site of its own.
  const writerAt = 0x380;
  [-512, -512, 0].forEach((value, axis) => bytes.writeFloatLE(value, writerAt + 0x10 + axis * 8));
  [64, 64, 32].forEach((value, axis) => bytes.writeFloatLE(value, writerAt + 0x14 + axis * 8));
  // Constraint: a run that is ordered but leaves the world box on one axis must not become a site.
  const halfOutsideAt = 0x480;
  [-512, -512, 0].forEach((value, axis) => bytes.writeFloatLE(value, halfOutsideAt + axis * 4));
  [512, 512, 4096].forEach((value, axis) => bytes.writeFloatLE(value, halfOutsideAt + 0xc + axis * 4));
  const found = findPages(bytes);
  const known = found.pages.map(page => page.at);
  const expected = pages.map((_, index) => 0x200 + index * 0x100);
  const decoyAt = 0x200 + pages.length * 0x100;
  const scanned = scanBoundSites(bytes).sites.map(site => site.at);
  const ok = found.table === 0x150 &&
    found.entries.length === 5 &&
    known.length === 3 &&
    known.every((at, index) => at === expected[index]) &&
    found.pages.every(page => nestedIn(page.box, found.world) && page.at !== decoyAt) &&
    scanned.includes(writerAt) &&
    !scanned.includes(halfOutsideAt);
  return { ok, found, decoyAt, writerAt, halfOutsideAt, bytes };
}

export function selfTestReport() {
  const { ok, found, decoyAt, writerAt, halfOutsideAt, bytes } = selfTest();
  const scanned = scanBoundSites(bytes);
  const lines = [
    '== bound site self-test on a synthetic retail tome ==',
    `  page table 0x${found.table.toString(16)}, entries ${found.entries.length}, detected pages ${found.pages.map(p => '0x' + p.at.toString(16)).join(' ')}`,
    `  decoy page at 0x${decoyAt.toString(16)} outside the world box rejected: ${!found.pages.some(p => p.at === decoyAt)}`,
    `  writer interleaved run at 0x${writerAt.toString(16)} accepted: ${scanned.sites.some(site => site.at === writerAt)}`,
    `  ordered run leaving the world box at 0x${halfOutsideAt.toString(16)} rejected: ${!scanned.sites.some(site => site.at === halfOutsideAt)}`,
    `  nested bound sites in the fixture: ${scanned.sites.length}`,
    `  self-test: ${ok ? 'PASS' : 'FAIL'}`,
  ];
  return { ok, lines };
}

