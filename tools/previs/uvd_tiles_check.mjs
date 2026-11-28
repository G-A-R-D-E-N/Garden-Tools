// Constraint: one place decides which claims are invariants, so the sweep and the self-test cannot disagree about what a violation is.
import { Claims } from './uvd_claims.mjs';
import { parse, write } from './uvd_tile_model.mjs';
import { ENFORCED_BODY, decodeBodies, encodeBodies, validateBodies } from './uvd_tile_body.mjs';
import { ENFORCED_HEADER, decodeHeader, encodeHeader, validateHeader } from './uvd_tile_header.mjs';

export const ENFORCED = [...ENFORCED_BODY, ...ENFORCED_HEADER];
export function roundTrips(bytes, model = parse(bytes)) {
  const bodies = decodeBodies(model);
  const header = decodeHeader(model);
  return {
    stage1: write(model).equals(bytes),
    stage2: write(encodeBodies(bodies, model)).equals(bytes),
    stage3: write(encodeHeader(header, encodeBodies(bodies, model))).equals(bytes),
    bodies, header,
  };
}

export function validate(bytes, model = parse(bytes)) {
  const claims = new Claims(ENFORCED);
  const bodies = decodeBodies(model);
  validateBodies(bodies, claims);
  validateHeader(model, decodeHeader(model), claims);
  return { claims, issues: claims.violations() };
}

// Constraint: a generated tome is accepted only when it parses, violates no enforced claim, and its cells list exactly the objects the header holds (and the caller's count when given).
export function acceptTome(bytes, { objects } = {}) {
  try { return acceptParsed(bytes, objects); } catch (e) { return { ok: false, issues: [`parse: ${e.message}`], objectCount: 0 }; }
}

function acceptParsed(bytes, objects) {
  const model = parse(bytes);
  const issues = validate(bytes, model).issues.map(v => `${v.claim}: ${v.failed}/${v.of}`);
  const objectCount = model.objectBoxes.length / 24;
  const listed = decodeBodies(model).slots.reduce((sum, t) => sum + (t ? t.cells.reduce((s, c) => s + c.listCount, 0) : 0), 0);
  if (listed !== objectCount) issues.push(`cells list ${listed} objects, header holds ${objectCount}`);
  if (objects !== undefined && objects !== objectCount) issues.push(`tome holds ${objectCount} objects, expected ${objects}`);
  return { ok: issues.length === 0, issues, objectCount, model };
}
