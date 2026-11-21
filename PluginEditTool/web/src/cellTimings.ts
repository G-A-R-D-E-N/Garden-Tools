export interface CellBatchTimings {
  meshes?: number;
  errors?: number;
  wallMs?: number;
  threadSumMs?: number;
  indexMs?: number;
  resolveMs?: number;
  extractMs?: number;
  convertMs?: number;
  slowestMs?: number;
  slowestPath?: string;
  dumpMs?: number;
  jsonBytes?: number;
}

export interface CellTextureTimings {
  textureCalls?: number;
  decodeMs?: number;
  pngWriteMs?: number;
  base64Ms?: number;
}

const seconds = (ms: number | undefined): string => `${((ms ?? 0) / 1000).toFixed(2)}s`;

export function formatCellTimings(
  batch: CellBatchTimings | null | undefined,
  textures?: CellTextureTimings | null,
): string {
  const b = batch ?? {};
  const meshes = `meshes ${seconds(b.wallMs)} (resolve ${seconds(b.resolveMs)}/extract ${seconds(b.extractMs)}/convert ${seconds(b.convertMs)})`;
  const texture = textures
    ? `textures ${textures.textureCalls ?? 0} calls ${seconds((textures.decodeMs ?? 0) + (textures.pngWriteMs ?? 0) + (textures.base64Ms ?? 0))}`
    : 'textures -';
  return `Timing: index ${seconds(b.indexMs)}, ${meshes}, ${texture}`;
}
