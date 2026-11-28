#!/usr/bin/env node
// Constraint: the driver must be resolved from the checkout itself, so the documented command takes only the Data folder.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { buildTile, withZeroLod } from './uvd_tile_build.mjs';
import { acceptTome } from './uvd_tiles_check.mjs';
import { patchTome } from './patch_cell.mjs';

const OVERRIDE = 'FO4IDE_PREVIS_DRIVER';
// Constraint: a driver is any built host that calls backend.GenerateCellPrevis; these are the names a build emits.
const DRIVER_NAMES = ['fo4ide-previs-driver.exe', 'fo4ide_previs_driver.exe', 'previs_driver.exe', 'generate_real.exe', 'fo4ide.exe'];
// Constraint: only committed build trees are searched, so no untracked scratch directory can become the driver source.
const BUILD_ROOTS = ['build', 'out', 'cmake-build-debug', 'cmake-build-release', 'PluginEditTool/build', 'PluginEditTool/native/build'];
const MAX_DEPTH = 6;

const repoRoot = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')), '..', '..');

function searchDriver(root) {
  const found = [];
  const walk = (dir, depth) => {
    if (depth > MAX_DEPTH || found.length >= 8) return;
    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        if (entry.name === 'node_modules' || entry.name === '.git') continue;
        walk(full, depth + 1);
      } else if (DRIVER_NAMES.includes(entry.name.toLowerCase())) {
        found.push(full);
      }
    }
  };
  walk(root, 0);
  return found;
}

function resolveDriver() {
  const override = process.env[OVERRIDE];
  if (override) {
    if (!fs.existsSync(override)) {
      console.error(`${OVERRIDE} points at ${override}, which does not exist`);
      process.exit(2);
    }
    return { driver: path.resolve(override), how: `because ${OVERRIDE} names it` };
  }
  const roots = [...new Set([...BUILD_ROOTS.map(root => path.join(repoRoot, root)), ...BUILD_ROOTS.map(root => path.resolve(root))])];
  const excluded = path.join(repoRoot, 'node_modules');
  for (const name of DRIVER_NAMES) {
    for (const root of roots) {
      const candidates = searchDriver(root).filter(file => path.basename(file).toLowerCase() === name && !file.startsWith(excluded));
      if (candidates.length) return { driver: candidates[0], how: `found under ${path.relative(process.cwd(), root) || '.'}` };
    }
  }
  return null;
}

const args = process.argv.slice(2);
// Constraint: CI holds no game data and no driver, so the container validation runs on a tome this repo writes itself.
if (args.includes('--self-test')) {
  const objects = [[-512, -512, 0, 512, 512, 256], [0, 0, 0, 100, 100, 300]].map((box, i) => ({ box, id: 0xfd000000 + i }));
  const bytes = buildTile({ cellBox: [-1024, -1024, 0, 1024, 1024, 512], objects });
  const accepted = acceptTome(bytes, { objects: objects.length });
  const rejected = acceptTome(withZeroLod(bytes));
  const ok = accepted.ok && !rejected.ok;
  console.log('== build_cell validation on a written tome ==');
  console.log(`  ${accepted.ok ? 'OK  ' : 'FAIL'} ${bytes.length}B objects=${accepted.objectCount} tileIssues=${accepted.issues.length ? accepted.issues.join('; ') : 'none'}`);
  console.log(`  ${rejected.ok ? 'FAIL' : 'OK  '} a resealed tome with a zero tile LOD is rejected: ${rejected.issues.join('; ') || 'ACCEPTED'}`);
  console.log(`  self-test: ${ok ? 'PASS' : 'FAIL'}`);
  process.exit(ok ? 0 : 1);
}
// Constraint: build and patch are one action, so the previs root is an option of this command and not a second tool to remember.
const valueOf = name => {
  const inline = args.find(argument => argument.startsWith(`${name}=`));
  if (inline) return inline.slice(name.length + 1);
  const at = args.indexOf(name);
  return at >= 0 ? args[at + 1] : undefined;
};
const optionValues = new Set([valueOf('--previs-root'), valueOf('--plugin')].filter(Boolean));
const positionals = args.filter(argument => !argument.startsWith('--') && !optionValues.has(argument));
const dataDir = positionals[0];
const outputArg = positionals[1];
const previsRoot = valueOf('--previs-root');
const pluginOverride = valueOf('--plugin');
const replaceCluster = args.includes('--replace-cluster');
if (!dataDir) {
  console.error('usage: node tools/previs/build_cell.mjs "<Fallout 4 Data>" [output root] [--previs-root=<root>] [--plugin=<name>] [--replace-cluster]');
  console.error(`  the driver is resolved automatically; set ${OVERRIDE} to choose one explicitly.`);
  console.error('  --previs-root turns the run into the complete action: build, then patch <root>/Vis/<plugin>/<cell8>.uvd.');
  process.exit(2);
}
const resolved = resolveDriver();
if (!resolved) {
  console.error('no previs driver was found.');
  console.error('  searched, at any depth up to ' + MAX_DEPTH + ':', BUILD_ROOTS.join(', '));
  console.error('  for one of:', DRIVER_NAMES.join(', '));
  console.error(`  build one with the native app target (AGENTS.md "Building and running"; .github/workflows/native-real-data.yml:211), or set ${OVERRIDE} to its path.`);
  process.exit(2);
}
const outputRoot = outputArg ? path.resolve(outputArg) : path.resolve('.previs-build');

