import { describe, expect, it } from 'vitest';
import { islandPlanes, smartProject, smartProjectInfo } from './uvProject';

const all = (n: number): Set<number> => new Set(Array.from({ length: n }, (_, i) => i));
const near = (actual: readonly number[], want: readonly number[]): void => {
  expect(actual.length).toBe(want.length);
  want.forEach((w, i) => expect(actual[i]).toBeCloseTo(w, 5));
};

const quadVerts = [0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0];
const quadTris = [0, 1, 2, 0, 2, 3];
const quadIds = [0, 0, 0, 0];
const quadUvs = [0, 0, 0, 0, 0, 0, 0, 0];
const s2 = Math.SQRT2;
const tiltedVerts = [0, 0, 0, 2, 0, 0, 2, s2, s2, 0, s2, s2];

const bbox = (uvs: readonly number[]): number[] => {
  const u = uvs.filter((_, i) => i % 2 === 0), v = uvs.filter((_, i) => i % 2 === 1);
  return [Math.min(...u), Math.max(...u), Math.min(...v), Math.max(...v)];
};

const scaleOf = (uvs: readonly number[], verts: readonly number[], a: number, b: number): number => {
  const flat = Math.hypot(uvs[2 * a] - uvs[2 * b], uvs[2 * a + 1] - uvs[2 * b + 1]);
  const solid = Math.hypot(verts[3 * a] - verts[3 * b], verts[3 * a + 1] - verts[3 * b + 1], verts[3 * a + 2] - verts[3 * b + 2]);
  return flat / solid;
};

const twoIslandsVerts = [...quadVerts, 5, 5, 0, 6, 5, 0, 6, 6, 0, 5, 6, 0];
const twoIslandsTris = [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7];
const twoIslandsIds = [0, 0, 0, 0, 1, 1, 1, 1];

