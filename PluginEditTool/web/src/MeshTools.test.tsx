import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { createRef } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from './backend';
import MeshTools, { type MeshToolsHandle } from './MeshTools';
import { describeShapeOpReport, MESH_OPS, parseShapeOpReport, replayMeshOps, stagingPathFor } from './meshOpsClient';

const A = 'C:\\mods\\a.nif';
const STAGING = 'C:\\mods\\a.mesh-tools.nif';

type ShapeOpFn = (nifPath: string, outNif: string, op: string, blocks: string, crease: number,
                  overwrite: boolean, ratio: number, maxInfluences?: number, pruneThreshold?: number,
                  sourceBlock?: number | null) => Promise<string>;

const successReport = (op: string, extra = '') =>
  `RESULT: shape op ${op} OK (in-process)\nSaved: ${STAGING}\nChanged blocks: 3\nSkipped: none\n` +
  `Vertices: 8 -> 4, triangles: 2 -> 2\n${extra}Manifest OK`;

const makeHost = (fn: ShapeOpFn) => ({ ShapeOp: vi.fn(fn) } as unknown as NifHost);

const fakeShapeOp = (impl?: (op: string) => Promise<string>) =>
  vi.fn<ShapeOpFn>(async (_nif, _out, op) => (impl ? impl(op) : successReport(op)));

afterEach(cleanup);

describe('meshTools helpers', () => {
  it('parses the counts, the displacement and the refusals', () => {
    const report = parseShapeOpReport(successReport('weld_vertices', 'Max vertex displacement: 0.7071\n'));
    expect(report.ok).toBe(true);
    expect(report.op).toBe('weld_vertices');
    expect(report.verticesBefore).toBe(8);
    expect(report.verticesAfter).toBe(4);
    expect(report.trianglesBefore).toBe(2);
    expect(report.maxError).toBeCloseTo(0.7071);

    const refused = parseShapeOpReport('Error: shape op failed\nsmoothness must be finite');
    expect(refused.ok).toBe(false);
    expect(refused.refusal).toBe('shape op failed (smoothness must be finite)');

    const weighted = parseShapeOpReport(
      successReport('normalize_weights', 'Weights changed on 5 vertices, largest change 0.25\n'));
    expect(weighted.weightsChanged).toBe(5);
    expect(describeShapeOpReport(weighted)).toContain('weights changed on 5 vertices');
  });

  it('names the staging file beside the source and describes an applied op', () => {
    expect(stagingPathFor(A)).toBe(STAGING);
    expect(describeShapeOpReport(parseShapeOpReport(successReport('simplify')))).toContain('vertices 8 -> 4');
  });

  it('replays the chain from the source into the final path', async () => {
    const shapeOp = fakeShapeOp();
    const replayed = await replayMeshOps(makeHost(shapeOp), A, 3, [MESH_OPS.weld, MESH_OPS.unused], A);
    expect(replayed.ok).toBe(true);
    expect(shapeOp).toHaveBeenCalledTimes(2);
    expect(shapeOp.mock.calls[0][0]).toBe(A);
    expect(shapeOp.mock.calls[0][1]).toBe(STAGING);
    expect(shapeOp.mock.calls[1][0]).toBe(STAGING);
    expect(shapeOp.mock.calls[1][1]).toBe(A);
    expect(shapeOp.mock.calls[0][3]).toBe('[3]');
  });
});

