import { describe, expect, it } from 'vitest';
import { bounds, frameView, islands, median, pickNearest, pointInBox, transformSel } from './uvMath';

const uvs = [0.25, 0.5, 0.75, 0.5, 0.5, 0.25];
const all = new Set([0, 1, 2]);
const origin = { u: 0, v: 0 };

describe('islands', () => {
  it('counts two disjoint triangles as two islands', () => {
    expect(islands(6, [0, 1, 2, 3, 4, 5])).toEqual([0, 0, 0, 1, 1, 1]);
  });

  it('joins triangles that share a vertex into one island', () => {
    expect(islands(5, [0, 1, 2, 2, 3, 4])).toEqual([0, 0, 0, 0, 0]);
  });

  it('gives an unreferenced vertex its own island', () => {
    expect(islands(4, [0, 1, 2])).toEqual([0, 0, 0, 1]);
  });
});

describe('transformSel', () => {
  it('moves only the selected vertices', () => {
    const out = transformSel(uvs, new Set([1]), { tx: 0.25, ty: -0.25, sx: 1, sy: 1, rotRad: 0, pivot: origin });
    expect(out).toEqual([0.25, 0.5, 1, 0.25, 0.5, 0.25]);
  });

  it('scales about the pivot', () => {
    const out = transformSel(uvs, all, { tx: 0, ty: 0, sx: 2, sy: 1, rotRad: 0, pivot: { u: 0.5, v: 0.5 } });
    expect(out).toEqual([0, 0.5, 1, 0.5, 0.5, 0.25]);
  });

  it('rotates about the pivot', () => {
    const out = transformSel(uvs, new Set([0]), { tx: 0, ty: 0, sx: 1, sy: 1, rotRad: Math.PI / 2, pivot: { u: 0.5, v: 0.5 } });
    expect(out[0]).toBeCloseTo(0.5);
    expect(out[1]).toBeCloseTo(0.25);
    expect(out.slice(2)).toEqual(uvs.slice(2));
  });

  it('returns a new array and leaves the input untouched', () => {
    const out = transformSel(uvs, all, { tx: 1, ty: 1, sx: 1, sy: 1, rotRad: 0, pivot: origin });
    expect(out).not.toBe(uvs);
    expect(uvs[0]).toBe(0.25);
  });
});

describe('bounds and median', () => {
  it('bounds the selection and is null for an empty selection', () => {
    expect(bounds(uvs, new Set([0, 2]))).toEqual({ minU: 0.25, minV: 0.25, maxU: 0.5, maxV: 0.5 });
    expect(bounds(uvs, new Set())).toBeNull();
  });

  it('takes the per-axis median', () => {
    expect(median(uvs, all)).toEqual({ u: 0.5, v: 0.5 });
  });
});

describe('pick', () => {
  it('returns the nearest vertex inside the radius', () => {
    expect(pickNearest(uvs, 0.74, 0.5, 0.05)).toBe(1);
  });

  it('returns -1 when nothing is in the radius', () => {
    expect(pickNearest(uvs, 0, 0, 0.05)).toBe(-1);
  });

  it('tests points against a box inclusively', () => {
    const box = { minU: 0, minV: 0, maxU: 1, maxV: 1 };
    expect(pointInBox(0.5, 0.5, box)).toBe(true);
    expect(pointInBox(1.5, 0.5, box)).toBe(false);
  });
});

describe('frameView', () => {
  it('fits the 0-1 tile and the shape inside the canvas', () => {
    const v = frameView([0.25, 0.25, 0.75, 0.75], 800, 600);
    expect(v.scale).toBeCloseTo(540);
    expect(v.px).toBeCloseTo(130);
    expect(v.py).toBeCloseTo(30);
  });
});
