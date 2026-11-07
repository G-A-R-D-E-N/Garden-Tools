import { describe, expect, it } from 'vitest';
import { averageIslandScale, packIslands, triStretch } from './uvPack';

const expectNear = (actual: readonly number[], expected: readonly number[]): void => {
  expect(actual.length).toBe(expected.length);
  actual.forEach((x, i) => expect(x).toBeCloseTo(expected[i], 5));
};

const squares = [0, 0, 0.5, 0, 0.5, 0.5, 0, 0.5, 0.6, 0.6, 0.9, 0.6, 0.9, 0.9, 0.6, 0.9];
const squareIds = [0, 0, 0, 0, 1, 1, 1, 1];
const bothSquares = new Set([0, 4]);

const scaleVerts = [0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 2, 2, 0, 2, 2, 2, 2, 0, 2, 2];
const scaleUvs = [0, 0, 0.5, 0, 0.5, 0.5, 0, 0.5, 0.5, 0.5, 1, 0.5, 1, 1, 0.5, 1];
const scaleTris = [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7];
const scaleIds = [0, 0, 0, 0, 1, 1, 1, 1];

describe('packIslands', () => {
  it('packs a large and a small square into the tile with a 0.1 margin and their 5:3 size ratio kept (detects overlap, lost margin or per-island scaling)', () => {
    expectNear(packIslands(squares, squareIds, bothSquares, 0.1), [
      0.1, 0.1, 0.5375, 0.1, 0.5375, 0.5375, 0.1, 0.5375,
      0.6375, 0.1, 0.9, 0.1, 0.9, 0.3625, 0.6375, 0.3625,
    ]);
  });

  it('moves a whole island when only one of its vertices is selected (detects moving only the selected vertices)', () => {
    const out = packIslands(squares, squareIds, new Set([0]), 0.1);
    expectNear(out.slice(0, 8), [0.1, 0.1, 0.9, 0.1, 0.9, 0.9, 0.1, 0.9]);
    expect(out.slice(8)).toEqual(squares.slice(8));
  });

  it('leaves an island with no selected vertex exactly as it was (detects packing every island)', () => {
    const withThird = [...squares, 0.2, 0.7, 0.3, 0.7, 0.25, 0.8];
    const out = packIslands(withThird, [...squareIds, 2, 2, 2], bothSquares, 0.1);
    expect(out.slice(16)).toEqual([0.2, 0.7, 0.3, 0.7, 0.25, 0.8]);
    expect(out[0]).toBeCloseTo(0.1, 5);
  });

  it('returns an equal copy when nothing is selected', () => {
    expect(packIslands(squares, squareIds, new Set(), 0.1)).toEqual(squares);
  });
});

describe('averageIslandScale', () => {
  it('scales two islands to the area-weighted mean stretch so both UV/3D ratios become 0.3 (detects an unweighted mean or scaling about the origin)', () => {
    expectNear(averageIslandScale(scaleUvs, scaleVerts, scaleTris, scaleIds, bothSquares), [
      0.1, 0.1, 0.4, 0.1, 0.4, 0.4, 0.1, 0.4,
      0.45, 0.45, 1.05, 0.45, 1.05, 1.05, 0.45, 1.05,
    ]);
  });

  it('leaves a single island unchanged because there is nothing to average against', () => {
    const out = averageIslandScale(scaleUvs.slice(0, 8), scaleVerts.slice(0, 12), [0, 1, 2, 0, 2, 3], [0, 0, 0, 0], new Set([0]));
    expect(out).toEqual(scaleUvs.slice(0, 8));
  });
});

describe('triStretch', () => {
  it('scores a uniformly scaled mesh 0 on every triangle (detects a stretch measure that ignores the mean)', () => {
    expectNear(triStretch([0, 0, 2, 0, 2, 2, 0, 2], [0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0], [0, 1, 2, 0, 2, 3]), [0, 0]);
  });

  it('scores a ratio of 2 and a ratio of 1/2 around a mean of 1 as 0.5 each (detects a missing ln or ln 4 normalisation)', () => {
    const verts = [0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 5, 2, 0, 5, 0, 2, 5];
    const uvs = [0, 0, 4, 0, 0, 1, 0, 0, 1, 0, 0, 2];
    expectNear(triStretch(uvs, verts, [0, 1, 2, 3, 4, 5]), [0.5, 0.5]);
  });

  it('clamps an extreme stretch to 1 (detects unclamped values above 1)', () => {
    const verts = [0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 5, 4, 0, 5, 0, 2, 5];
    const uvs = [0, 0, 4, 0, 0, 2, 0, 0, 1, 0, 0, 1];
    expectNear(triStretch(uvs, verts, [0, 1, 2, 3, 4, 5]), [1, 1]);
  });

  it('gives one value per whole triangle and 0 to a triangle that points past the vertex count (detects reading past vertexCount or a partial trailing triangle)', () => {
    const verts = [0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 5, 4, 0, 5, 0, 2, 5];
    const uvs = [0, 0, 4, 0, 0, 2, 0, 0, 1, 0, 0, 1];
    expectNear(triStretch(uvs, verts, [0, 1, 2, 3, 4, 5, 0, 1, 99, 7]), [1, 1, 0]);
  });
});

describe('guards', () => {
  it('treats a NaN pack margin as zero instead of writing NaN UVs (detects an unguarded margin)', () => {
    const out = packIslands(squares, squareIds, bothSquares, NaN);
    expect(out).toEqual(packIslands(squares, squareIds, bothSquares, 0));
    out.forEach(x => expect(Number.isFinite(x)).toBe(true));
  });

  it('clamps the average-scale factor so a tiny-but-valid island cannot blow UVs past the half-float range (detects an unclamped k)', () => {
    const verts = [...scaleVerts.slice(0, 12), 0, 2, 0, 1, 2, 0, 1, 3, 0, 0, 3, 0];
    const uvs = [...scaleUvs.slice(0, 8), 0, 0, 1e-4, 0, 1e-4, 1e-4, 0, 1e-4];
    const tris = [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7];
    const out = averageIslandScale(uvs, verts, tris, [0, 0, 0, 0, 1, 1, 1, 1], bothSquares);
    out.forEach(x => {
      expect(Number.isFinite(x)).toBe(true);
      expect(Math.abs(x)).toBeLessThanOrEqual(65504);
    });
    const small = out.slice(8);
    expect(Math.max(...small) - Math.min(...small)).toBeCloseTo(1e-1, 5);
  });
});

describe('input immutability', () => {
  it('never writes to frozen uvs, verts, tris or ids (detects in-place edits of caller arrays)', () => {
    const uvs = Object.freeze(scaleUvs.slice());
    const verts = Object.freeze(scaleVerts.slice());
    const tris = Object.freeze(scaleTris.slice());
    const ids = Object.freeze(scaleIds.slice());
    expect(() => {
      packIslands(uvs, ids, bothSquares, 0.1);
      averageIslandScale(uvs, verts, tris, ids, bothSquares);
      triStretch(uvs, verts, tris);
    }).not.toThrow();
  });
});
