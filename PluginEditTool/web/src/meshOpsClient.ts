import type { NifHost } from './backend';

export interface MeshOp {
  label: string;
  op: string;
  ratio?: number;
  maxInfluences?: number;
  pruneThreshold?: number;
  sourceBlock?: number;
  tris?: number[];
  distance?: number;
  loopA?: number[];
  loopB?: number[];
  normal?: [number, number, number];
  offset?: number;
  edges?: [number, number][];
  width?: number;
}

export const MESH_OPS = {
  weld: { label: 'Weld vertices', op: 'weld_vertices' },
  degenerate: { label: 'Remove degenerate triangles', op: 'remove_degenerate_triangles' },
  unused: { label: 'Remove unused vertices', op: 'remove_unused_vertices' },
  simplify: { label: 'Simplify', op: 'simplify' },
  sanitize: { label: 'Sanitize normals/tangents', op: 'sanitize' },
  normalize: { label: 'Normalize weights', op: 'normalize_weights' },
  limit: { label: 'Limit influences', op: 'limit_influences' },
  prune: { label: 'Prune weights', op: 'prune_weights' },
  copy: { label: 'Copy weights', op: 'copy_weights' },
  extrude: { label: 'Extrude faces', op: 'extrude_faces' },
  bridge: { label: 'Bridge loops', op: 'bridge_loops' },
  knife: { label: 'Knife plane', op: 'knife_plane' },
  cutLoop: { label: 'Cut loop', op: 'cut_loop' },
  bevel: { label: 'Bevel edges', op: 'bevel_edges' },
} as const;

const INDEX = /^\d+$/;

// Invariant: a parser returns null for anything but exactly the documented text, so a button never sends a half-read list.
export function parseIndexList(text: string): number[] | null {
  const parts = text.split(/[\s,]+/).filter(part => part.length > 0);
  if (parts.length === 0 || !parts.every(part => INDEX.test(part))) return null;
  const list = parts.map(Number);
  return list.every(Number.isSafeInteger) ? list : null;
}

export function parseEdgePairs(text: string): [number, number][] | null {
  const parts = text.split(/[,;]+/).map(part => part.trim()).filter(part => part.length > 0);
  if (parts.length === 0) return null;
  const pairs: [number, number][] = [];
  for (const part of parts) {
    const ends = part.split('-').map(end => end.trim());
    if (ends.length !== 2 || !ends.every(end => INDEX.test(end))) return null;
    const pair = ends.map(Number);
    if (!pair.every(Number.isSafeInteger)) return null;
    pairs.push([pair[0], pair[1]]);
  }
  return pairs;
}

export function parseVector3(text: string): [number, number, number] | null {
  const parts = text.split(/[\s,]+/).filter(part => part.length > 0);
  if (parts.length !== 3) return null;
  const numbers = parts.map(Number);
  return numbers.every(Number.isFinite) ? [numbers[0], numbers[1], numbers[2]] : null;
}

export interface ShapeOpReport {
  ok: boolean;
  op: string;
  saved: string;
  changed: string;
  skipped: string;
  verticesBefore: number;
  verticesAfter: number;
  trianglesBefore: number;
  trianglesAfter: number;
  maxError: number | null;
  weightsChanged: number;
  refusal: string;
}

const firstErrorLine = (text: string): string => {
  const lines = text.split('\n').map(l => l.trim()).filter(l => l.length > 0);
  return (lines[0] ?? '').replace(/^Error:\s*/i, '') + (lines.length > 1 ? ` (${lines.slice(1).join('; ')})` : '');
};

export function parseShapeOpReport(text: string): ShapeOpReport {
  const ok = /RESULT: shape op \w+ OK/.test(text);
  const name = text.match(/RESULT: shape op (\w+) (?:OK|failed)/);
  const saved = text.match(/Saved: ([^\n]*)/);
  const changed = text.match(/Changed blocks: ([^\n]*)/);
  const skipped = text.match(/Skipped: ([^\n]*)/);
  const counts = text.match(/Vertices: (\d+) -> (\d+), triangles: (\d+) -> (\d+)/);
  const displacement = text.match(/Max vertex displacement: ([0-9.eE+-]+)/);
  const weights = text.match(/Weights changed on (\d+) vertices/);
  return {
    ok,
    op: name ? name[1] : '',
    saved: saved ? saved[1].trim() : '',
    changed: changed ? changed[1].trim() : '',
    skipped: skipped ? skipped[1].trim() : '',
    verticesBefore: counts ? Number(counts[1]) : 0,
    verticesAfter: counts ? Number(counts[2]) : 0,
    trianglesBefore: counts ? Number(counts[3]) : 0,
    trianglesAfter: counts ? Number(counts[4]) : 0,
    maxError: displacement ? Number(displacement[1]) : null,
    weightsChanged: weights ? Number(weights[1]) : 0,
    refusal: ok ? '' : firstErrorLine(text),
  };
}

export function describeShapeOpReport(report: ShapeOpReport): string {
  if (!report.ok) return `refused -- ${report.refusal}`;
  const displacement = report.maxError === null ? '' : `, max displacement ${report.maxError.toFixed(4)}`;
  const weights = report.weightsChanged > 0 ? `, weights changed on ${report.weightsChanged} vertices` : '';
  return `${report.op}: vertices ${report.verticesBefore} -> ${report.verticesAfter}, ` +
    `triangles ${report.trianglesBefore} -> ${report.trianglesAfter}${displacement}${weights}` +
    (report.skipped && report.skipped !== 'none' ? ` (skipped ${report.skipped})` : '');
}

/** Unique scratch path so mesh tools never clobber an existing `.mesh-tools.nif` the user owns. */
export function createMeshToolsStagingPath(nifPath: string): string {
  const base = nifPath.replace(/\.nif$/i, '');
  return `${base}.mesh-tools-${crypto.randomUUID()}.nif`;
}

// Requires a selected shape: every mesh op runs on that one block, so a refusal names it.
export async function runMeshOp(host: NifHost, inputPath: string, outPath: string, block: number,
                                op: MeshOp, overwrite: boolean): Promise<ShapeOpReport> {
  const text = await host.ShapeOp(inputPath, outPath, op.op, JSON.stringify([block]), 180, overwrite, op.ratio ?? 0.5,
    op.maxInfluences ?? 4, op.pruneThreshold ?? 0, op.sourceBlock ?? null, op.tris ?? null, op.distance ?? null,
    op.loopA ?? null, op.loopB ?? null, op.normal ?? null, op.offset ?? null, op.edges ?? null, op.width ?? null);
  return parseShapeOpReport(text);
}

// Invariant: the chain is replayed from the original file, so the last step is the only commit target.
export async function replayMeshOps(host: NifHost, nifPath: string, blocks: number, ops: MeshOp[],
                                    finalPath: string, workStaging: string,
                                    commitOverwrite = true): Promise<{ ok: boolean; text: string; report: ShapeOpReport | null }> {
  let input = nifPath;
  let text = '';
  let report: ShapeOpReport | null = null;
  for (let i = 0; i < ops.length; i += 1) {
    const isLast = i === ops.length - 1;
    const outPath = isLast ? finalPath : workStaging;
    const overwrite = isLast ? commitOverwrite : true;
    report = await runMeshOp(host, input, outPath, blocks, ops[i], overwrite);
    if (!report.ok) return { ok: false, text: report.refusal, report };
    input = outPath;
    text = report.saved;
  }
  return { ok: true, text, report };
}