describe('MeshTools panel', () => {
  const shapes = [{ id: 3, name: 'Body' }, { id: 7, name: 'Gear' }];

  const renderTools = (shapeOp: ReturnType<typeof fakeShapeOp>) => {
    const handle = createRef<MeshToolsHandle>();
    const appendLog = vi.fn();
    const onDirtyChange = vi.fn();
    const onBusyChange = vi.fn();
    render(
      <MeshTools ref={handle} host={makeHost(shapeOp)} nifPath={A} shapes={shapes}
                 appendLog={appendLog} onDirtyChange={onDirtyChange} onBusyChange={onBusyChange} />,
    );
    fireEvent.change(screen.getByRole('combobox', { name: 'Mesh tools shape' }), { target: { value: '3' } });
    return { handle, appendLog, onDirtyChange, onBusyChange };
  };

  it('runs exactly one op per button with the requested ratio', async () => {
    const shapeOp = fakeShapeOp();
    const { appendLog, onDirtyChange } = renderTools(shapeOp);
    fireEvent.click(screen.getByText('Weld vertices'));
    await waitFor(() => expect(shapeOp).toHaveBeenCalledTimes(1));
    expect(shapeOp.mock.calls[0][2]).toBe('weld_vertices');
    expect(shapeOp.mock.calls[0][6]).toBeCloseTo(0.5);
    await waitFor(() => expect(onDirtyChange).toHaveBeenCalledWith(true));
    expect(appendLog.mock.calls[0][0]).toContain('vertices 8 -> 4');

    fireEvent.change(screen.getByTitle('Simplify: target fraction of the triangles'), { target: { value: '0.25' } });
    fireEvent.click(screen.getByText('Simplify'));
    await waitFor(() => expect(shapeOp).toHaveBeenCalledTimes(2));
    expect(shapeOp.mock.calls[1][2]).toBe('simplify');
    expect(shapeOp.mock.calls[1][6]).toBeCloseTo(0.25);
    expect(shapeOp.mock.calls[1][0]).toBe(STAGING);
  });

  it('logs a refusal verbatim and leaves the file clean', async () => {
    const shapeOp = fakeShapeOp(async () => 'Error: edit on block 3: only BSTriShape is supported');
    const { appendLog, onDirtyChange } = renderTools(shapeOp);
    fireEvent.click(screen.getByText('Remove unused'));
    await waitFor(() => expect(appendLog).toHaveBeenCalledTimes(1));
    expect(appendLog.mock.calls[0][0]).toContain('only BSTriShape is supported');
    expect(onDirtyChange).not.toHaveBeenCalledWith(true);
    expect(screen.getByText('No mesh changes')).toBeTruthy();
  });

  it('marks dirty on success and commits the chain back onto the open file', async () => {
    const shapeOp = fakeShapeOp();
    const { handle, onDirtyChange } = renderTools(shapeOp);
    fireEvent.click(screen.getByText('Weld vertices'));
    await waitFor(() => expect(onDirtyChange).toHaveBeenCalledWith(true));
    expect(handle.current?.hasPending()).toBe(true);
    const committed = await handle.current!.commit();
    expect(committed.ok).toBe(true);
    expect(shapeOp).toHaveBeenCalledTimes(2);
    expect(shapeOp.mock.calls[1][1]).toBe(A);
    expect(handle.current?.hasPending()).toBe(false);
    await waitFor(() => expect(onDirtyChange).toHaveBeenCalledWith(false));
  });

  it('logs a shape with nothing to change without marking the file dirty', async () => {
    const shapeOp = fakeShapeOp(async op =>
      `RESULT: shape op ${op} OK (in-process)\nSaved: ${STAGING}\nChanged blocks: none\nSkipped: none\n` +
      'Vertices: 0 -> 0, triangles: 2 -> 2\nManifest OK');
    const { appendLog, onDirtyChange } = renderTools(shapeOp);
    fireEvent.click(screen.getByText('Remove unused'));
    await waitFor(() => expect(appendLog).toHaveBeenCalledTimes(1));
    expect(appendLog.mock.calls[0][0]).toContain('nothing to change');
    expect(onDirtyChange).not.toHaveBeenCalledWith(true);
  });

  it('names the skip reason when a shape has nothing to change', async () => {
    const shapeOp = fakeShapeOp(async op =>
      `RESULT: shape op ${op} OK (in-process)\nSaved: ${STAGING}\nChanged blocks: none\n` +
      'Skipped: block 3: vertex 0 would end with no positive bone weight\n' +
      'Vertices: 8 -> 8, triangles: 2 -> 2\nManifest OK');
    const { appendLog, onDirtyChange } = renderTools(shapeOp);
    fireEvent.click(screen.getByText('Normalize weights'));
    await waitFor(() => expect(appendLog).toHaveBeenCalledTimes(1));
    expect(appendLog.mock.calls[0][0]).toContain(
      'nothing to change on block 3 (skipped block 3: vertex 0 would end with no positive bone weight)');
    expect(onDirtyChange).not.toHaveBeenCalledWith(true);
  });

  it('sends the skin weight arguments only for the op that reads them', async () => {
    const shapeOp = fakeShapeOp();
    const { onDirtyChange } = renderTools(shapeOp);
    fireEvent.click(screen.getByText('Normalize weights'));
    await waitFor(() => expect(onDirtyChange).toHaveBeenCalledWith(true));
    expect(shapeOp.mock.calls[0][2]).toBe('normalize_weights');
    expect(shapeOp.mock.calls[0][7]).toBe(4);
    expect(shapeOp.mock.calls[0][8]).toBe(0);
    expect(shapeOp.mock.calls[0][9]).toBeNull();

    fireEvent.change(screen.getByTitle('Limit influences: most bones a vertex keeps'), { target: { value: '2' } });
    fireEvent.click(screen.getByText('Limit influences'));
    await waitFor(() => expect(shapeOp).toHaveBeenCalledTimes(2));
    expect(shapeOp.mock.calls[1][2]).toBe('limit_influences');
    expect(shapeOp.mock.calls[1][7]).toBe(2);
    expect(shapeOp.mock.calls[1][1]).toBe(STAGING);
  });

  it('sends the prune threshold and the copy source to the native op', async () => {
    const shapeOp = fakeShapeOp();
    const { onDirtyChange } = renderTools(shapeOp);
    fireEvent.change(screen.getByTitle('Prune weights: weights below this are dropped'), { target: { value: '0.2' } });
    fireEvent.click(screen.getByText('Prune weights'));
    await waitFor(() => expect(onDirtyChange).toHaveBeenCalledWith(true));
    expect(shapeOp.mock.calls[0][2]).toBe('prune_weights');
    expect(shapeOp.mock.calls[0][8]).toBeCloseTo(0.2);

    const source = screen.getByTitle('Copy weights: source shape') as HTMLSelectElement;
    expect(Array.from(source.options).map(o => o.value)).toEqual(['-1', '7']);
    expect(screen.getByText('Copy weights').hasAttribute('disabled')).toBe(true);
    fireEvent.change(source, { target: { value: '7' } });
    fireEvent.click(screen.getByText('Copy weights'));
    await waitFor(() => expect(shapeOp).toHaveBeenCalledTimes(2));
    expect(shapeOp.mock.calls[1][2]).toBe('copy_weights');
    expect(shapeOp.mock.calls[1][3]).toBe('[3]');
    expect(shapeOp.mock.calls[1][9]).toBe(7);
  });

  it('undoes the last op without touching the file', async () => {
    const shapeOp = fakeShapeOp();
    const { handle, onDirtyChange } = renderTools(shapeOp);
    fireEvent.click(screen.getByText('Weld vertices'));
    await waitFor(() => expect(onDirtyChange).toHaveBeenCalledWith(true));
    fireEvent.click(screen.getByText('Undo op'));
    await waitFor(() => expect(handle.current?.hasPending()).toBe(false));
    expect(shapeOp).toHaveBeenCalledTimes(1);
  });
});
