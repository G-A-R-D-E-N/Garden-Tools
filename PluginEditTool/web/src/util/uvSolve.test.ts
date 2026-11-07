import { describe, expect, it } from 'vitest';
import { boundaryVerts, findStitchPairs, overlappingFaces, relaxUvs, stitchSelection } from './uvSolve';

const allSel = (n: number): Set<number> => new Set(Array.from({ length: n }, (_, i) => i));
const near = (xs: readonly number[], want: readonly number[]): void => want.forEach((w, i) => expect(xs[i]).toBeCloseTo(w, 6));
const dist = (xs: readonly number[], a: number, b: number): number => Math.hypot(xs[2 * a] - xs[2 * b], xs[2 * a + 1] - xs[2 * b + 1]);

const fanTris = [0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1];
const fanUvs = [0.2, 0.2, 0, 0, 1, 0, 1, 1, 0, 1, 0.9, 0.9];

const splitVerts = [0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, -1, 0];
const splitIds = [0, 0, 0, 1, 1, 1];

const squareUvs = [0, 0, 1, 0, 1, 1, 0, 1, 2, 2, 2, 3, 1, 3, 1, 2, 5, 5, 6, 5, 5, 6];
const squareVerts = [0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, -1, 0, 0, -1, 0, 10, 10, 0, 11, 10, 0, 10, 11, 0];
const squareIds = [0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2];
const squareTris = [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10];
const squareSel = allSel(11);

describe('boundaryVerts', () => {
  it('keeps the fan centre interior and flags only the rim (detects marking every corner of a triangle as boundary)', () => {
    expect(Array.from(boundaryVerts(5, fanTris)).sort((p, q) => p - q)).toEqual([1, 2, 3, 4]);
  });

  it('ignores a triangle that points past the vertex count (detects an out-of-range index leaking into the boundary)', () => {
    expect(Array.from(boundaryVerts(3, [0, 1, 2, 0, 1, 9])).sort((p, q) => p - q)).toEqual([0, 1, 2]);
  });
});

describe('relaxUvs', () => {
  it('moves only the selected interior vertex to the mean of its neighbours (detects boundary or isolated vertices drifting)', () => {
    expect(relaxUvs(fanUvs, fanTris, new Set([0, 1, 5]), 1)).toEqual([0.5, 0.5, 0, 0, 1, 0, 1, 1, 0, 1, 0.9, 0.9]);
  });

  it('leaves an interior vertex still when it is not selected (detects ignoring the selection set)', () => {
    expect(relaxUvs(fanUvs, fanTris, new Set([1]), 3)).toEqual(fanUvs);
  });

  it('is a no-op for zero iterations (detects an off-by-one pass count)', () => {
    expect(relaxUvs(fanUvs, fanTris, new Set([0]), 0)).toEqual(fanUvs);
  });
});

describe('findStitchPairs', () => {
  it('pairs split vertices across two islands in index order (detects missing or unsorted pairs)', () => {
    expect(findStitchPairs(splitVerts, splitIds, allSel(6), 1e-6)).toEqual([[0, 4], [1, 3]]);
  });

  it('skips unselected vertices and coincident vertices of the same island (detects pairing within one island or ignoring sel)', () => {
    expect(findStitchPairs(splitVerts, splitIds, new Set([0, 1, 2, 3, 5]), 1e-6)).toEqual([[1, 3]]);
    expect(findStitchPairs([0, 0, 0, 0, 0, 0], [0, 0], allSel(2), 1e-6)).toEqual([]);
  });

  it('finds a pair whose vertices sit in adjacent grid cells (detects a neighbour search limited to the own cell)', () => {
    expect(findStitchPairs([0.99, 0, 0, 1.01, 0, 0, 1.2, 0, 0], [0, 1, 1], allSel(3), 0.05)).toEqual([[0, 1]]);
  });

  it('returns no pairs for a 1e-17 or NaN tolerance without spinning (detects the tiny-cell neighbour loop hang)', () => {
    expect(findStitchPairs(splitVerts, splitIds, allSel(6), 1e-17)).toEqual([]);
    expect(findStitchPairs(splitVerts, splitIds, allSel(6), NaN)).toEqual([]);
  });
});

