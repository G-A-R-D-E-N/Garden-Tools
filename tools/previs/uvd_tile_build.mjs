#!/usr/bin/env node
// Constraint: mirrors Radicle/src/previs/uvd_tile_build.cpp; the builder emits one open tile that lists every surviving object and culls only what closed occluder triangles within 4096 units enclose from its centre and box corners on all six axes.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { DEFAULT_BANNER, crc32c } from './uvd_tome.mjs';
import { parse, write } from './uvd_tile_model.mjs';
import { FLT_MAX, OUTSIDE_TARGET, bitLength, decodeBodies, encodeBodies } from './uvd_tile_body.mjs';
import { decodeHeader, encodeHeader } from './uvd_tile_header.mjs';
import { acceptTome } from './uvd_tiles_check.mjs';

export const LOD_BASE = 512;
export const FACES = 6;
const FULL = [0, 0xffff];
const EXPAND_MIN = 16;
const EXPAND_MAX = 128;
const f32 = Math.fround;

function requireBox(box, label) {
  if (box.length !== 6 || !box.every(Number.isFinite)) throw new Error(`build: ${label} has a non-finite coordinate`);
  for (let a = 0; a < 3; a++) if (!(box[a] <= box[3 + a])) throw new Error(`build: ${label} has min above max`);
}

export function tileLod(box, lodBase = LOD_BASE) {
  return f32(Math.cbrt((box[3] - box[0]) * (box[4] - box[1]) * (box[5] - box[2])) / lodBase);
}

export function defaultExpand(box) {
  const longest = Math.max(box[3] - box[0], box[4] - box[1], box[5] - box[2]);
  let expand = EXPAND_MIN;
  while (expand < EXPAND_MAX && expand * 2 <= longest / 64) expand *= 2;
  return expand;
}

export function worldBox({ cellBox, objects }) {
  let world = cellBox ? cellBox.map(f32) : null;
  if (world) requireBox(world, 'cell box');
  objects.forEach((object, i) => {
    const box = object.box.map(f32);
    requireBox(box, `object ${i} box`);
    if (!world) { world = box; return; }
    for (let a = 0; a < 3; a++) { world[a] = Math.min(world[a], box[a]); world[3 + a] = Math.max(world[3 + a], box[3 + a]); }
  });
  if (!world) throw new Error('build: no cell box and no objects');
  for (let a = 0; a < 3; a++) if (!(world[3 + a] > world[a])) world[3 + a] = f32(world[a] + LOD_BASE);
  if (!world.every(Number.isFinite)) throw new Error('build: world box is not finite');
  return world;
}

// Constraint: a body's own triangles touch its box, so a body never culls itself.
function touchesBox(triangle, box) {
  for (let a = 0; a < 3; a++) {
    const low = Math.min(triangle[a], triangle[3 + a], triangle[6 + a]);
    const high = Math.max(triangle[a], triangle[3 + a], triangle[6 + a]);
    if (high < box[a] || low > box[3 + a]) return false;
  }
  return true;
}

// Constraint: a ray that needs more than one exterior cell edge to hit a wall is not looking at an enclosing room.
const MAX_ENCLOSURE = 4096;
// Constraint: lets a ray through the diagonal seam of two wall triangles be blocked by one of them.
const SEAM = 1e-9;

// Constraint: Moller-Trumbore without a backface test, so a wall blocks whichever way its winding faces.
function blocksRay(triangle, origin, direction) {
  const edge1 = [triangle[3] - triangle[0], triangle[4] - triangle[1], triangle[5] - triangle[2]];
  const edge2 = [triangle[6] - triangle[0], triangle[7] - triangle[1], triangle[8] - triangle[2]];
  const p = [direction[1] * edge2[2] - direction[2] * edge2[1], direction[2] * edge2[0] - direction[0] * edge2[2],
    direction[0] * edge2[1] - direction[1] * edge2[0]];
  const determinant = edge1[0] * p[0] + edge1[1] * p[1] + edge1[2] * p[2];
  if (!(Math.abs(determinant) > 1e-12)) return false;
  const inverse = 1 / determinant;
  const offset = [origin[0] - triangle[0], origin[1] - triangle[1], origin[2] - triangle[2]];
  const u = (offset[0] * p[0] + offset[1] * p[1] + offset[2] * p[2]) * inverse;
  if (u < -SEAM || u > 1 + SEAM) return false;
  const q = [offset[1] * edge1[2] - offset[2] * edge1[1], offset[2] * edge1[0] - offset[0] * edge1[2],
    offset[0] * edge1[1] - offset[1] * edge1[0]];
  const v = (direction[0] * q[0] + direction[1] * q[1] + direction[2] * q[2]) * inverse;
  if (v < -SEAM || u + v > 1 + SEAM) return false;
  const distance = (edge2[0] * q[0] + edge2[1] * q[1] + edge2[2] * q[2]) * inverse;
  return distance > 0 && distance <= MAX_ENCLOSURE;
}

