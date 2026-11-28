// Constraint: the self-test builds its tomes with the same encoders the retail round trip uses, and every corruption must be rejected with its own reason.
import { DEFAULT_BANNER, checkTome, crc32c } from './uvd_tome.mjs';
import { TomeLayoutError, parse, write } from './uvd_tile_model.mjs';
import { FLT_MAX, encodeBodies } from './uvd_tile_body.mjs';
import { encodeHeader } from './uvd_tile_header.mjs';
import { roundTrips, validate } from './uvd_tiles_check.mjs';

const FULL = [0, 0xffff];
const cell = (start, count) => ({ portalStart: start, portalCount: count, listStart: 0, listCount: 1, cluster: 0, tag: 0, x: FULL, y: FULL, z: FULL });
const portal = (target, face) => ({ target, hier: 0, door: 0, outside: 0, face, plane: face & 1 ? 0xffff : 0, targetCell: 0, rect0: FULL, rect1: FULL });
const leafKd = () => ({ nodeCount: 1, mapWidth: 1, symbols: Uint8Array.of(3), splits: [], entries: [0] });
const tile = (box, leaf, cells, portals) => ({ box, leaf, flags: 0, expand: 128, clusterCount: 1, cells, portals, planes: [], bsp: [], kd: leaf ? leafKd() : null });

export function syntheticTome({ extraObject = false } = {}) {
  const world = { min: [0, 0, 0], max: [1024, 1024, 256] };
  const slots = [
    tile([0, 0, 0, 1024, 1024, 256], false, [cell(0, 2)], [portal(1, 1), portal(2, 0)]),
    tile([0, 0, 0, 512, 1024, 256], true, [cell(0, 1)], [portal(2, 1)]),
    tile([512, 0, 0, 1024, 1024, 256], true, [cell(0, 1)], [portal(1, 0)]),
  ];
  const boxes = [[0, 0, 0, 100, 100, 50], [200, 0, 0, 300, 100, 60]];
  if (extraObject) boxes.push([400, 0, 0, 500, 100, 70]);
  const header = {
    banner: DEFAULT_BANNER, lodBase: 512, reserved10: 0, world, widths: [3, 2, 0, 0],
    topKd: { symbols: Uint8Array.of(0, 3, 3), splits: [512, FLT_MAX, FLT_MAX] },
    lod: [20, 15, 15],
    objects: {
      boxes,
      distances: boxes.map(b => [b[0], b[1], b[2], 0, b[3], b[4], b[5], FLT_MAX]),
      ids: boxes.map((_, i) => 0x01000a74 + i),
    },
    runA: [[1, 1], [2, 0]], runB: [],
    clusters: [{ portalStart: 0, portalCount: 0, x: FULL, y: FULL, z: FULL }],
    clusterPortals: Buffer.alloc(0),
    gates: { count: 0, records: [], lists: Buffer.alloc(0), trailer: Buffer.alloc(0) },
    child: {
      trees: [{ nodeCount: 3, mapWidth: 2, symbols: Uint8Array.of(0, 3, 3), splits: [512], entries: [0, 1] }],
      rows: [{ words: [3, 1, 1], block: Buffer.alloc(16, 0x5a), treesBefore: 1 }, { words: [0, 0, 0], block: null, treesBefore: 0 }],
    },
  };
  const bodies = { slots };
  const model = encodeHeader(header, encodeBodies(bodies, { slots: [] }));
  return write(model);
}

const u32 = (b, o) => b.readUInt32LE(o);
const reseal = bytes => { bytes.writeUInt32LE(crc32c(bytes.subarray(8)), 4); return bytes; };

function rejection(bytes, mutate, expect, { seal = true } = {}) {
  const broken = Buffer.from(bytes);
  mutate(broken);
  if (seal) reseal(broken);
  try {
    const model = parse(broken);
    const issues = validate(broken, model).issues;
    const hit = issues.find(i => i.claim.includes(expect));
    return hit ? `rejected by validate (${hit.claim})` : null;
  } catch (e) {
    if (e instanceof TomeLayoutError && e.message.includes(expect)) return `rejected by parse (${e.message})`;
    return null;
  }
}

