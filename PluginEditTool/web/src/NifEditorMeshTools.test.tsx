import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from './backend';
import NifEditor, { type NifTree } from './NifEditor';

type ShapeOpFn = (nifPath: string, outNif: string, op: string, blocks: string, crease: number,
                  overwrite: boolean, ratio: number) => Promise<string>;

const A = 'C:\\mods\\a.nif';
const STAGING = 'C:\\mods\\a.mesh-tools.nif';

const report = (op: string, saved: string) =>
  `RESULT: shape op ${op} OK (in-process)\nSaved: ${saved}\nChanged blocks: 3\nSkipped: none\n` +
  'Vertices: 8 -> 4, triangles: 2 -> 2\nManifest OK';

const tree: NifTree = {
  fo4: true,
  file: 'a.nif',
  blocks: [
    { id: 0, type: 'NiNode', name: 'root', group: 'Nodes', cat: '', fields: [] },
    {
      id: 3, type: 'BSTriShape', name: 'Body', group: 'Shapes', cat: '',
      fields: [{ key: 'name', label: 'Name', type: 'string', value: 'Body' }],
    },
  ],
};

afterEach(cleanup);

// Invariant: every ShapeOp call stays unresolved until the test resolves it, so "busy" is observable.
function deferredShapeOp() {
  const resolvers: ((text: string) => void)[] = [];
  const shapeOp = vi.fn<ShapeOpFn>(() => new Promise<string>(resolve => { resolvers.push(resolve); }));
  return { shapeOp, resolvers };
}

function renderEditor(shapeOp: ShapeOpFn) {
  const applyEdits = vi.fn(async () => 'set OK');
  const onSaved = vi.fn();
  const appendLog = vi.fn();
  const host = { ShapeOp: shapeOp, ApplyEdits: applyEdits } as unknown as NifHost;
  render(<NifEditor tree={tree} nif={host} nifPath={A} onSaved={onSaved}
                    onDirtyChange={vi.fn()} appendLog={appendLog} />);
  fireEvent.change(screen.getByRole('combobox', { name: 'Mesh tools shape' }), { target: { value: '3' } });
  return { applyEdits, onSaved, appendLog };
}

const byName = (name: string) => screen.getByRole('button', { name }) as HTMLButtonElement;
const saveButton = () => byName('Save');

describe('NifEditor mesh op routing', () => {
  it('disables Save, Save As and Revert while a mesh op is in flight', async () => {
    const { shapeOp, resolvers } = deferredShapeOp();
    renderEditor(shapeOp);

    fireEvent.click(screen.getByText('Weld vertices'));
    await waitFor(() => expect(shapeOp).toHaveBeenCalledTimes(1));
    resolvers[0](report('weld_vertices', STAGING));
    await waitFor(() => expect(saveButton().disabled).toBe(false));

    fireEvent.click(screen.getByText('Remove unused'));
    await waitFor(() => expect(shapeOp).toHaveBeenCalledTimes(2));
    expect(saveButton().disabled).toBe(true);
    expect(byName('Save As…').disabled).toBe(true);
    expect(byName('Revert').disabled).toBe(true);

    fireEvent.click(saveButton());
    expect(shapeOp).toHaveBeenCalledTimes(2);
    resolvers[1](report('remove_unused_vertices', STAGING));
    await waitFor(() => expect(saveButton().disabled).toBe(false));
  });

  it('commits the pending chain onto the open file from Save, not through ApplyEdits', async () => {
    const { shapeOp, resolvers } = deferredShapeOp();
    const { applyEdits, onSaved, appendLog } = renderEditor(shapeOp);

    fireEvent.click(screen.getByText('Weld vertices'));
    await waitFor(() => expect(shapeOp).toHaveBeenCalledTimes(1));
    expect(shapeOp.mock.calls[0][1]).toBe(STAGING);
    resolvers[0](report('weld_vertices', STAGING));
    await waitFor(() => expect(saveButton().disabled).toBe(false));

    fireEvent.click(saveButton());
    await waitFor(() => expect(shapeOp).toHaveBeenCalledTimes(2));
    expect(shapeOp.mock.calls[1][0]).toBe(A);
    expect(shapeOp.mock.calls[1][1]).toBe(A);
    expect(shapeOp.mock.calls[1][2]).toBe('weld_vertices');
    resolvers[1](report('weld_vertices', A));

    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    expect(applyEdits).not.toHaveBeenCalled();
    expect(appendLog.mock.calls.some(call => String(call[0]).includes('✓ saved 1 mesh op'))).toBe(true);
    await waitFor(() => expect(saveButton().disabled).toBe(true));
  });

  it('does not commit while the op it would commit is still running', async () => {
    const { shapeOp, resolvers } = deferredShapeOp();
    renderEditor(shapeOp);

    fireEvent.click(screen.getByText('Weld vertices'));
    await waitFor(() => expect(shapeOp).toHaveBeenCalledTimes(1));
    resolvers[0](report('weld_vertices', STAGING));
    await waitFor(() => expect(saveButton().disabled).toBe(false));

    fireEvent.click(screen.getByText('Simplify'));
    await waitFor(() => expect(shapeOp).toHaveBeenCalledTimes(2));
    fireEvent.click(saveButton());
    expect(shapeOp).toHaveBeenCalledTimes(2);
    resolvers[1](report('simplify', STAGING));
    await waitFor(() => expect(shapeOp).toHaveBeenCalledTimes(2));
  });
});
