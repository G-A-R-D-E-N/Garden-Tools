import { describe, expect, it } from 'vitest';
import { edgeKey, seamsInSelection, splitPlan } from './uvSplit';

const stripTris = [0, 1, 2, 0, 2, 3, 1, 4, 5, 1, 5, 2];
const fanTris = [0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1];
const allSel = (n: number): Set<number> => new Set(Array.from({ length: n }, (_, i) => i));

describe('seamsInSelection', () => {
  it('marks every mesh edge whose two vertices are both selected (detects marking missing edges or fabricating them)', () => {
    expect(Array.from(seamsInSelection(stripTris, new Set([1, 2])))).toEqual([edgeKey(1, 2)]);
    expect(Array.from(seamsInSelection(stripTris, new Set([0, 1, 2, 3]))).sort((p, q) => p - q))
      .toEqual([edgeKey(0, 1), edgeKey(0, 2), edgeKey(0, 3), edgeKey(1, 2), edgeKey(2, 3)].sort((p, q) => p - q));
    expect(seamsInSelection(stripTris, new Set()).size).toBe(0);
  });

  it('skips hidden faces when marking (detects seams marked on invisible geometry)', () => {
    expect(seamsInSelection(stripTris, new Set([1, 2]), new Set([0, 3])).size).toBe(0);
  });
});

describe('splitPlan', () => {
  it('splits a reconnecting bowtie vertex even with no seam (known part-1 behaviour: duplicates appear without a visible seam change)', () => {
    const bowtie = [0, 1, 2, 0, 3, 4];
    expect(splitPlan(5, bowtie, new Set())).toEqual({ sources: [0], tris: [0, 1, 2, 5, 3, 4] });
  });

  it('leaves the topology exactly as it was when no edge is a seam', () => {
    expect(splitPlan(6, stripTris, new Set())).toEqual({ sources: [], tris: stripTris });
  });

  it('leaves already separated islands alone because their vertices are single-use', () => {
    const four = [0, 1, 2, 3, 4, 5, 6, 7, 8];
    expect(splitPlan(9, four, new Set())).toEqual({ sources: [], tris: four });
  });

  it('duplicates only the seam vertices and keeps the non-seam side on the original index (detects splitting every shared vertex)', () => {
    expect(splitPlan(6, stripTris, new Set([edgeKey(1, 2)])))
      .toEqual({ sources: [1, 2], tris: [0, 1, 2, 0, 2, 3, 6, 4, 5, 6, 5, 7] });
  });

  it('gives every triangle its own vertex copy when all edges are seams (detects a partial split)', () => {
    const seams = seamsInSelection(fanTris, allSel(5));
    const plan = splitPlan(5, fanTris, seams);
    expect(plan.sources.length).toBe(7);
    expect(plan.tris.length).toBe(12);
    expect(new Set(plan.tris).size).toBe(12);
  });

  it('appends one created vertex per source entry in vertex order (detects an unstable duplicate order)', () => {
    const plan = splitPlan(6, stripTris, new Set([edgeKey(1, 2)]));
    expect(plan.sources).toEqual([1, 2]);
    expect(plan.tris.filter(i => i >= 6).length).toBe(3);
  });

  it('never writes to its inputs and is deterministic (detects in-place edits or hidden state)', () => {
    const tris = Object.freeze([...stripTris]);
    const seams = Object.freeze(new Set([edgeKey(1, 2)]));
    const first = splitPlan(6, tris, seams);
    expect(first).toEqual(splitPlan(6, tris, seams));
    expect(tris).toEqual(stripTris);
    expect(Array.from(seams)).toEqual([edgeKey(1, 2)]);
  });
});