export function selfTest() {
  const lines = ['== uvd_tiles self-test on tomes this writer built =='];
  let ok = true;
  const note = (label, pass, detail = '') => { ok = ok && pass; lines.push(`  ${label}: ${pass ? 'ok' : 'FAIL'}${detail ? ' (' + detail + ')' : ''}`); };

  const bytes = syntheticTome();
  const model = parse(bytes);
  const tiles = model.slots.filter(Boolean).length;
  note('synthetic tome parses', tiles === 3, `${bytes.length} bytes, ${tiles} tiles, ${model.child.trees.length} child tree`);
  note('container check (magic, size, CRC-32C, alignment)', checkTome(bytes).issues.length === 0, checkTome(bytes).issues.join(','));
  const r = roundTrips(bytes, model);
  note('stage 1 framing round trip is byte-identical', r.stage1);
  note('stage 2 tile interior decode and encode is byte-identical', r.stage2);
  note('stage 3 header decode and encode is byte-identical', r.stage3);
  const v = validate(bytes, model);
  note('validators report no violation', v.issues.length === 0, v.issues.map(i => i.claim).join(','));

  const grown = syntheticTome({ extraObject: true });
  const gm = parse(grown);
  note('offsets and counts are re-derived when the model grows', u32(grown, 0x40) === 3 && u32(grown, 0xa0) > u32(bytes, 0xa0) && grown.length > bytes.length && validate(grown, gm).issues.length === 0,
    `objects 2 -> 3, tile table 0x${u32(bytes, 0xa0).toString(16)} -> 0x${u32(grown, 0xa0).toString(16)}, size ${bytes.length} -> ${grown.length}`);

  const tile1 = u32(bytes, u32(bytes, 0xa0) + 4);
  const pad = (() => { const lod = u32(bytes, 0x9c); return lod + 4 * 3; })();
  const cases = [
    ['body bit flip without a CRC update', b => { b[tile1 + 0x60] ^= 1; }, 'crc mismatch', { seal: false }],
    ['tile size field moved by 16', b => b.writeUInt32LE(b.readUInt32LE(tile1 + 0x2c) + 0x1000, tile1 + 0x2c), 'tile slot 1'],
    ['cell-starts offset moved by 16', b => b.writeUInt32LE(u32(b, 0x88) + 16, 0x88), 'cellStarts'],
    ['nonzero byte in region padding', b => { b[pad] = 1; }, 'nonzero padding'],
    ['leaf tile count off by one', b => b.writeUInt32LE(u32(b, 0x8c) + 1, 0x8c), 'leaf tile count'],
    ['tile table entry swapped', b => { const t = u32(b, 0xa0); const x = u32(b, t + 4); b.writeUInt32LE(u32(b, t + 8), t + 4); b.writeUInt32LE(x, t + 8); }, 'tile slot 1'],
    ['file size field wrong', b => b.writeUInt32LE(b.length + 16, 8), 'size field'],
    ['cell portal count off by one', b => b.writeUInt32LE(2, tile1 + u32(b, tile1 + 0x38) + 4), 'portal counts sum'],
    ['portal target outside the slots', b => { const p = tile1 + u32(b, tile1 + 0x3c); b.writeUInt32LE((b.readUInt32LE(p) & 0xfc000000) | 9, p); }, 'target slot holds a tile'],
    ['tile box moved off its tree node', b => b.writeFloatLE(3, tile1 + 4), 'tile box'],
    ['leaf tile cluster count wrong', b => b.writeUInt16LE(2, tile1 + 0x36), 'cluster count'],
    ['child tree node symbol flipped', b => { const k = u32(b, u32(b, 0xa8) + 4); b[k] = (b[k] & 0xf3) | 0x08; }, 'child: tree node symbols'],
    ['top kd symbol flipped', b => { const k = u32(b, 0x30); b[k] = (b[k] & 0xfc) | 1; }, 'top kd'],
    ['slot path byte flipped', b => { b[u32(b, 0x98)] ^= 0xff; }, 'slot paths'],
    ['cell starts entry changed', b => b.writeUInt32LE(7, u32(b, 0x88) + 4), 'cell starts'],
  ];
  for (const [name, mutate, expect, opts] of cases) {
    const reason = rejection(bytes, mutate, expect, opts);
    note(`${name} corrupted`, reason !== null, reason ?? 'ACCEPTED, the model is too loose');
  }
  lines.push(`  self-test: ${ok ? 'PASS' : 'FAIL'}`);
  return { ok, lines };
}
