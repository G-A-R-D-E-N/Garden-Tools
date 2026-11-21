// Constraint: a generated tome only reaches the game as a loose file under Vis/<plugin>, so the patch writes it there and reads it back.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { ba2Entries } from './uvd_tome.mjs';
import { buildTile, withZeroLod } from './uvd_tile_build.mjs';
import { acceptTome } from './uvd_tiles_check.mjs';
import { parse } from './uvd_tile_model.mjs';
import { decodeHeader } from './uvd_tile_header.mjs';

// Constraint: whether the cell tome ships loose or inside a shipped archive, its name keeps that casing instead of a second lower-case copy.
function archiveTomeName(root, plugin, cell) {
  const wanted = `Vis\\${plugin}\\${cell}.uvd`.toLowerCase();
  let files;
  try {
    files = fs.readdirSync(root).filter(name => name.toLowerCase().endsWith('.ba2'));
  } catch {
    return undefined;
  }
  for (const file of files) {
    let archive;
    try {
      archive = ba2Entries(path.join(root, file));
    } catch {
      continue;
    }
    const hit = archive.tomes().find(entry => entry.path.replace(/\//g, '\\').toLowerCase() === wanted);
    if (hit) return hit.path.split(/[\\/]/).pop();
  }
  return undefined;
}

export function resolveTomeName(pluginDir, cell, root) {
  const wanted = `${cell}.uvd`;
  let entries;
  try {
    entries = fs.readdirSync(pluginDir);
  } catch {
    entries = undefined;
  }
  const loose = entries?.find(name => name.toLowerCase() === wanted.toLowerCase());
  if (loose) return loose;
  return (root ? archiveTomeName(root, path.basename(pluginDir), cell) : undefined) ?? wanted;
}

const objectKeys = model => {
  const { boxes, ids } = decodeHeader(model).objects;
  return boxes.map((box, i) => `${ids[i]}:${box.join(',')}`);
};

// Constraint: the patch is only proven by reading the written file back through the tile model and its enforced claims.
export function patchTome({ builtTome, previsRoot, plugin, cell, replaceCluster = false }) {
  const generated = fs.readFileSync(builtTome);
  const accepted = acceptTome(generated);
  if (!accepted.ok) {
    return { ok: false, reason: `the built tome fails the tile model: ${accepted.issues.join('; ')}` };
  }
  const generatedObjects = objectKeys(accepted.model);

  const pluginDir = path.join(previsRoot, 'Vis', plugin);
  const name = resolveTomeName(pluginDir, cell, previsRoot);
  const target = path.join(pluginDir, name);
  let previous = 'none';
  if (fs.existsSync(target)) {
    const prior = fs.readFileSync(target);
    let priorObjects;
    try {
      priorObjects = parse(prior).objectBoxes.length / 24;
      previous = `${prior.length}B objects=${priorObjects}`;
    } catch (error) {
      previous = `${prior.length}B not a tile-model tome (${error.message})`;
    }
    // Constraint: a tome holding more objects than this build, or one the tile model cannot read, would lose its previs when one cell's build replaces it.
    if ((priorObjects === undefined || priorObjects > generatedObjects.length) && !replaceCluster) {
      return {
        ok: false,
        cluster: true,
        reason: `${target} is ${previous} and this cell's build has ${generatedObjects.length} objects; replacing it would drop that previs, so pass --replace-cluster to accept the loss or keep the shipped tome out of this root`,
      };
    }
  }

  fs.mkdirSync(pluginDir, { recursive: true });
  fs.writeFileSync(target, generated);

  const written = fs.readFileSync(target);
  const readBack = acceptTome(written, { objects: generatedObjects.length });
  const world = readBack.model?.world;
  const checks = [
    ['tile model accepts the written tome', readBack.ok],
    ['written bytes equal the built bytes', written.equals(generated)],
    ['written tome lists the generated objects', readBack.ok && objectKeys(readBack.model).join(';') === generatedObjects.join(';')],
    ['written name keeps the existing casing', path.basename(target) === name],
  ];
  const lines = [
    '== patch a cell tome into the previs root ==',
    `  built tome   ${builtTome} objects=${generatedObjects.length}`,
    `  target       ${target}`,
    `  previous     ${previous}`,
    `  written      ${written.length}B objects=${readBack.objectCount} tileIssues=${readBack.issues.length ? readBack.issues.join('; ') : 'none'}`,
    `  world        ${world ? `${world.min.join(',')} .. ${world.max.join(',')}` : 'unreadable'}`,
  ];
  for (const [what, ok] of checks) lines.push(`  ${what.padEnd(44)} ${ok ? 'ok' : 'FAILED'}`);
  const ok = checks.every(([, passed]) => passed);
  lines.push(`patch: ${ok ? 'all checks passed' : 'FAILURES'}`);
  return { ok, target, name, previous, written, checks, lines };
}

// Constraint: CI holds no game data, so the patch and its case handling are proven on tomes this repo writes itself.
function selfTest() {
  const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'previs-patch-'));
  const objects = count => Array.from({ length: count }, (_, i) => ({ box: [i * 100, 0, 0, i * 100 + 50, 50, 50], id: 0xfd000000 + i }));
  const world = [-1024, -1024, 0, 1024, 1024, 512];
  const built = path.join(scratch, 'built', '00000fec.uvd');
  fs.mkdirSync(path.dirname(built), { recursive: true });
  fs.writeFileSync(built, buildTile({ cellBox: world, objects: objects(2) }));

  const clean = patchTome({ builtTome: built, previsRoot: path.join(scratch, 'clean'), plugin: 'Fallout4.esm', cell: '00000fec' });
  const pluginDir = path.join(scratch, 'cased', 'Vis', 'Fallout4.esm');
  fs.mkdirSync(pluginDir, { recursive: true });
  fs.writeFileSync(path.join(pluginDir, '00000FEC.UVD'), buildTile({ cellBox: world, objects: objects(1) }));
  const cased = patchTome({ builtTome: built, previsRoot: path.join(scratch, 'cased'), plugin: 'Fallout4.esm', cell: '00000fec' });
  const casedFiles = fs.readdirSync(pluginDir).sort().join(',');
  const clusterDir = path.join(scratch, 'cluster', 'Vis', 'Fallout4.esm');
  fs.mkdirSync(clusterDir, { recursive: true });
  fs.writeFileSync(path.join(clusterDir, '00000fec.uvd'), buildTile({ cellBox: world, objects: objects(5) }));
  const refused = patchTome({ builtTome: built, previsRoot: path.join(scratch, 'cluster'), plugin: 'Fallout4.esm', cell: '00000fec' });
  const forced = patchTome({ builtTome: built, previsRoot: path.join(scratch, 'cluster'), plugin: 'Fallout4.esm', cell: '00000fec', replaceCluster: true });
  const legacyDir = path.join(scratch, 'legacy', 'Vis', 'Fallout4.esm');
  fs.mkdirSync(legacyDir, { recursive: true });
  fs.writeFileSync(path.join(legacyDir, '00000fec.uvd'), Buffer.alloc(64));
  const legacy = patchTome({ builtTome: built, previsRoot: path.join(scratch, 'legacy'), plugin: 'Fallout4.esm', cell: '00000fec' });
  const broken = path.join(scratch, 'built', 'broken.uvd');
  fs.writeFileSync(broken, withZeroLod(fs.readFileSync(built)));
  const invalid = patchTome({ builtTome: broken, previsRoot: path.join(scratch, 'invalid'), plugin: 'Fallout4.esm', cell: '00000fec' });

  const checks = [
    ['clean root receives the built tome', clean.ok && clean.written.equals(fs.readFileSync(built))],
    ['existing 00000FEC.UVD keeps its casing', cased.ok && cased.name === '00000FEC.UVD' && casedFiles === '00000FEC.UVD'],
    // Constraint: Windows matches file names case-insensitively, so only an exact directory listing entry is a duplicate.
    ['no second lower-case tome is created', !fs.readdirSync(pluginDir).includes('00000fec.uvd')],
    ['a tome with more objects is refused instead of truncated', !refused.ok && refused.cluster === true],
    ['--replace-cluster overrides the refusal', forced.ok],
    ['an unreadable existing tome is refused without --replace-cluster', !legacy.ok && legacy.cluster === true],
    ['a built tome that violates the tile model is not written', !invalid.ok && !fs.existsSync(path.join(scratch, 'invalid'))],
  ];
  console.log('== patch_cell self-test on written tomes ==');
  for (const [what, ok] of checks) console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${what}`);
  const ok = checks.every(([, passed]) => passed);
  console.log(`  self-test: ${ok ? 'PASS' : 'FAIL'}`);
  fs.rmSync(scratch, { recursive: true, force: true });
  return ok;
}

// Constraint: build_cell imports this module for patchTome, so the command line runs only when this file is the entry point.
function main(args) {
  if (args.includes('--self-test')) process.exit(selfTest() ? 0 : 1);
  const flags = args.filter(argument => argument.startsWith('--'));
  const [builtTome, previsRoot, plugin, cell] = args.filter(argument => !argument.startsWith('--'));
  if (!builtTome || !previsRoot || !plugin || !cell) {
    console.error('usage: node tools/previs/patch_cell.mjs <built.uvd> "<Data or output root>" <plugin> <cell8> [--replace-cluster]');
    console.error('  writes <root>/Vis/<plugin>/<cell8>.uvd and verifies it through this repo\'s reader and detector.');
    process.exit(2);
  }
  const result = patchTome({ builtTome, previsRoot, plugin, cell, replaceCluster: flags.includes('--replace-cluster') });
  for (const line of result.lines ?? []) console.log(line);
  if (!result.ok) {
    console.error(result.reason);
    process.exit(result.cluster ? 3 : 1);
  }
}

const isEntry = process.argv[1] !== undefined &&
  path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
if (isEntry) main(process.argv.slice(2));
