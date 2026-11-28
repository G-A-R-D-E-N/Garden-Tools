#!/usr/bin/env node
// Constraint: the tile model is proved only by byte-identical round trips over real tomes, so the retail sweep exits non-zero on any mismatch or violated invariant.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Claims } from './uvd_claims.mjs';
import { TomeLayoutError, parse, write } from './uvd_tile_model.mjs';
import { validateBodies } from './uvd_tile_body.mjs';
import { validateHeader } from './uvd_tile_header.mjs';
import { ENFORCED, roundTrips, validate } from './uvd_tiles_check.mjs';

export { parse, write, ENFORCED, roundTrips, validate };

export function collect(inputs) {
  const files = [];
  const walk = p => {
    const st = fs.statSync(p);
    if (st.isDirectory()) for (const e of fs.readdirSync(p).sort()) walk(path.join(p, e));
    else if (p.toLowerCase().endsWith('.uvd')) files.push(p);
  };
  inputs.forEach(walk);
  return files;
}

export function sweep(inputs, { log = console.log } = {}) {
  const groups = new Map();
  const t0 = Date.now();
  for (const input of inputs) {
    const g = { tomes: 0, parsed: 0, stage1: 0, stage2: 0, stage3: 0, parseErrors: new Map(), claims: new Claims(ENFORCED) };
    groups.set(input, g);
    for (const file of collect([input])) {
      g.tomes++;
      let bytes;
      let model;
      try { bytes = fs.readFileSync(file); model = parse(bytes); g.parsed++; } catch (e) {
        const key = e instanceof TomeLayoutError ? e.message.replace(/0x[0-9a-f]+|\d+/g, 'N') : String(e.message);
        const seen = g.parseErrors.get(key) ?? { count: 0, first: file };
        seen.count++;
        g.parseErrors.set(key, seen);
        continue;
      }
      const r = roundTrips(bytes, model);
      for (const k of ['stage1', 'stage2', 'stage3']) if (r[k]) g[k]++;
      validateBodies(r.bodies, g.claims);
      validateHeader(model, r.header, g.claims);
    }
  }
  let failed = false;
  for (const [input, g] of groups) {
    log(`== ${input}: ${g.tomes} tomes`);
    log(`  stage 1 framing round trip  parsed ${g.parsed}/${g.tomes}, byte-identical ${g.stage1}/${g.tomes}`);
    log(`  stage 2 tile interior       decode+encode+write byte-identical ${g.stage2}/${g.tomes}`);
    log(`  stage 3 header sections     decode+encode+write byte-identical ${g.stage3}/${g.tomes}`);
    for (const [msg, e] of g.parseErrors) log(`  parse error x${e.count}: ${msg} (first ${e.first})`);
    for (const line of g.claims.lines()) log('  ' + line);
    const bad = g.claims.violations();
    for (const v of bad) log(`  VIOLATION ${v.claim}: ${v.failed}/${v.of} (${(v.where ?? []).join('; ')})`);
    if (g.parsed !== g.tomes || g.stage1 !== g.tomes || g.stage2 !== g.tomes || g.stage3 !== g.tomes || bad.length) failed = true;
  }
  log(`sweep ${failed ? 'FAIL' : 'PASS'} in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
  return { ok: !failed, groups };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  if (args.includes('--self-test')) {
    const { selfTest } = await import('./uvd_tiles_selftest.mjs');
    const result = selfTest();
    for (const line of result.lines) console.log(line);
    process.exit(result.ok ? 0 : 1);
  }
  const inputs = args.filter(a => !a.startsWith('--'));
  if (!inputs.length) {
    console.error('usage: uvd_tiles.mjs --self-test | <tome.uvd|directory>...');
    process.exit(2);
  }
  process.exit(sweep(inputs).ok ? 0 : 1);
}
