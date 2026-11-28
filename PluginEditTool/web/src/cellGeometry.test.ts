import { describe, expect, it, vi } from 'vitest';
import type { CellHost, CellPlacedReference } from './backend';
import type { CellGeoMap } from './CellViewport';
import { fetchMissingGeometry, missingModelPaths, referenceModelPaths } from './cellGeometry';

function ref(formKey: string, modelPath: string | null, scol: string[] = []): CellPlacedReference {
  return {
    formKey,
    recordType: 'REFR',
    baseFormKey: 'BASE',
    modelPath,
    scolParts: scol.length > 0 ? scol.map(p => ({ modelPath: p, placements: [] })) : null,
    position: { x: 0, y: 0, z: 0 },
    rotation: { x: 0, y: 0, z: 0 },
    scale: 1,
  } as CellPlacedReference;
}

function cellWith(reply: string) {
  return { GetGeometryBatch: vi.fn(async () => reply) } as unknown as Pick<CellHost, 'GetGeometryBatch'>;
}

describe('cell geometry refresh', () => {
  it('lists each model and static part path once', () => {
    const refs = [ref('A', 'a.nif'), ref('B', 'a.nif', ['b.nif', 'c.nif']), ref('C', null)];
    expect(referenceModelPaths(refs)).toEqual(['a.nif', 'b.nif', 'c.nif']);
  });

  it('keeps only the model paths the geometry map does not hold yet', () => {
    const geometry: CellGeoMap = { 'a.nif': { error: 'unavailable' } };
    expect(missingModelPaths([ref('A', 'a.nif'), ref('B', 'd.nif')], geometry)).toEqual(['d.nif']);
  });

  it('requests only the missing paths and merges the reply', async () => {
    const cell = cellWith(JSON.stringify({ geometry: { 'd.nif': { error: 'nope' } } }));
    const log = vi.fn<(line: string) => void>();
    const geometry: CellGeoMap = { 'a.nif': { error: 'unavailable' } };
    const result = await fetchMissingGeometry(cell, [ref('A', 'a.nif'), ref('B', 'd.nif')], geometry, () => true, log);
    expect(cell.GetGeometryBatch).toHaveBeenCalledTimes(1);
    expect(cell.GetGeometryBatch).toHaveBeenCalledWith(JSON.stringify(['d.nif']));
    expect(result).toEqual({ 'd.nif': { error: 'nope' } });
    expect(log).toHaveBeenCalledWith('• geometry: 0/1 new mesh(es) resolved, 1 failed');
  });

  it('does not call the host when every model is already known', async () => {
    const cell = cellWith('{}');
    const geometry: CellGeoMap = { 'a.nif': { error: 'unavailable' } };
    const result = await fetchMissingGeometry(cell, [ref('A', 'a.nif')], geometry, () => true, vi.fn());
    expect(result).toBeNull();
    expect(cell.GetGeometryBatch).not.toHaveBeenCalled();
  });

  it('marks a path the reply omits as failed so it is not requested forever', async () => {
    const cell = cellWith('{"geometry":{}}');
    const result = await fetchMissingGeometry(cell, [ref('B', 'd.nif')], {}, () => true, vi.fn());
    expect(result).toEqual({ 'd.nif': { error: 'no geometry returned' } });
  });

  it('logs a batch error reply and merges nothing', async () => {
    const cell = cellWith('{"error":"archive missing"}');
    const log = vi.fn<(line: string) => void>();
    const result = await fetchMissingGeometry(cell, [ref('B', 'd.nif')], {}, () => true, log);
    expect(result).toBeNull();
    expect(log).toHaveBeenCalledWith('✗ geometry batch: archive missing');
  });

  it('drops a result from a superseded load', async () => {
    const cell = cellWith('{"geometry":{"d.nif":{"error":"nope"}}}');
    const result = await fetchMissingGeometry(cell, [ref('B', 'd.nif')], {}, () => false, vi.fn());
    expect(result).toBeNull();
  });
});
