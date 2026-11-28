#!/usr/bin/env node
// Constraint: the proof fails if the generated tome cannot be read back as the geometry the generator was handed.
import fs from 'node:fs';
import { ba2Entries, buildTome, checkTome, parseTome, readGeometry } from './uvd_tome.mjs';

const dataDir = process.argv[2];
const report = [];

function hexdumpDiff(generated, vanilla, label) {
  const lines = [];
  const length = Math.min(generated.length, vanilla.length);
  let differing = 0;
  for (let i = 0; i < length; i++) {
    if (generated[i] !== vanilla[i]) {
      differing++;
      lines.push({ offset: i, generated: generated[i], vanilla: vanilla[i] });
    }
  }
  report.push(`${label}: ${generated.length}B generated vs ${vanilla.length}B vanilla, ${differing} differing bytes over the common prefix`);
  const runs = [];
  for (const line of lines) {
    const last = runs[runs.length - 1];
    if (last && line.offset === last.to + 1) { last.to = line.offset; last.count++; }
    else runs.push({ from: line.offset, to: line.offset, count: 1 });
  }
  for (const run of runs.slice(0, 30)) {
    report.push(`  0x${run.from.toString(16).padStart(4, '0')}..0x${run.to.toString(16).padStart(4, '0')}  ${run.count} byte(s)`);
  }
  if (runs.length > 30) report.push(`  ... ${runs.length - 30} more runs`);
  return runs;
}

function describeRun(run, generated, vanilla) {
  const generatedWord = generated.readUInt32LE(run.from);
  const vanillaWord = vanilla.readUInt32LE(run.from);
  const fields = [];
  const named = [
    [0, 'magic'], [4, 'crc'], [8, 'size'], [0x0c, 'tile'], [0x10, 'tileBias'],
    [0x14, 'worldMinX'], [0x18, 'worldMaxX'], [0x1c, 'worldMinY'], [0x20, 'worldMaxY'],
    [0x24, 'worldMinZ'], [0x28, 'worldMaxZ'], [0x2c, 'sectionWords'], [0x30, 's1'], [0x38, 'countB'],
    [0x3c, 's2'], [0x80, 's3'], [0x88, 's4'], [0x98, 's5'], [0x9c, 's6'], [0xa0, 'pageRegion'],
    [0xa4, 'leafTable'], [0xa8, 'childBlock'], [0xac, 'leafCount'],
  ];
  for (const [offset, name] of named) {
    if (run.from <= offset && offset <= run.to) fields.push(`${name}@0x${offset.toString(16)}`);
  }
  if (fields.length) return fields.join(', ');
  const bannerEnd = vanilla.indexOf(0, 0xbc);
  if (run.from >= 0xbc && run.to < bannerEnd) return 'banner text';
  if (vanillaWord === 0x7f7fffff || generatedWord === 0x7f7fffff) return 'descriptor sentinel';
  return 'page payload or leaf/child record';
}

// Constraint: a single-page cell with 8 vertices and 2 leaves must survive the writer.
const cell = {
  worldMin: [-4096, -1664, 20480],
  worldMax: [4096, 6656, 28672],
};
const page = {
  min: [-512, -512, 0],
  max: [512, 512, 256],
  vertices: [
    [-512, -512, 0], [512, -512, 0], [512, 512, 0], [-512, 512, 0],
    [-512, -512, 256], [512, -512, 256], [512, 512, 256], [-512, 512, 256],
  ],
  indices: [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7],
  leaves: [{ count: 12, kind: 1 }, { count: 12, kind: 2 }],
};

const generated = buildTome({ ...cell, pages: [page] });
fs.mkdirSync('.probe/uvd-proof', { recursive: true });
fs.writeFileSync('.probe/uvd-proof/generated.uvd', generated);