console.log(`driver: ${resolved.driver} (${resolved.how})`);
console.log(`data:   ${dataDir}`);
console.log(`output: ${outputRoot}`);
const run = spawnSync(resolved.driver, [dataDir, outputRoot], { encoding: 'utf8' });
if (run.stdout) process.stdout.write(run.stdout);
if (run.stderr) process.stderr.write(run.stderr);
if (run.status !== 0) {
  console.error(`the previs driver exited ${run.status}`);
  process.exit(run.status ?? 1);
}

const tomes = [];
const inventory = [];
const walk = dir => {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(full);
    else if (entry.name.toLowerCase().endsWith('.uvd')) tomes.push(full);
    else inventory.push(full);
  }
};
walk(outputRoot);

console.log('\n== built containers ==');
let failed = 0;
for (const tome of tomes) {
  const bytes = fs.readFileSync(tome);
  const accepted = acceptTome(bytes);
  if (!accepted.ok) failed++;
  console.log(`  ${accepted.ok ? 'OK  ' : 'FAIL'} ${path.relative(outputRoot, tome)} ${bytes.length}B objects=${accepted.objectCount} tileIssues=${accepted.issues.length ? accepted.issues.join('; ') : 'none'}`);
}
for (const file of inventory) console.log(`  ${path.relative(outputRoot, file)} ${fs.readFileSync(file).length}B`);

if (!tomes.length) {
  console.error('\nno .uvd tome was written');
  process.exit(1);
}
console.log(`\ntomes the tile model accepts: ${tomes.length - failed}/${tomes.length}`);
if (failed) process.exit(1);

// Constraint: a build that succeeds but a patch that fails must fail this action, so the reason is printed and the exit is non-zero.
if (previsRoot) {
  const root = path.resolve(previsRoot);
  const patchFailures = [];
  for (const tome of tomes) {
    const cell = path.basename(tome, path.extname(tome)).toLowerCase();
    const plugin = pluginOverride ?? path.basename(path.dirname(tome));
    // Constraint: a filesystem error while patching must fail the action with its reason, not crash past the driver's success.
    let patched;
    try {
      patched = patchTome({ builtTome: tome, previsRoot: root, plugin, cell, replaceCluster });
    } catch (error) {
      patchFailures.push(`${plugin}/${cell}: ${error.message}`);
      continue;
    }
    for (const line of patched.lines ?? []) console.log(line);
    if (!patched.ok) patchFailures.push(`${plugin}/${cell}: ${patched.reason}`);
  }
  if (patchFailures.length) {
    console.error(`\nthe action built ${tomes.length} tome(s) but patched ${tomes.length - patchFailures.length}:`);
    for (const failure of patchFailures) console.error(`  ${failure}`);
    process.exit(1);
  }
  console.log(`\npatched into ${root}: ${tomes.length}/${tomes.length} tomes`);
} else {
  console.log('\nbuild only: pass --previs-root=<root> to patch the tomes into a previs root in the same action');
}
