import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { createRef } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from './backend';
import MeshTools, { type MeshToolsHandle } from './MeshTools';
import {
  createMeshToolsStagingPath, describeShapeOpReport, MESH_OPS, parseEdgePairs, parseIndexList, parseShapeOpReport,
  parseVector3, replayMeshOps,
} from './meshOpsClient';

const A = 'C:\\mods\\a.nif';
const STAGING = 'C:\\mods\\a.mesh-tools-00000000-0000-4000-8000-000000000001.nif';

vi.stubGlobal('crypto', { randomUUID: () => '00000000-0000-4000-8000-000000000001' });

type ShapeOpFn = (nifPath: string, outNif: string, op: string, blocks: string, crease: number,
                  overwrite: boolean, ratio: number, maxInfluences?: number, pruneThreshold?: number,
                  sourceBlock?: number | null, tris?: number[] | null, distance?: number | null,
                  loopA?: number[] | null, loopB?: number[] | null, normal?: number[] | null,
                  offset?: number | null, edges?: [number, number][] | null,
                  width?: number | null) => Promise<string>;

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

  it('names a unique staging file beside the source and describes an applied op', () => {
    expect(createMeshToolsStagingPath(A)).toBe(STAGING);
    expect(describeShapeOpReport(parseShapeOpReport(successReport('simplify')))).toContain('vertices 8 -> 4');
  });

  // Invariant: fails when a parser accepts text the native side would read differently (a negative, a decimal, a lone number).
  it('reads index lists, vertex pairs and a vector only when the text is exactly that', () => {
    expect(parseIndexList('0, 2 5')).toEqual([0, 2, 5]);
    expect(parseIndexList('')).toBeNull();
    expect(parseIndexList('1, -2')).toBeNull();
    expect(parseIndexList('1.5')).toBeNull();
    expect(parseIndexList('1, x')).toBeNull();
    expect(parseEdgePairs('0-1, 4 - 5')).toEqual([[0, 1], [4, 5]]);
    expect(parseEdgePairs('0-1-2')).toBeNull();
    expect(parseEdgePairs('3')).toBeNull();
    expect(parseEdgePairs('-1-2')).toBeNull();
    expect(parseVector3('0, -1.5, 2')).toEqual([0, -1.5, 2]);
    expect(parseVector3('1, 2')).toBeNull();
    expect(parseVector3('1, 2, nope')).toBeNull();
    expect(parseVector3('1, 2, Infinity')).toBeNull();
  });

  it('replays the chain from the source into the final path', async () => {
    const shapeOp = fakeShapeOp();
    const replayed = await replayMeshOps(makeHost(shapeOp), A, 3, [MESH_OPS.weld, MESH_OPS.unused], A, STAGING);
    expect(replayed.ok).toBe(true);
    expect(shapeOp).toHaveBeenCalledTimes(2);
    expect(shapeOp.mock.calls[0][0]).toBe(A);
    expect(shapeOp.mock.calls[0][1]).toBe(STAGING);
    expect(shapeOp.mock.calls[0][5]).toBe(true);
    expect(shapeOp.mock.calls[1][0]).toBe(STAGING);
    expect(shapeOp.mock.calls[1][1]).toBe(A);
    expect(shapeOp.mock.calls[1][5]).toBe(true);
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
    expect(shapeOp.mock.calls[1][5]).toBe(true);
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

  it('overwrites the owned staging file after a no-op, so the next op is not refused as existing', async () => {
    const shapeOp = fakeShapeOp(async op =>
      op === 'remove_unused_vertices'
        ? `RESULT: shape op ${op} OK (in-process)\nSaved: ${STAGING}\nChanged blocks: none\nSkipped: none\n` +
          'Vertices: 8 -> 8, triangles: 2 -> 2\nManifest OK'
        : successReport(op));
    const { appendLog, onDirtyChange } = renderTools(shapeOp);
    fireEvent.click(screen.getByText('Remove unused'));
    await waitFor(() => expect(appendLog.mock.calls.some(c => String(c[0]).includes('nothing to change'))).toBe(true));
    await waitFor(() => expect(screen.getByText('Weld vertices').hasAttribute('disabled')).toBe(false));
    fireEvent.click(screen.getByText('Weld vertices'));
    await waitFor(() => expect(onDirtyChange).toHaveBeenCalledWith(true));
    expect(shapeOp).toHaveBeenCalledTimes(2);
    expect(shapeOp.mock.calls[1][0]).toBe(A);
    expect(shapeOp.mock.calls[1][1]).toBe(STAGING);
    expect(shapeOp.mock.calls[1][5]).toBe(true);
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

  // Invariant: fails when a model op loses an argument, sends text instead of numbers, or an invalid input reaches the host.
  it('sends the N3 model arguments at the native positions and keeps them through a commit replay', async () => {
    const shapeOp = fakeShapeOp();
    const { handle } = renderTools(shapeOp);
    const type = (title: string, value: string) => fireEvent.change(screen.getByTitle(title), { target: { value } });
    const button = (label: string) => screen.getByText(label) as HTMLButtonElement;
    const settled = (calls: number) => waitFor(() => {
      expect(shapeOp).toHaveBeenCalledTimes(calls);
      expect(button('Undo op').disabled).toBe(false);
    });

    expect(button('Extrude faces').disabled).toBe(true);
    type('Extrude faces: triangle indices', '0, 2,5');
    type('Extrude faces: distance along the vertex normals', '0.5');
    fireEvent.click(button('Extrude faces'));
    await settled(1);
    expect(shapeOp.mock.calls[0].slice(2, 4)).toEqual(['extrude_faces', '[3]']);
    expect(shapeOp.mock.calls[0][10]).toEqual([0, 2, 5]);
    expect(shapeOp.mock.calls[0][11]).toBeCloseTo(0.5);

    type('Bridge loops: first vertex loop', '0 1 2');
    type('Bridge loops: second vertex loop, same length', '4, 5');
    expect(button('Bridge loops').disabled).toBe(true);
    type('Bridge loops: second vertex loop, same length', '4, 5, 6');
    fireEvent.click(button('Bridge loops'));
    await settled(2);
    expect(shapeOp.mock.calls[1][2]).toBe('bridge_loops');
    expect(shapeOp.mock.calls[1][12]).toEqual([0, 1, 2]);
    expect(shapeOp.mock.calls[1][13]).toEqual([4, 5, 6]);

    type('Knife plane: normal x, y, z', '0, 0, 0');
    expect(button('Knife plane').disabled).toBe(true);
    type('Knife plane: normal x, y, z', '1, 0, 0');
    type('Knife plane: offset along the normal', '-0.25');
    fireEvent.click(button('Knife plane'));
    await settled(3);
    expect(shapeOp.mock.calls[2][2]).toBe('knife_plane');
    expect(shapeOp.mock.calls[2][14]).toEqual([1, 0, 0]);
    expect(shapeOp.mock.calls[2][15]).toBeCloseTo(-0.25);

    type('Cut loop and Bevel edges: vertex pairs', '0-1, 1 - 2; 2-0');
    fireEvent.click(button('Cut loop'));
    await settled(4);
    expect(shapeOp.mock.calls[3][2]).toBe('cut_loop');
    expect(shapeOp.mock.calls[3][16]).toEqual([[0, 1], [1, 2], [2, 0]]);

    type('Bevel edges: chamfer width, above 0 and below 0.5', '0.5');
    expect(button('Bevel edges').disabled).toBe(true);
    type('Bevel edges: chamfer width, above 0 and below 0.5', '0.2');
    fireEvent.click(button('Bevel edges'));
    await settled(5);
    expect(shapeOp.mock.calls[4][2]).toBe('bevel_edges');
    expect(shapeOp.mock.calls[4][17]).toBeCloseTo(0.2);
    expect(shapeOp.mock.calls[4][0]).toBe(STAGING);

    expect((await handle.current!.commit()).ok).toBe(true);
    expect(shapeOp).toHaveBeenCalledTimes(10);
    expect(shapeOp.mock.calls[5][0]).toBe(A);
    expect(shapeOp.mock.calls[5][10]).toEqual([0, 2, 5]);
    expect(shapeOp.mock.calls[9][1]).toBe(A);
    expect(shapeOp.mock.calls[9][16]).toEqual([[0, 1], [1, 2], [2, 0]]);
  });

  // Invariant: fails when an engine refusal of a model op is swallowed or the file is marked dirty after it.
  it('logs the engine refusal of a knife plane that misses the shape and stays clean', async () => {
    const shapeOp = fakeShapeOp(async () => 'Error: knife_plane: the plane crosses no triangle');
    const { appendLog, onDirtyChange } = renderTools(shapeOp);
    fireEvent.change(screen.getByTitle('Knife plane: offset along the normal'), { target: { value: '99' } });
    fireEvent.click(screen.getByText('Knife plane'));
    await waitFor(() => expect(appendLog).toHaveBeenCalledTimes(1));
    expect(appendLog.mock.calls[0][0]).toContain('the plane crosses no triangle');
    expect(onDirtyChange).not.toHaveBeenCalledWith(true);
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