const { issues, parsed } = checkTome(generated, { generated: true });
report.push(`generated tome: ${generated.length} bytes`);
report.push(`reader issues: ${issues.length === 0 ? 'none' : issues.join('; ')}`);
report.push(`header: size=${parsed.header.size} tile=${parsed.header.tile} counts=${JSON.stringify(parsed.header.counts)}`);
report.push(`offsets: ${Object.entries(parsed.header.offsets).map(([k, v]) => `${k}=0x${v.toString(16)}`).join(' ')}`);
report.push(`banner: ${parsed.banner}`);
report.push(`page bounds read back: ${parsed.page.bounds.join(', ')}`);
report.push(`leaf rows read back: ${parsed.leaf.count} -> ${JSON.stringify(parsed.leaf.rows)}`);
report.push(`child records read back: ${parsed.child.records.length}`);

// Constraint: the round trip must return the bounds and indices exactly as handed in.
const back = readGeometry(generated);
const boundsMatch = back.bounds.map(v => Math.round(v)).join(',') === page.min.flatMap((v, i) => [v, page.max[i]]).join(',');
const indicesMatch = back.indices.join(',') === page.indices.join(',');
let maxError = 0;
for (let i = 0; i < page.vertices.length; i++) {
  for (let axis = 0; axis < 3; axis++) {
    maxError = Math.max(maxError, Math.abs(back.vertices[i][axis] - page.vertices[i][axis]));
  }
}
report.push(`round trip: bounds ${boundsMatch ? 'match' : 'DIFFER'} (${back.bounds.join(', ')})`);
report.push(`round trip: indices ${indicesMatch ? 'match' : 'DIFFER'} (${back.indices.length} of ${page.indices.length})`);
report.push(`round trip: vertices decoded ${back.vertices.length} of ${page.vertices.length}, max quantisation error ${maxError.toFixed(4)} units`);
report.push(`round trip: leaves ${JSON.stringify(back.leaves.map(r => ({ count: generated.readUInt32LE(r.offsetA), kind: generated.readUInt32LE(r.offsetA + 4) })))}`);

// Constraint: every differing byte offset must be reported with its cause.
let vanilla = null;
if (dataDir) {
  const archive = ba2Entries(`${dataDir}/Fallout4 - MeshesExtra.ba2`);
  const smallest = archive.tomes()
    .map(entry => ({ entry, bytes: archive.extract(entry) }))
    .sort((a, b) => a.bytes.length - b.bytes.length)[0];
  vanilla = smallest.bytes;
  report.push(`\nvanilla baseline: ${smallest.entry.path} ${vanilla.length}B`);
  const runs = hexdumpDiff(generated, vanilla, 'byte diff');
  for (const run of runs.slice(0, 20)) {
    report.push(`  0x${run.from.toString(16)}..0x${run.to.toString(16)} <- ${describeRun(run, generated, vanilla)}`);
    report.push(`      generated=${[...generated.subarray(run.from, run.to + 1)].map(b => b.toString(16)).join(' ')}`);
    report.push(`      vanilla  =${[...vanilla.subarray(run.from, run.to + 1)].map(b => b.toString(16)).join(' ')}`);
  }

  // Constraint: the retail tomes must still pass the same checks after the writer change.
  for (const archiveName of ['Fallout4 - MeshesExtra.ba2', 'DLCCoast - Main.ba2']) {
    const path = `${dataDir}/${archiveName}`;
    if (!fs.existsSync(path)) continue;
    const a = ba2Entries(path);
    const tomes = a.tomes();
    let ok = 0;
    const failures = [];
    for (const entry of tomes) {
      const bytes = a.extract(entry);
      const checked = checkTome(bytes);
      if (checked.issues.length === 0) ok++;
      else if (failures.length < 5) failures.push(`${entry.path}: ${checked.issues.join('; ')}`);
    }
    report.push(`${archiveName}: ${ok}/${tomes.length} tomes pass magic+size+crc+alignment+offset checks`);
    for (const failure of failures) report.push(`  ${failure}`);
  }
}

console.log(report.join('\n'));
const failed = issues.length > 0 || !boundsMatch || !indicesMatch || maxError > 0.5;
if (failed) {
  console.error('\nPROOF FAILED');
  process.exit(1);
}