describe('stitchSelection', () => {
  it('rotates and offsets the mobile square onto its neighbour so paired UVs coincide (detects a translation-only or unrotated fit)', () => {
    const out = stitchSelection(squareUvs, squareVerts, squareIds, squareSel, 1e-6);
    near(out, [0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 5, 5, 6, 5, 5, 6]);
    expect(dist(out, 0, 4)).toBeCloseTo(0, 6);
    expect(dist(out, 1, 5)).toBeCloseTo(0, 6);
  });

  it('keeps the mobile island edge lengths unchanged (detects scaling or reshaping the mobile island)', () => {
    const out = stitchSelection(squareUvs, squareVerts, squareIds, squareSel, 1e-6);
    [[4, 5], [5, 6], [6, 7], [7, 4]].forEach(([a, b]) => expect(dist(out, a, b)).toBeCloseTo(dist(squareUvs, a, b), 6));
  });

  it('leaves an island with no pair to the anchor untouched (detects a transform applied to every island)', () => {
    expect(stitchSelection(squareUvs, squareVerts, squareIds, squareSel, 1e-6).slice(16)).toEqual([5, 5, 6, 5, 5, 6]);
  });
});

describe('overlappingFaces', () => {
  it('reports two identical stacked triangles (detects a separating-axis test that misses full overlap)', () => {
    expect(overlappingFaces([0, 0, 1, 0, 0, 1], [0, 1, 2, 0, 1, 2], new Set())).toEqual([0, 1]);
  });

  it('does not report triangles that share an edge or only a corner (detects touching triangles counted as overlapping)', () => {
    expect(overlappingFaces([0, 0, 1, 0, 0, 1, 1, 1], [0, 1, 2, 1, 2, 3], new Set())).toEqual([]);
    expect(overlappingFaces([0, 0, 1, 0, 0, 1, 2, 0, 2, -1], [0, 1, 2, 1, 3, 4], new Set())).toEqual([]);
  });

  it('ignores hidden triangles (detects hidden faces still counting as overlaps)', () => {
    expect(overlappingFaces([0, 0, 1, 0, 0, 1], [0, 1, 2, 0, 1, 2], new Set([1]))).toEqual([]);
  });

  it('ignores degenerate and NaN triangles (detects zero-area or NaN faces being reported)', () => {
    const uvs = [0, 0, 1, 0, 0, 1, 0.2, 0.2, 0.4, 0.4, 0.6, 0.6];
    expect(overlappingFaces(uvs, [0, 1, 2, 3, 4, 5], new Set())).toEqual([]);
    expect(overlappingFaces(uvs, [0, 1, 2, 0, 1, 2, 3, 4, 5], new Set())).toEqual([0, 1]);
    const nanUvs = [0, 0, 1, 0, 0, 1, NaN, NaN, NaN, NaN, NaN, NaN];
    expect(overlappingFaces(nanUvs, [0, 1, 2, 3, 4, 5, 0, 1, 2], new Set())).toEqual([0, 2]);
  });
});

describe('stitch groups', () => {
  it('stitches a second independent seam group that shares no island with the first (detects a single-component sweep)', () => {
    const verts = [...splitVerts, 5, 0, 0, 6, 0, 0, 5, 1, 0, 5, 0, 0, 6, 0, 0, 5, -1, 0];
    const ids = [...splitIds, 2, 2, 2, 3, 3, 3];
    const uvs = [...squareUvs.slice(0, 12), 0.6, 0.6, 0.8, 0.6, 0.6, 0.8, 0.1, 0.1, 0.3, 0.1, 0.1, 0.3];
    const out = stitchSelection(uvs, verts, ids, allSel(12), 1e-6);
    expect(dist(out, 0, 4)).toBeCloseTo(0, 6);
    expect(dist(out, 6, 9)).toBeCloseTo(0, 6);
    expect(dist(out, 7, 10)).toBeCloseTo(0, 6);
    near(out.slice(18, 24), [0.6, 0.6, 0.8, 0.6, 0.6, 0.8]);
  });
});

describe('overlap with stray uvs', () => {
  it('reports only the overlapping faces when a third triangle sits far outside the tile (detects a clamped global grid)', () => {
    expect(overlappingFaces([0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1000, 1000, 1001, 1000, 1000, 1001],
      [0, 1, 2, 3, 4, 5, 6, 7, 8], new Set())).toEqual([0, 1]);
  });
});

describe('input immutability', () => {
  it('never writes to frozen uvs, vertex, id or triangle arrays (detects in-place edits of caller data)', () => {
    const uvs = Object.freeze(squareUvs.slice());
    const verts = Object.freeze(squareVerts.slice());
    const ids = Object.freeze(squareIds.slice());
    const tris = Object.freeze(squareTris.slice());
    expect(() => {
      boundaryVerts(11, tris);
      relaxUvs(uvs, tris, squareSel, 5);
      findStitchPairs(verts, ids, squareSel, 1e-6);
      stitchSelection(uvs, verts, ids, squareSel, 1e-6);
      overlappingFaces(uvs, tris, new Set());
    }).not.toThrow();
  });
});
