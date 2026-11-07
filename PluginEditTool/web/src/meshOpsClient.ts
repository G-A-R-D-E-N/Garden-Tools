import type { NifHost } from './backend';

export interface MeshOp {
  label: string;
  op: string;
  ratio?: number;
  maxInfluences?: number;
  pruneThreshold?: number;
  sourceBlock?: number;
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
} as const;

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

// Invariant: the staging file keeps the source file untouched until Save commits the chain.
export function stagingPathFor(nifPath: string): string {
  return nifPath.replace(/\.nif$/i, '') + '.mesh-tools.nif';
}

// Requires a selected shape: every mesh op runs on that one block, so a refusal names it.
export async function runMeshOp(host: NifHost, inputPath: string, outPath: string, block: number,
                                op: MeshOp): Promise<ShapeOpReport> {
  const text = await host.ShapeOp(inputPath, outPath, op.op, JSON.stringify([block]), 180, true, op.ratio ?? 0.5,
    op.maxInfluences ?? 4, op.pruneThreshold ?? 0, op.sourceBlock ?? null);
  return parseShapeOpReport(text);
}

// Invariant: the chain is replayed from the original file, so the last step is the only commit target.
export async function replayMeshOps(host: NifHost, nifPath: string, blocks: number, ops: MeshOp[],
                                    finalPath: string): Promise<{ ok: boolean; text: string; report: ShapeOpReport | null }> {
  let input = nifPath;
  let text = '';
  let report: ShapeOpReport | null = null;
  for (let i = 0; i < ops.length; i += 1) {
    const outPath = i === ops.length - 1 ? finalPath : stagingPathFor(nifPath);
    report = await runMeshOp(host, input, outPath, blocks, ops[i]);
    if (!report.ok) return { ok: false, text: report.refusal, report };
    input = outPath;
    text = report.saved;
  }
  return { ok: true, text, report };
}
