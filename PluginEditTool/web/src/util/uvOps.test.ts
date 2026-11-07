import { describe, expect, it } from 'vitest';
import {
  alignSel, boxVerts, clampTile, invert, linked, mergeAtCentre, mergeByDistance,
  pickEdge, pickFace, selectedTris, snapGrid, snapPixels, visibleVerts,
} from './uvOps';

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

describe('mergeAtCentre', () => {
  it('moves selected verts to the bounding-box centre and leaves unselected ones', () => {
    const uvs = [0.25, 0.25, 0.75, 0.25, 0.5, 0.75];
    expect(mergeAtCentre(uvs, new Set([0, 1]))).toEqual([0.5, 0.25, 0.5, 0.25, 0.5, 0.75]);
  });
});

describe('mergeByDistance', () => {
  it('welds near verts at or within dist to their mean and leaves far and unselected verts alone', () => {
    const uvs = [0.5, 0.5, 0.75, 0.5, 0.9, 0.9, 0.625, 0.5];
    expect(mergeByDistance(uvs, new Set([0, 1, 2]), 0.25)).toEqual([0.625, 0.5, 0.625, 0.5, 0.9, 0.9, 0.625, 0.5]);
  });

  it('clusters a chain transitively when the ends are farther apart than dist (detects welding only direct pairs)', () => {
    const uvs = [0, 0, 0.125, 0, 0.25, 0];
    expect(mergeByDistance(uvs, new Set([0, 1, 2]), 0.1875)).toEqual([0.125, 0, 0.125, 0, 0.125, 0]);
  });

  it('welds verts that straddle a grid cell boundary (detects a neighbour search limited to the own cell)', () => {
    const uvs = [0.1875, 0.5, 0.3125, 0.5];
    expect(mergeByDistance(uvs, new Set([0, 1]), 0.25)).toEqual([0.25, 0.5, 0.25, 0.5]);
  });

  it('returns an unchanged copy for a 1e-17 distance instead of spinning on overflowed grid cells (detects the infinite neighbour loop hang)', () => {
    const uvs = [0.5, 0.5, 0.75, 0.5];
    expect(mergeByDistance(uvs, new Set([0, 1]), 1e-17)).toEqual(uvs);
  });
});

describe('alignSel', () => {
  it('aligns the chosen component of selected verts to min, centre or max', () => {
    const uvs = [0.25, 0.5, 0.75, 0.25, 0.5, 0.75];
    const sel = new Set([0, 2]);
    expect(alignSel(uvs, sel, 'u', 'min')).toEqual([0.25, 0.5, 0.75, 0.25, 0.25, 0.75]);
    expect(alignSel(uvs, sel, 'u', 'centre')).toEqual([0.375, 0.5, 0.75, 0.25, 0.375, 0.75]);
    expect(alignSel(uvs, sel, 'u', 'max')).toEqual([0.5, 0.5, 0.75, 0.25, 0.5, 0.75]);
  });
});

describe('snapGrid', () => {
  it('rounds selected verts to the step and returns a copy for a zero step', () => {
    const uvs = [0.3, 0.6, 0.3, 0.6];
    expect(snapGrid(uvs, new Set([0]), 0.25)).toEqual([0.25, 0.5, 0.3, 0.6]);
    expect(snapGrid(uvs, new Set([0]), 0)).toEqual(uvs);
  });
});

describe('snapPixels', () => {
  it('snaps to texel corners or texel centres and returns a copy for a zero size', () => {
    const uvs = [0.3, 0.6, 0.3, 0.6];
    expect(snapPixels(uvs, new Set([0]), 4, 4, false)).toEqual([0.25, 0.5, 0.3, 0.6]);
    expect(snapPixels(uvs, new Set([0]), 4, 4, true)).toEqual([0.375, 0.625, 0.3, 0.6]);
    expect(snapPixels(uvs, new Set([0]), 0, 4, false)).toEqual(uvs);
  });

  it('keeps a centre-snapped UV of exactly 1.0 inside the tile (detects the 1.125 overshoot past the last texel)', () => {
    expect(snapPixels([1, 1, 0.3, 0.6], new Set([0]), 4, 4, true)).toEqual([0.875, 0.875, 0.3, 0.6]);
  });
});

describe('clampTile', () => {
  it('clamps selected verts into 0-1 and leaves in-range and unselected values unchanged', () => {
    const uvs = [-0.2, 0.5, 1.4, 1.2, 0.3, 0.7, 1.5, -0.1];
    expect(clampTile(uvs, new Set([0, 1, 2]))).toEqual([0, 0.5, 1, 1, 0.3, 0.7, 1.5, -0.1]);
  });
});

describe('input immutability', () => {
  it('uv-returning operators never write to a frozen uvs array', () => {
    const uvs = Object.freeze([0.3, 0.6, 1.4, -0.2, 0.5, 0.5]);
    const sel = new Set([0, 1]);
    expect(() => {
      snapGrid(uvs, sel, 0.25);
      snapPixels(uvs, sel, 4, 4, true);
      clampTile(uvs, sel);
      mergeAtCentre(uvs, sel);
      mergeByDistance(uvs, sel, 1);
      alignSel(uvs, sel, 'v', 'max');
    }).not.toThrow();
  });

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
