import type { NifHost } from '../backend';
import { edgeKey } from './uvMath';
import type { Align } from './uvOps';

type Reply = { error?: string };

async function uvCall<T>(nif: NifHost, op: string, args: object): Promise<T> {
  const text = await nif.UvOp(op, JSON.stringify(args));
  let reply: Reply;
  try { reply = JSON.parse(text) as Reply; } catch { throw new Error(text); }
  if (typeof reply.error === 'string') throw new Error(reply.error);
  return reply as T;
}

const sel = (s: ReadonlySet<number>): number[] => Array.from(s);

const keep = (prev: number[], next: number[]): number[] => {
  let same = next.length === prev.length;
  const out = next.map((x, i) => {
    if (Math.fround(prev[i]) === x) return prev[i];
    same = false;
    return x;
  });
  return same ? prev : out;
};

export type UvInfo = { uvs: number[]; skipped: number };

export const uvIslands = async (nif: NifHost, vertexCount: number, tris: readonly number[]): Promise<number[]> =>
  (await uvCall<{ ids: number[] }>(nif, 'islands', { vertex_count: vertexCount, tris })).ids;

export async function uvPack(nif: NifHost, uvs: number[], tris: readonly number[], pick: ReadonlySet<number>, margin: number): Promise<number[] | null> {
  const r = await uvCall<{ fits: boolean; uvs: number[] }>(nif, 'pack', { uvs, tris, selection: sel(pick), margin });
  return r.fits ? keep(uvs, r.uvs) : null;
}

export async function uvAverage(nif: NifHost, uvs: number[], verts: readonly number[], tris: readonly number[], pick: ReadonlySet<number>): Promise<number[]> {
  const r = await uvCall<{ uvs: number[] }>(nif, 'average_scale', { uvs, positions: verts, tris, selection: sel(pick) });
  return keep(uvs, r.uvs);
}

export const uvStretch = async (nif: NifHost, uvs: readonly number[], verts: readonly number[], tris: readonly number[]): Promise<number[]> =>
  (await uvCall<{ values: number[] }>(nif, 'tri_stretch', { uvs, positions: verts, tris })).values;

export async function uvSmartProject(nif: NifHost, uvs: number[], verts: readonly number[], tris: readonly number[], pick: ReadonlySet<number>, margin: number):
  Promise<UvInfo & { fits: boolean; projected: number }> {
  const r = await uvCall<{ fits: boolean; projected: number; skipped: number; uvs: number[] }>(nif, 'smart_project',
    { uvs, positions: verts, tris, selection: sel(pick), margin });
  return { fits: r.fits, projected: r.projected, skipped: r.skipped, uvs: keep(uvs, r.uvs) };
}

export async function uvUnwrap(nif: NifHost, uvs: number[], verts: readonly number[], tris: readonly number[], pick: ReadonlySet<number>,
  pinned: ReadonlyMap<number, [number, number]>): Promise<UvInfo & { unwrapped: number }> {
  const r = await uvCall<{ unwrapped: number; skipped: number; uvs: number[] }>(nif, 'lscm_unwrap', {
    uvs, positions: verts, tris, selection: sel(pick),
    pin_vertices: Array.from(pinned.keys()), pin_uvs: Array.from(pinned.values()).flat(),
  });
  return { unwrapped: r.unwrapped, skipped: r.skipped, uvs: keep(uvs, r.uvs) };
}

async function uvsOp(nif: NifHost, op: string, uvs: number[], args: object): Promise<number[]> {
  const r = await uvCall<{ uvs: number[] }>(nif, op, { uvs, ...args });
  return keep(uvs, r.uvs);
}

export const uvRelax = (nif: NifHost, uvs: number[], tris: readonly number[], pick: ReadonlySet<number>, iterations: number) =>
  uvsOp(nif, 'relax', uvs, { tris, selection: sel(pick), iterations });

export const uvStitch = (nif: NifHost, uvs: number[], verts: readonly number[], tris: readonly number[], pick: ReadonlySet<number>, eps: number) =>
  uvsOp(nif, 'stitch', uvs, { positions: verts, tris, selection: sel(pick), eps });

export const uvMergeCentre = (nif: NifHost, uvs: number[], pick: ReadonlySet<number>) =>
  uvsOp(nif, 'merge_at_centre', uvs, { selection: sel(pick) });

export const uvWeld = (nif: NifHost, uvs: number[], pick: ReadonlySet<number>, distance: number) =>
  uvsOp(nif, 'merge_by_distance', uvs, { selection: sel(pick), distance });

const ALIGN_TO: Record<Align, number> = { min: 0, centre: 1, max: 2 };
export const uvAlign = (nif: NifHost, uvs: number[], pick: ReadonlySet<number>, axis: 'u' | 'v', to: Align) =>
  uvsOp(nif, 'align', uvs, { selection: sel(pick), axis: axis === 'u' ? 0 : 1, to: ALIGN_TO[to] });

export const uvSnapGrid = (nif: NifHost, uvs: number[], pick: ReadonlySet<number>, step: number) =>
  uvsOp(nif, 'snap_grid', uvs, { selection: sel(pick), step });

export const uvSnapPixels = (nif: NifHost, uvs: number[], pick: ReadonlySet<number>, width: number, height: number, centre: boolean) =>
  uvsOp(nif, 'snap_pixels', uvs, { selection: sel(pick), width, height, centre });

export const uvClamp = (nif: NifHost, uvs: number[], pick: ReadonlySet<number>) =>
  uvsOp(nif, 'clamp_tile', uvs, { selection: sel(pick) });

export async function uvOverlap(nif: NifHost, uvs: readonly number[], tris: readonly number[], hidden: ReadonlySet<number>): Promise<number[]> {
  return (await uvCall<{ faces: number[] }>(nif, 'overlapping_faces', { uvs, tris, hidden: sel(hidden) })).faces;
}

export async function uvSeams(nif: NifHost, tris: readonly number[], pick: ReadonlySet<number>, hidden: ReadonlySet<number>): Promise<Set<number>> {
  const { pairs } = await uvCall<{ pairs: number[] }>(nif, 'seams_in_selection', { tris, selection: sel(pick), hidden: sel(hidden) });
  const keys = new Set<number>();
  for (let i = 0; i + 1 < pairs.length; i += 2) keys.add(edgeKey(pairs[i], pairs[i + 1]));
  return keys;
}

export async function uvSplitPlan(nif: NifHost, vertexCount: number, tris: readonly number[], seams: ReadonlySet<number>): Promise<{ sources: number[]; tris: number[] }> {
  const flat = Array.from(seams).flatMap(k => [Math.floor(k / 65536), k % 65536]);
  return uvCall<{ sources: number[]; tris: number[] }>(nif, 'split_plan', { vertex_count: vertexCount, tris, seams: flat });
}
