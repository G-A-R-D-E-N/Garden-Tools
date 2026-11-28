import { describe, expect, it } from 'vitest';
import {
  describeCandidates, describeProgress, describeVerify, hostFailure, parseCandidates, parsePlan, parseVerify, parseWorlds,
  sameWorld, verifyProblems,
} from './lodClient';

describe('parseWorlds', () => {
  it('keeps the picker fields and defaults missing ranges', () => {
    const worlds = parseWorlds(
      '[{"name":"Commonwealth","southwestX":-96,"southwestY":-96,"stride":256,"levelMin":4,"levelMax":32},' +
        '{"name":"FarHarbor","stride":128}]',
    );
    expect(worlds).toHaveLength(2);
    expect(worlds[0]).toEqual({
      name: 'Commonwealth',
      southwestX: -96,
      southwestY: -96,
      stride: 256,
      levelMin: 4,
      levelMax: 32,
    });
    expect(worlds[1].levelMin).toBe(0);
  });

  it('rejects a world without a stride and a host error string', () => {
    expect(() => parseWorlds('[{"name":"Broken"}]')).toThrow(/stride/);
    expect(() => parseWorlds('Error: LOD settings unreadable')).toThrow(/LOD settings unreadable/);
    expect(() => parseWorlds('{}')).toThrow(/array/);
  });
});

describe('parsePlan', () => {
  it('keeps rebuild flags and the ledger and commit text', () => {
    const plan = parsePlan(
      '{"plan":[{"world":"Commonwealth","kind":"terrain","level":4,"x":0,"y":0,"inputs":42,"rebuild":true}],' +
        '"ledger":"a 1\\nb 2\\n","commit":"a 1\\n"}',
    );
    expect(plan.plan).toHaveLength(1);
    expect(plan.plan[0]).toMatchObject({ world: 'Commonwealth', level: 4, inputs: 42, rebuild: true });
    expect(plan.ledger).toBe('a 1\nb 2\n');
    expect(plan.commit).toBe('a 1\n');
  });

  it('rejects a tile without coordinates-shaped fields', () => {
    expect(() => parsePlan('{"plan":[{"world":"Commonwealth"}]}')).toThrow(/level/);
  });
});

describe('describeProgress', () => {
  it('reports the pending count and the empty case', () => {
    expect(describeProgress(0, 0)).toBe('no tiles');
    expect(describeProgress(3525, 12)).toBe('12 of 3525 tile(s) need a rebake');
  });
});

describe('parseVerify', () => {
  it('keeps tile and atlas states apart and defaults absent counters', () => {
    const report = parseVerify(JSON.stringify({
      missing: 2,
      ok: 7,
      stale: 1,
      tiles: [
        { world: 'Commonwealth', kind: 'terrain', level: 4, x: 0, y: 0, state: 'missing', local: 'missing',
          ledger: 'stale', bytes: 0, inArchive: false },
        { world: 'Commonwealth', kind: 'object', level: 8, x: 0, y: 0, state: 'ok', local: 'present',
          ledger: 'current', bytes: 4096, inArchive: true },
        { world: 'Commonwealth', kind: 'object', level: 16, x: 0, y: 0, state: 'empty', local: 'unknown',
          ledger: 'stale', bytes: 0, inArchive: false },
      ],
      texturesMissing: 1,
      textures: [
        { world: 'Commonwealth', kind: 'texture', map: 'diffuse', state: 'missing', ledger: 'stale', bytes: 0 },
      ],
    }));
    expect(report.partial).toBe(0);
    expect(report.unverified).toBe(0);
    expect(report.tiles.map(tile => tile.state)).toEqual(['missing', 'ok', 'empty']);
    expect(report.tiles[1].inArchive).toBe(true);
    expect(report.textures[0].map).toBe('diffuse');
    // Constraint: a known-empty tile is not a problem, so the rows that need action exclude it.
    expect(verifyProblems(report).map(tile => tile.state)).toEqual(['missing']);
    expect(describeVerify(report)).toBe('7 ok, 2 missing, 1 stale, 1 atlas map(s) missing');
  });

  it('rejects a host failure and a tile without a state', () => {
    expect(() => parseVerify('Error: LOD atlas row unreadable')).toThrow(/atlas row unreadable/);
    expect(() => parseVerify('{"tiles":[{"world":"Commonwealth"}]}')).toThrow(/state/);
    expect(() => parseVerify('{"textures":[{"world":"Commonwealth"}]}')).toThrow(/map/);
  });
});

describe('parseCandidates', () => {
  it('keeps every skip reason that happened and refuses a candidate without a reference', () => {
    const candidates = parseCandidates(JSON.stringify({
      world: 'Commonwealth',
      level: 8,
      tiles: 4,
      counts: { referencesSeen: 9, included: 3, skipped: { noMesh: 4, multirefParent: 2, other: 0 } },
      candidates: [{ reference: 'Fallout4.esm:0x1234', base: 'Fallout4.esm:0x5678', mesh: 'Architecture\\A.nif' }],
    }));
    expect(candidates.included).toBe(3);
    expect(candidates.tiles).toBe(4);
    expect(candidates.skipped).toEqual([{ reason: 'noMesh', count: 4 }, { reason: 'multirefParent', count: 2 }]);
    expect(describeCandidates(candidates)).toBe('3 of 9 reference(s) enter 4 tile(s)');
    expect(() => parseCandidates('{"candidates":[{"base":"Fallout4.esm:0x1"}]}')).toThrow(/reference/);
    expect(() => parseCandidates('Error: no LOD settings for this world')).toThrow(/no LOD settings/);
  });
});

describe('sameWorld', () => {
  it('folds case because plan worlds are lower-cased', () => {
    expect(sameWorld('testworld', 'TestWorld')).toBe(true);
    expect(sameWorld('TestWorld', 'Commonwealth')).toBe(false);
  });
});

describe('hostFailure', () => {
  it('only flags host error strings', () => {
    expect(hostFailure('[]')).toBeNull();
    expect(hostFailure('Error: nope')).toBe('nope');
  });
});
