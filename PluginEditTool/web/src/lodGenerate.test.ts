import { describe, expect, it } from 'vitest';
import { describeGenerate, parseGenerate } from './lodGenerate';

// Constraint: the native summary omits a zero counter, so the parser must read an absent one as zero.
const MIXED_RUN = JSON.stringify({
  report: {
    apply: true,
    written: 4,
    unsupportedCandidates: 2,
    objectDrops: { multiref_child: 3, unsupported_base: 2, no_lod_model: 0 },
    texturesWritten: 3,
    terrainTexturesWritten: 8,
    empty: 2,
    atlasEntries: 12,
    atlasMissingTextures: 1,
    objectMissingMeshes: 1,
    textures: [
      { world: 'Commonwealth', kind: 'texture', map: 'diffuse', side: 2048, status: 'written', bytes: 1024 },
      { world: 'Commonwealth', kind: 'texture', map: 'normal', side: 2048, status: 'failed', bytes: 0, error: 'bake failed' },
    ],
    tiles: [
      { world: 'Commonwealth', kind: 'terrain', level: 32, x: 0, y: 0, status: 'written', bytes: 4096 },
      { world: 'Commonwealth', kind: 'object', level: 4, x: 0, y: 0, status: 'written', bytes: 8192 },
    ],
  },
  ledger: 'terrain 32 0 0 4\nobject 4 0 0 7\n',
});

const CLEAN_RUN = JSON.stringify({ report: { apply: true, tiles: [], textures: [] }, ledger: '' });

describe('parseGenerate', () => {
  // Detects: a parser that needs the zero counters present, which would turn every clean repeat run into an error.
  it('reads a summary with omitted counters as zero and keeps the tile, atlas and object counts apart', () => {
    const result = parseGenerate(MIXED_RUN);
    expect(result.report).toEqual({
      apply: true,
      written: 4,
      wouldWrite: 0,
      unsupportedCandidates: 2,
      objectDrops: [
        { reason: 'multiref_child', count: 3 },
        { reason: 'unsupported_base', count: 2 },
      ],
      failed: 0,
      unreadableWorlds: 0,
      empty: 2,
      atlasEntries: 12,
      atlasMissingTextures: 1,
      atlasClampedShapes: 0,
      objectMissingMeshes: 1,
      objectDroppedTriangles: 0,
      objectsSkipped: '',
      cancelled: false,
      texturesWritten: 3,
      texturesSkipped: 0,
      terrainTexturesWritten: 8,
      terrainTexturesSkipped: 0,
      terrainTexturesEmpty: 0,
      textures: [
        { world: 'Commonwealth', kind: 'texture', map: 'diffuse', side: 2048, status: 'written', bytes: 1024, error: '' },
        { world: 'Commonwealth', kind: 'texture', map: 'normal', side: 2048, status: 'failed', bytes: 0, error: 'bake failed' },
      ],
      tiles: [
        { world: 'Commonwealth', kind: 'terrain', level: 32, x: 0, y: 0, status: 'written', bytes: 4096, error: '' },
        { world: 'Commonwealth', kind: 'object', level: 4, x: 0, y: 0, status: 'written', bytes: 8192, error: '' },
      ],
    });
    expect(result.ledger).toBe('terrain 32 0 0 4\nobject 4 0 0 7\n');
    expect(result.report.textures.filter(texture => texture.status === 'failed')).toEqual([
      { world: 'Commonwealth', kind: 'texture', map: 'normal', side: 2048, status: 'failed', bytes: 0, error: 'bake failed' },
    ]);
    expect(describeGenerate(result.report)).toBe(
      '4 tile(s) written, 0 failed (2 unsupported object(s)), 2 empty tile(s), 11 texture map(s) written, ' +
      '1 missing mesh(es), 1 missing atlas source(s)',
    );
    expect(parseGenerate(CLEAN_RUN).report.written).toBe(0);
    expect(describeGenerate(parseGenerate(CLEAN_RUN).report)).toBe(
      '0 tile(s) written, 0 failed (0 unsupported object(s))',
    );
  });

  // Detects: dropping the unreadable-world count when only it and the tile count are present, hiding a skipped world.
  it('keeps an unreadable-world count readable when only it and the tile count are present', () => {
    const report = parseGenerate(JSON.stringify({
      report: { apply: true, written: 1, failed: 1, unreadableWorlds: 2, tiles: [] },
      ledger: '',
    })).report;
    expect(describeGenerate(report)).toBe(
      '1 tile(s) written, 1 failed (0 unsupported object(s)), 2 unreadable world(s)',
    );
    expect(report.textures).toEqual([]);
    expect(report.empty).toBe(0);
  });

  // Detects (invariant): a level-scoped run must say why it wrote no object tile, not report a bare zero.
  it('shows the reason a partial level scope skipped the object stage', () => {
    const report = parseGenerate(JSON.stringify({
      report: { apply: true, written: 2, objectsSkipped: 'level scope', scope: { levelMin: 8, levelMax: 8 }, tiles: [] },
      ledger: '',
    })).report;
    expect(report.objectsSkipped).toBe('level scope');
    expect(describeGenerate(report)).toBe(
      '2 tile(s) written, 0 failed (0 unsupported object(s)), object tiles skipped (level scope)',
    );
    expect(parseGenerate(CLEAN_RUN).report.objectsSkipped).toBe('');
  });
});