// Constraint: one open side leaves the object listed, so a missing or broken wall can never hide geometry behind it.
export function cullOccluded({ objects = [], occluders = [] }) {
  if (occluders.length === 0) return objects.slice();
  const directions = [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]];
  // Constraint: nothing outside the occluder set's own extent can block, so this rejects an open cell without scanning it.
  const bounds = [Infinity, Infinity, Infinity, -Infinity, -Infinity, -Infinity];
  for (const triangle of occluders) {
    for (let a = 0; a < 3; a++) {
      bounds[a] = Math.min(bounds[a], triangle[a], triangle[3 + a], triangle[6 + a]);
      bounds[3 + a] = Math.max(bounds[3 + a], triangle[a], triangle[3 + a], triangle[6 + a]);
    }
  }
  return objects.filter(object => {
    // Constraint: origin 0 is the centre and 1..8 are the box corners, so the shell must cover the whole box and not only its centre line.
    const origins = [[0, 1, 2].map(a => (object.box[a] + object.box[3 + a]) / 2)];
    for (let corner = 0; corner < 8; corner++) origins.push([0, 1, 2].map(a => object.box[(corner >> a) & 1 ? 3 + a : a]));
    const shell = occluders.filter(triangle => !touchesBox(triangle, object.box));
    for (let direction = 0; direction < 6; direction++) {
      const axis = direction >> 1;
      const withinExtent = direction % 2 === 0 ? origins[0][axis] < bounds[3 + axis] : origins[0][axis] > bounds[axis];
      if (!withinExtent) return true;
      for (const origin of origins) {
        if (!shell.some(triangle => blocksRay(triangle, origin, directions[direction]))) return true;
      }
    }
    return false;
  });
}

// Constraint: a closed box of wall triangles encloses, and skipFace drops one wall so a shell can be left open.
export function shellTriangles(box, skipFace = -1) {
  const [x0, y0, z0, x1, y1, z1] = box;
  const quad = (a, b, c, d) => [[...a, ...b, ...c], [...a, ...c, ...d]];
  const faces = [
    quad([x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [x1, y0, z1]),
    quad([x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0]),
    quad([x0, y1, z0], [x0, y1, z1], [x1, y1, z1], [x1, y1, z0]),
    quad([x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1]),
    quad([x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]),
    quad([x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [x1, y0, z0]),
  ];
  return faces.filter((_, face) => face !== skipFace).flat();
}

const trivialKd = () => ({ nodeCount: 1, mapWidth: 1, symbols: Uint8Array.of(3), splits: [], entries: [0] });

function openTile(box, count, expand) {
  return {
    box, leaf: true, flags: 0, expand, clusterCount: 1,
    cells: [{ portalStart: 0, portalCount: FACES, listStart: 0, listCount: count, cluster: 0, tag: 0, x: FULL, y: FULL, z: FULL }],
    portals: Array.from({ length: FACES }, (_, face) => ({
      target: OUTSIDE_TARGET, hier: 0, door: 0, outside: 1, face, plane: face & 1 ? 0xffff : 0, targetCell: 0, rect0: FULL, rect1: FULL,
    })),
    planes: [], bsp: [], kd: trivialKd(),
  };
}

export function buildTile({ cellBox, objects = [], occluders = [], banner = DEFAULT_BANNER, expand }) {
  const visible = cullOccluded({ objects, occluders });
  const world = worldBox({ cellBox, objects: visible });
  if (new Set(visible.map(object => object.id)).size !== visible.length) throw new Error('build: an object id is repeated');
  const count = visible.length;
  const tile = openTile(world, count, expand ?? defaultExpand(world));
  const header = {
    banner, lodBase: LOD_BASE, reserved10: 0, world: { min: world.slice(0, 3), max: world.slice(3) },
    widths: [count ? 1 : 0, bitLength(count), 0, 0],
    topKd: { symbols: Uint8Array.of(3), splits: [FLT_MAX] },
    lod: [tileLod(world)],
    objects: { boxes: visible.map(object => object.box), distances: null, ids: visible.map(object => object.id) },
    runA: count ? [[0, count]] : [], runB: [],
    clusters: [{ portalStart: 0, portalCount: 0, x: FULL, y: FULL, z: FULL }],
    clusterPortals: Buffer.alloc(0),
    gates: { count: 0, records: [], lists: Buffer.alloc(0), trailer: Buffer.alloc(0) },
    child: {
      trees: Array.from({ length: FACES }, trivialKd),
      rows: [{ words: [FACES, 0, 0], block: null, treesBefore: 0 }],
    },
  };
  const bytes = write(encodeHeader(header, encodeBodies({ slots: [tile] }, { slots: [] })));
  const accepted = acceptTome(bytes, { objects: count });
  if (!accepted.ok) throw new Error(`build: the written tome is invalid: ${accepted.issues[0]}`);
  return bytes;
}

export function withZeroLod(bytes) {
  const broken = Buffer.from(bytes);
  broken.writeFloatLE(0, broken.readUInt32LE(0x9c));
  broken.writeUInt32LE(crc32c(broken.subarray(8)), 4);
  return broken;
}

const RETAIL = ['00000FE1', '00000FE8', '0023F949', '000016D9'];

function findFile(root, name) {
  const hits = [];
  const walk = dir => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (entry.name.toLowerCase() === `${name}.uvd`.toLowerCase()) hits.push(full);
    }
  };
  walk(root);
  return hits[0];
}

