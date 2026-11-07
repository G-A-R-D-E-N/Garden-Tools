import { describe, expect, it } from 'vitest';
import { lscmUnwrap, lscmUnwrapInfo } from './uvUnwrap';

const quadVerts = [0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0];
const quadTris = [0, 1, 2, 0, 2, 3];
const quadIds = [0, 0, 0, 0];
const all = (n: number): Set<number> => new Set(Array.from({ length: n }, (_, i) => i));
const dist = (uvs: readonly number[], a: number, b: number): number =>
  Math.hypot(uvs[2 * a] - uvs[2 * b], uvs[2 * a + 1] - uvs[2 * b + 1]);
const dist3d = (verts: readonly number[], a: number, b: number): number =>
  Math.hypot(verts[3 * a] - verts[3 * b], verts[3 * a + 1] - verts[3 * b + 1], verts[3 * a + 2] - verts[3 * b + 2]);

describe('lscmUnwrap', () => {
  it('maps a planar quad isometrically with the pins exact and the same orientation as the mesh (detects a mirrored or unpinned solve)', () => {
    const pins = new Map<number, [number, number]>([[0, [0, 0]], [1, [2, 0]]]);
    const out = lscmUnwrap([0, 0, 0, 0, 0, 0, 0, 0], quadVerts, quadTris, quadIds, all(4), pins);
    expect(out[0]).toBeCloseTo(0, 6);
    expect(out[1]).toBeCloseTo(0, 6);
    expect(out[2]).toBeCloseTo(2, 6);
    expect(out[3]).toBeCloseTo(0, 6);
    expect(out[4]).toBeCloseTo(2, 4);
    expect(out[5]).toBeCloseTo(2, 4);
    expect(out[6]).toBeCloseTo(0, 4);
    expect(out[7]).toBeCloseTo(2, 4);
    const cross = (out[2] - out[0]) * (out[5] - out[1]) - (out[3] - out[1]) * (out[4] - out[0]);
    expect(cross).toBeGreaterThan(0);
  });

  it('leaves an island with fewer than two pins untouched (detects a solve without constraints)', () => {
    const uvs = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8];
    const out = lscmUnwrap(uvs, quadVerts, quadTris, quadIds, all(4), new Map([[0, [0, 0] as [number, number]]]));
    expect(out).toEqual(uvs);
  });

  it('gives the same result from two different starting UVs (detects a solver that leaks the old UVs)', () => {
    const pins = new Map<number, [number, number]>([[0, [0, 0]], [3, [0, 2]]]);
    const a = lscmUnwrap([0, 0, 0, 0, 0, 0, 0, 0], quadVerts, quadTris, quadIds, all(4), pins);
    const b = lscmUnwrap([0.9, 0.9, 0.2, 0.1, 0.1, 0.8, 0.1, 0.1], quadVerts, quadTris, quadIds, all(4), pins);
    a.forEach((x, i) => expect(b[i]).toBeCloseTo(x, 6));
    expect(b[0]).toBeCloseTo(0, 6);
    expect(b[1]).toBeCloseTo(0, 6);
    expect(b[6]).toBeCloseTo(0, 6);
    expect(b[7]).toBeCloseTo(2, 6);
  });

  it('unrolls a bent-but-developable two-triangle strip with every edge length kept (detects an energy that only smooths)', () => {
    const verts = [0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2];
    const tris = [0, 1, 2, 0, 1, 3];
    const pins = new Map<number, [number, number]>([[0, [0, 0]], [1, [2, 0]]]);
    const out = lscmUnwrap([0, 0, 0, 0, 0, 0, 0, 0], verts, tris, [0, 0, 0, 0], all(4), pins);
    for (const [a, b] of [[0, 1], [1, 2], [0, 2], [1, 3], [0, 3]]) {
      expect(dist(out, a, b)).toBeCloseTo(dist3d(verts, a, b), 3);
    }
  });

  it('unwraps only the island that has pins (detects a solve that mixes islands or ignores the pin split)', () => {
    const verts = [...quadVerts, 5, 5, 0, 6, 5, 0, 6, 6, 0, 5, 6, 0];
    const tris = [...quadTris, 4, 5, 6, 4, 6, 7];
    const ids = [0, 0, 0, 0, 1, 1, 1, 1];
    const pins = new Map<number, [number, number]>([[0, [0, 0]], [1, [2, 0]]]);
    const tail = [0.4, 0.4, 0.5, 0.4, 0.5, 0.5, 0.4, 0.5];
    const info = lscmUnwrapInfo([0, 0, 0, 0, 0, 0, 0, 0, ...tail], verts, tris, ids, all(8), pins);
    expect(info.unwrapped).toBe(1);
    expect(info.skipped).toBe(1);
    expect(info.uvs.slice(8)).toEqual(tail);
  });

  it('refuses pins that sit at the same UV instead of collapsing the island (detects a zero right-hand side saved as a layout)', () => {
    const uvs = [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5];
    const pins = new Map<number, [number, number]>([[0, [0.5, 0.5]], [2, [0.5, 0.5]]]);
    const info = lscmUnwrapInfo(uvs, quadVerts, quadTris, quadIds, all(4), pins);
    expect(info.unwrapped).toBe(0);
    expect(info.skipped).toBe(1);
    expect(info.uvs).toEqual(uvs);
  });

  it('is deterministic and never writes to frozen inputs', () => {
    const uvs = Object.freeze([0, 0, 0, 0, 0, 0, 0, 0]);
    const verts = Object.freeze([...quadVerts]);
    const tris = Object.freeze([...quadTris]);
    const ids = Object.freeze([...quadIds]);
    const pins = new Map<number, [number, number]>([[0, [0, 0]], [2, [1, 1]]]);
    const first = lscmUnwrap(uvs, verts, tris, ids, all(4), pins);
    expect(lscmUnwrap(uvs, verts, tris, ids, all(4), pins)).toEqual(first);
    expect(uvs).toEqual([0, 0, 0, 0, 0, 0, 0, 0]);
  });

  it('accepts a well-formed pinned island as converged instead of silently skipping it (detects a gate that rejects every solve)', () => {
    const pins = new Map<number, [number, number]>([[0, [0, 0]], [1, [2, 0]]]);
    const info = lscmUnwrapInfo([0, 0, 0, 0, 0, 0, 0, 0], quadVerts, quadTris, quadIds, all(4), pins);
    expect(info.skipped).toBe(0);
    expect(info.unwrapped).toBe(1);
    expect(info.uvs[2]).toBeCloseTo(2, 5);
    expect(info.uvs[3]).toBeCloseTo(0, 5);
  });
});
