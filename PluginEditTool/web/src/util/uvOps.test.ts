import { describe, expect, it } from 'vitest';
import { boxVerts, invert, linked, pickEdge, pickFace, selectedTris, visibleVerts } from './uvOps';

const sorted = (xs: Iterable<number>): number[] => Array.from(xs).sort((a, b) => a - b);

const hideUvs = [0.1, 0.1, 0.3, 0.1, 0.1, 0.3, 0.2, 0.2, 0.4, 0.4];
const hideTris = [0, 1, 2, 2, 3, 4];
const hidden1 = new Set([1]);
const allBox = { minU: 0, minV: 0, maxU: 0.5, maxV: 0.5 };

const edgeUvs = [0, 0, 1, 0, 0, 1, 0.4, 0.12, 0.6, 0.12, 0.5, 0.2];
const edgeTris = [0, 1, 2, 3, 4, 5];

const overlapUvs = [0, 0, 1, 0, 0, 1, 0.1, 0.1, 0.9, 0.1, 0.1, 0.9];
const overlapTris = [0, 1, 2, 3, 4, 5];

describe('visibleVerts', () => {
  it('hides vertices that only a hidden triangle uses but keeps unreferenced vertices', () => {
    expect(sorted(visibleVerts(6, hideTris, hidden1))).toEqual([0, 1, 2, 5]);
  });

  it('ignores a triangle that points past the vertex count', () => {
    expect(sorted(visibleVerts(3, [0, 1, 9], new Set()))).toEqual([0, 1, 2]);
  });
});

describe('pickEdge', () => {
  it('picks the nearest edge and skips a hidden triangle lying on top of it', () => {
    expect(pickEdge(edgeUvs, edgeTris, hidden1, 0.5, 0.1, 0.5)).toEqual([0, 1]);
    expect(pickEdge(edgeUvs, edgeTris, new Set(), 0.5, 0.1, 0.5)).toEqual([3, 4]);
  });

  it('returns null when every edge is beyond maxDist', () => {
    expect(pickEdge(edgeUvs, edgeTris, new Set(), 0.5, 0.1, 0.01)).toBeNull();
  });
});

describe('pickFace', () => {
  it('returns the topmost non-hidden triangle containing the point', () => {
    expect(pickFace(overlapUvs, overlapTris, new Set(), 0.2, 0.2)).toEqual([3, 4, 5]);
    expect(pickFace(overlapUvs, overlapTris, hidden1, 0.2, 0.2)).toEqual([0, 1, 2]);
  });

  it('accepts clockwise winding and points exactly on an edge', () => {
    expect(pickFace(overlapUvs, [0, 2, 1], new Set(), 0.2, 0.2)).toEqual([0, 2, 1]);
    expect(pickFace(overlapUvs, [0, 1, 2], new Set(), 0.5, 0)).toEqual([0, 1, 2]);
  });

  it('skips a zero-area triangle so a collinear or collapsed UV triangle does not swallow every click (detects degenerate triangles containing every point)', () => {
    const uvs = [0, 0, 0.5, 0.5, 1, 1, 0, 0, 1, 0, 0, 1];
    expect(pickFace(uvs, [3, 4, 5, 0, 1, 2], new Set(), 0.2, 0.2)).toEqual([3, 4, 5]);
    expect(pickFace(uvs, [0, 1, 2], new Set(), 0.2, 0.2)).toBeNull();
    expect(pickFace([0.5, 0.5, 0.5, 0.5, 0.5, 0.5], [0, 1, 2], new Set(), 0.2, 0.2)).toBeNull();
  });
});

describe('boxVerts', () => {
  it('drops hidden-only vertices in vertex mode and hidden triangles in face mode', () => {
    expect(boxVerts(hideUvs, hideTris, hidden1, allBox, 'vertex')).toEqual([0, 1, 2]);
    expect(boxVerts(hideUvs, hideTris, hidden1, allBox, 'face')).toEqual([0, 1, 2]);
  });

  it('face mode needs all three corners in the box while edge mode takes the inside edge', () => {
    const uvs = [0.1, 0.1, 0.15, 0.1, 0.1, 0.5];
    const box = { minU: 0, minV: 0, maxU: 0.2, maxV: 0.2 };
    expect(boxVerts(uvs, [0, 1, 2], new Set(), box, 'face')).toEqual([]);
    expect(boxVerts(uvs, [0, 1, 2], new Set(), box, 'edge')).toEqual([0, 1]);
  });
});

describe('invert', () => {
  it('never selects hidden-only vertices when inverting a visible selection', () => {
    const visible = visibleVerts(6, hideTris, hidden1);
    expect(invert(visible, new Set([0]))).toEqual(new Set([1, 2, 5]));
  });
});

describe('linked', () => {
  it('fills the whole island of a selected vertex and not a second island', () => {
    expect(linked([0, 0, 0, 1, 1, 1], new Set([1]))).toEqual([0, 1, 2]);
  });
});

describe('selectedTris', () => {
  it('lists triangles whose three corners are all selected and not hidden', () => {
    const sel = new Set([0, 1, 2, 3]);
    expect(selectedTris([0, 1, 2, 2, 3, 4], 5, sel, new Set())).toEqual([0]);
    expect(selectedTris([0, 1, 2, 2, 3, 4], 5, sel, new Set([0]))).toEqual([]);
  });
});

describe('input immutability', () => {
  it('triangle queries never write to frozen triangle and uv arrays', () => {
    const uvs = Object.freeze(overlapUvs.slice());
    const tris = Object.freeze(overlapTris.slice());
    expect(() => {
      visibleVerts(6, tris, new Set());
      pickEdge(uvs, tris, new Set(), 0.2, 0.2, 1);
      pickFace(uvs, tris, new Set(), 0.2, 0.2);
      boxVerts(uvs, tris, new Set(), allBox, 'face');
      selectedTris(tris, 6, new Set([0, 1, 2]), new Set());
    }).not.toThrow();
  });
});