export function retailCheck(root, log = console.log) {
  let ok = true;
  for (const name of RETAIL) {
    const file = findFile(root, name);
    if (!file) { log(`  FAIL ${name}: not found under ${root}`); ok = false; continue; }
    const retail = fs.readFileSync(file);
    const model = parse(retail);
    const header = decodeHeader(model);
    const tile = decodeBodies(model).slots[0];
    const objects = header.objects.boxes.map((box, i) => ({ box, id: header.objects.ids[i] }));
    const built = buildTile({ cellBox: [...header.world.min, ...header.world.max], objects, banner: header.banner, expand: tile.expand });
    const same = built.equals(retail);
    log(`  ${same ? 'OK  ' : 'FAIL'} ${name} ${retail.length}B objects=${objects.length} expand=${tile.expand} rebuilt ${built.length}B ${same ? 'byte-identical' : 'DIFFERS'}`);
    if (!same) ok = false;
  }
  log(`retail check: ${ok ? 'PASS' : 'FAIL'}`);
  return ok;
}

export function selfTest(log = console.log) {
  const checks = [];
  const note = (what, pass) => checks.push([what, pass]);
  const boxes = [[0, 0, 0, 100, 100, 50], [200, 0, 0, 300, 100, 60], [400, 0, 0, 500, 100, 70]];
  const objects = boxes.map((box, i) => ({ box, id: 0xfd000000 + i }));
  const bytes = buildTile({ objects });
  const accepted = acceptTome(bytes, { objects: 3 });
  const header = decodeHeader(parse(bytes));
  note('three objects build a tome the tile model accepts', accepted.ok && accepted.objectCount === 3);
  note('world box is the join of the object boxes', header.world.min.join() === '0,0,0' && header.world.max.join() === '500,100,70');
  note('run list A is (0, 3) with widths 1, 2, 0, 0', header.runA.join() === '0,3' && header.widths.join() === '1,2,0,0');
  note('ids and boxes keep caller order', header.objects.ids.join() === objects.map(o => o.id).join() && header.objects.boxes.every((b, i) => b.join() === boxes[i].join()));
  note('LOD is float32(cbrt(volume) / 512)', header.lod[0] === Math.fround(Math.cbrt(500 * 100 * 70) / 512));
  const empty = buildTile({ cellBox: [0, 0, 0, 4096, 4096, 100] });
  note('no objects: empty list, widths 0,0,0,0, expand 64', decodeHeader(parse(empty)).runA.length === 0 && decodeHeader(parse(empty)).widths.join() === '0,0,0,0' && decodeBodies(parse(empty)).slots[0].expand === 64 && acceptTome(empty, { objects: 0 }).ok);
  note('an axis without thickness is widened by 512', decodeHeader(parse(buildTile({ objects: [{ box: [0, 0, 0, 10, 10, 0], id: 1 }] }))).world.max[2] === 512);
  const refuses = fn => { try { fn(); return false; } catch { return true; } };
  note('a repeated id is refused', refuses(() => buildTile({ objects: [{ box: boxes[0], id: 1 }, { box: boxes[1], id: 1 }] })));
  note('an inverted box is refused', refuses(() => buildTile({ objects: [{ box: [1, 0, 0, 0, 1, 1], id: 1 }] })));
  note('no box and no object is refused', refuses(() => buildTile({ objects: [] })));
  note('a resealed tome with a zero tile LOD is rejected', !acceptTome(withZeroLod(bytes)).ok);
  const truncated = Buffer.from(bytes.subarray(0, 0x150 + 8));
  truncated.writeUInt32LE(truncated.length, 8);
  truncated.writeUInt32LE(crc32c(truncated.subarray(8)), 4);
  note('a resealed truncated tome is refused, not thrown', (() => { try { return !acceptTome(truncated).ok; } catch { return false; } })());
  note('an object count that differs from the tome is rejected', !acceptTome(bytes, { objects: 4 }).ok);
  const cullObjects = [[0, 0, 0, 64, 64, 64], [400, 0, 0, 464, 64, 64], [100, 0, 0, 164, 64, 64]].map((box, i) => ({ box, id: 11 + i }));
  const cullCell = [-1024, -1024, 0, 1024, 1024, 512];
  const shell = shellTriangles([-16, -16, -16, 80, 80, 80]);
  const culled = buildTile({ cellBox: cullCell, objects: cullObjects, occluders: shell });
  note('a shell of wall triangles culls the object it encloses', decodeHeader(parse(culled)).objects.ids.join() === '12,13' && acceptTome(culled, { objects: 2 }).ok);
  note('without occluder triangles all three objects are listed', decodeHeader(parse(buildTile({ cellBox: cullCell, objects: cullObjects }))).objects.ids.join() === '11,12,13');
  note('a shell with one open side culls nothing', decodeHeader(parse(buildTile({ cellBox: cullCell, objects: cullObjects, occluders: shellTriangles([-16, -16, -16, 80, 80, 80], 0) }))).objects.ids.join() === '11,12,13');
  note('a lone wall triangle culls nothing', decodeHeader(parse(buildTile({ cellBox: cullCell, objects: cullObjects, occluders: [[-16, -16, -16, -16, 80, -16, -16, 80, 80]] }))).objects.ids.join() === '11,12,13');
  const idsOf = occluders => decodeHeader(parse(buildTile({ cellBox: cullCell, objects: cullObjects, occluders }))).objects.ids.join();
  note('six plates beyond one cell edge cull nothing', idsOf(shellTriangles([-6000, -6000, -6000, 6000, 6000, 6000])) === '11,12,13');
  note('the same shell within one cell edge culls the object it encloses and keeps the two whose corners sit past the cap', idsOf(shellTriangles([-4000, -4000, -4000, 4000, 4000, 4000])) === '12,13');
  const xWall = (hole) => {
    const out = [];
    for (let iy = 0; iy < 3; iy++) for (let iz = 0; iz < 3; iz++) {
      if (hole && iy === 2 && iz === 2) continue;
      const y0 = -16 + iy * 32, y1 = y0 + 32, z0 = -16 + iz * 32, z1 = z0 + 32;
      out.push([80, y0, z0, 80, y1, z0, 80, y1, z1], [80, y0, z0, 80, y1, z1, 80, y0, z1]);
    }
    return out;
  };
  note('a room whose wall is whole culls the object it encloses', idsOf([...shellTriangles([-16, -16, -16, 80, 80, 80], 0), ...xWall(false)]) === '12,13');
  note('a hole only the box corner can see through keeps the object', idsOf([...shellTriangles([-16, -16, -16, 80, 80, 80], 0), ...xWall(true)]) === '11,12,13');
  const allCulled = buildTile({ cellBox: cullCell, objects: [cullObjects[0], { box: [8, 8, 8, 40, 40, 40], id: 14 }], occluders: shell });
  note('a cell with every object culled builds the 1,056-byte no-object tome', allCulled.length === 1056 && decodeHeader(parse(allCulled)).objects.ids.length === 0 && acceptTome(allCulled, { objects: 0 }).ok);
  log('== uvd_tile_build self-test ==');
  for (const [what, pass] of checks) log(`  ${pass ? 'PASS' : 'FAIL'}  ${what}`);
  const ok = checks.every(([, pass]) => pass);
  log(`  self-test: ${ok ? 'PASS' : 'FAIL'}`);
  return ok;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  if (args.includes('--self-test')) process.exit(selfTest() ? 0 : 1);
  const at = args.indexOf('--retail');
  if (at >= 0 && args[at + 1]) process.exit(retailCheck(args[at + 1]) ? 0 : 1);
  console.error('usage: uvd_tile_build.mjs --self-test | --retail <folder holding the extracted 00000FE1, 00000FE8, 0023F949 and 000016D9 tomes>');
  process.exit(2);
}