describe('smartProject', () => {
  it('projects a flat quad into the tile with one uniform scale for every edge (detects a missing or non-uniform projection)', () => {
    const out = smartProject(quadUvs, quadVerts, quadTris, quadIds, all(4));
    near(bbox(out), [0.01, 0.99, 0.01, 0.99]);
    const s = scaleOf(out, quadVerts, 0, 1);
    for (const [a, b] of [[0, 1], [1, 2], [2, 3], [3, 0], [0, 2], [1, 3]]) {
      expect(scaleOf(out, quadVerts, a, b)).toBeCloseTo(s, 4);
    }
  });

  it('projects a quad in a tilted plane with the same uniform scale (detects a world-axis projection instead of the island plane)', () => {
    const out = smartProject(quadUvs, tiltedVerts, quadTris, quadIds, all(4));
    const s = scaleOf(out, tiltedVerts, 0, 1);
    for (const [a, b] of [[1, 2], [2, 3], [3, 0], [0, 2]]) {
      expect(scaleOf(out, tiltedVerts, a, b)).toBeCloseTo(s, 4);
    }
  });

  it('keeps texel density equal across two differently sized islands (detects scaling each island to fill its own slot)', () => {
    const out = smartProject([...quadUvs, 0, 0, 0, 0, 0, 0, 0, 0], twoIslandsVerts, twoIslandsTris, twoIslandsIds, all(8));
    const big = bbox(out.slice(0, 8)), small = bbox(out.slice(8));
    expect((big[1] - big[0]) / (small[1] - small[0])).toBeCloseTo(2, 3);
    out.forEach(x => {
      expect(Number.isFinite(x)).toBe(true);
      expect(x).toBeGreaterThanOrEqual(-1e-6);
      expect(x).toBeLessThanOrEqual(1 + 1e-6);
    });
  });

  it('passes the pack margin through (detects a hardcoded margin)', () => {
    near(bbox(smartProject(quadUvs, quadVerts, quadTris, quadIds, all(4), 0.1)), [0.1, 0.9, 0.1, 0.9]);
  });

  it('projects the whole island when only one of its vertices is selected (detects a per-vertex projection)', () => {
    const info = smartProjectInfo(quadUvs, quadVerts, quadTris, quadIds, new Set([0]));
    expect(info.projected).toBe(4);
    expect(info.skipped).toBe(0);
    near(bbox(info.uvs), bbox(smartProject(quadUvs, quadVerts, quadTris, quadIds, all(4))));
  });

  it('leaves an island with no selected vertex exactly as it was (detects projecting every island)', () => {
    const tail = [0.4, 0.4, 0.5, 0.4, 0.5, 0.5, 0.4, 0.5];
    const out = smartProject([...quadUvs, ...tail], twoIslandsVerts, twoIslandsTris, twoIslandsIds, all(4));
    expect(out.slice(8)).toEqual(tail);
  });

  it('returns an equal copy when nothing is selected', () => {
    expect(smartProject(quadUvs, quadVerts, quadTris, quadIds, new Set())).toEqual(quadUvs);
  });

  it('skips a closed-shell island whose normals cancel and never writes NaN (detects a cancelled normal normalised into noise)', () => {
    const verts = [0, 0, 0, 1, 0, 0, 0, 1, 0];
    const info = smartProjectInfo([0, 0, 0, 0, 0, 0], verts, [0, 1, 2, 0, 2, 1], [0, 0, 0], all(3));
    expect(info.projected).toBe(0);
    expect(info.skipped).toBe(1);
    expect(info.uvs).toEqual([0, 0, 0, 0, 0, 0]);
    expect(islandPlanes(verts, [0, 1, 2, 0, 2, 1], [0, 0, 0], all(3)).size).toBe(0);
  });

  it('skips an island with non-finite vertices and still projects the finite one', () => {
    const verts = [NaN, 0, 0, 1, 0, 0, 0, 1, 0, 5, 5, 0, 6, 5, 0, 6, 6, 0, 5, 6, 0];
    const out = smartProject([0, 0, 0, 0, 0, 0, 0, 0], verts, [0, 1, 2, 4, 5, 6, 4, 6, 7], [0, 0, 0, 1, 1, 1, 1], all(7));
    expect(out.slice(0, 6)).toEqual([0, 0, 0, 0, 0, 0]);
    out.slice(6).forEach(x => expect(Number.isFinite(x)).toBe(true));
  });

  it('is deterministic and idempotent on its own output (detects hidden state or a projection that reads the old UVs)', () => {
    const first = smartProject(quadUvs, quadVerts, quadTris, quadIds, all(4));
    expect(smartProject(quadUvs, quadVerts, quadTris, quadIds, all(4))).toEqual(first);
    expect(smartProject(first, quadVerts, quadTris, quadIds, all(4))).toEqual(first);
  });

  it('never writes to frozen inputs', () => {
    const uvs = Object.freeze([...quadUvs]);
    const verts = Object.freeze([...quadVerts]);
    const tris = Object.freeze([...quadTris]);
    const ids = Object.freeze([...quadIds]);
    expect(() => smartProject(uvs, verts, tris, ids, all(4))).not.toThrow();
    expect(uvs).toEqual(quadUvs);
  });
});

describe('islandPlanes', () => {
  it('uses the area-weighted normal of two non-coplanar faces, not the first face normal (detects an unweighted normal)', () => {
    const verts = [0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 2, 0, 0, 0, 1];
    const tris = [0, 1, 2, 0, 3, 4];
    const plane = islandPlanes(verts, tris, [0, 0, 0, 0, 0], all(5)).get(0);
    expect(plane).toBeDefined();
    const u = plane!.u, v = plane!.v;
    const n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
    expect(n[1]).toBeCloseTo(0, 6);
    expect(n[0]).toBeCloseTo(0.124, 3);
    expect(n[2]).toBeCloseTo(0.992, 3);
  });
});
