import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from './backend';
import NifEditor, { type NifTree } from './NifEditor';
import { createMeshToolsStagingPath } from './meshOpsClient';

type ShapeOpFn = (nifPath: string, outNif: string, op: string, blocks: string, crease: number,
                  overwrite: boolean, ratio: number) => Promise<string>;

const A = 'C:\\mods\\a.nif';

vi.stubGlobal('crypto', { randomUUID: () => '00000000-0000-4000-8000-000000000002' });
const STAGING = createMeshToolsStagingPath(A);

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

const OUT = 'C:\\mods\\out.nif';

function renderEditor(shapeOp: ShapeOpFn, stagedCopy = false) {
  const applyEdits = vi.fn<(path: string, payload: string, out: string, source?: string) => Promise<string>>(async () => 'set OK');
  const browseSave = vi.fn(async () => OUT);
  const onSaved = vi.fn();
  const appendLog = vi.fn();
  const host = { ShapeOp: shapeOp, ApplyEdits: applyEdits, BrowseForSave: browseSave } as unknown as NifHost;
  render(<NifEditor tree={tree} nif={host} nifPath={A} stagedCopy={stagedCopy} onSaved={onSaved}
                    onDirtyChange={vi.fn()} appendLog={appendLog} />);
  fireEvent.change(screen.getByRole('combobox', { name: 'Mesh tools shape' }), { target: { value: '3' } });
  return { applyEdits, browseSave, onSaved, appendLog };
}

const byName = (name: string) => screen.getByRole('button', { name }) as HTMLButtonElement;
const saveButton = () => byName('Save');

describe('NifEditor mesh op routing', () => {
  // Invariant: fails when the pose panel is hidden for a real FO4 skin instance type or shown for a file with none.
  it('shows the pose tools only when the tree holds a skin instance Radicle can pose', () => {
    const host = { ShapeOp: vi.fn(), ApplyEdits: vi.fn() } as unknown as NifHost;
    const props = { nif: host, nifPath: A, onSaved: vi.fn(), onDirtyChange: vi.fn(), appendLog: vi.fn() };
    const skin = (type: string) => ({ id: 9, type, name: '', group: 'Other', cat: '', fields: [] });

    render(<NifEditor tree={tree} {...props} />);
    expect(screen.queryByText('Pose tools')).toBeNull();
    cleanup();
    render(<NifEditor tree={{ ...tree, blocks: [...tree.blocks, skin('NiSkinInstance')] }} {...props} />);
    expect(screen.queryByText('Pose tools')).toBeNull();
    cleanup();
    render(<NifEditor tree={{ ...tree, blocks: [...tree.blocks, skin('BSSkin::Instance')] }} {...props} />);
    expect(screen.getByText('Pose tools')).toBeTruthy();
    expect(screen.getByText('9: BSSkin::Instance')).toBeTruthy();
    cleanup();
    render(<NifEditor tree={{ ...tree, blocks: [...tree.blocks, skin('BSDismemberSkinInstance')] }} {...props} />);
    expect(screen.getByText('9: BSDismemberSkinInstance')).toBeTruthy();
  });

  // Invariant: fails when the key panel is shown for a block set_keys refuses or hidden for one it writes.
  it('shows the key tools only for the key blocks set_keys writes', () => {
    const host = { ShapeOp: vi.fn(), ApplyEdits: vi.fn() } as unknown as NifHost;
    const props = { nif: host, nifPath: A, onSaved: vi.fn(), onDirtyChange: vi.fn(), appendLog: vi.fn() };
    const keyed = (type: string) => ({ id: 8, type, name: '', group: 'Other', cat: '', fields: [] });

    for (const refused of ['NiBoolData', 'NiTextKeyExtraData']) {
      render(<NifEditor tree={{ ...tree, blocks: [...tree.blocks, keyed(refused)] }} {...props} />);
      expect(screen.queryByText('Key tools')).toBeNull();
      cleanup();
    }
    render(<NifEditor tree={{ ...tree, blocks: [...tree.blocks, keyed('NiPosData')] }} {...props} />);
    expect(screen.getByText('Key tools')).toBeTruthy();
    expect(screen.getByText('8: NiPosData')).toBeTruthy();
  });

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

describe('NifEditor field saves', () => {
  it('refuses a field edit made during a save and keeps the field edits outside the submitted payload (detects a later edit wiped by the save success)', async () => {
    let finish!: (text: string) => void;
    const applyEdits = vi.fn<(path: string, payload: string, out: string, source?: string) => Promise<string>>(() => new Promise<string>(resolve => { finish = resolve; }));
    const onSaved = vi.fn();
    const host = { ShapeOp: vi.fn(), ApplyEdits: applyEdits } as unknown as NifHost;
    render(<NifEditor tree={tree} nif={host} nifPath={A} onSaved={onSaved}
                      onDirtyChange={vi.fn()} appendLog={vi.fn()} />);

    fireEvent.change(screen.getByDisplayValue('Body'), { target: { value: 'First' } });
    fireEvent.click(saveButton());
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(1));
    expect((document.querySelector('.nif-editor-fields') as HTMLFieldSetElement).disabled).toBe(true);

    fireEvent.change(screen.getByDisplayValue('First'), { target: { value: 'Second' } });
    expect(screen.getByDisplayValue('First')).toBeTruthy();
    finish('set OK');

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    expect(JSON.parse(applyEdits.mock.calls[0][1])).toEqual([{ id: 3, cat: '', key: 'name', value: 'First' }]);
    await waitFor(() => expect(screen.getByText('No changes')).toBeTruthy());
  });
});

describe('NifEditor Save As with pending mesh ops (review F4)', () => {
  it('writes the field edits and the mesh ops to the chosen file for a dropped copy and never to the copy (detects Save As refused over pending mesh ops)', async () => {
    const shapeOp = vi.fn<ShapeOpFn>(async (_input, out) => report('weld_vertices', out));
    const { applyEdits, browseSave, onSaved, appendLog } = renderEditor(shapeOp, true);

    fireEvent.click(screen.getByText('Weld vertices'));
    await waitFor(() => expect(byName('Save As…').disabled).toBe(false));
    fireEvent.change(screen.getByDisplayValue('Body'), { target: { value: 'Second' } });
    fireEvent.click(byName('Save As…'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    expect(browseSave).toHaveBeenCalledTimes(1);
    expect(applyEdits.mock.calls[0]).toEqual([A, JSON.stringify([{ id: 3, cat: '', key: 'name', value: 'Second' }]), OUT, '']);
    expect(shapeOp).toHaveBeenCalledTimes(2);
    expect(shapeOp.mock.calls[1][0]).toBe(OUT);
    expect(shapeOp.mock.calls[1][1]).toBe(OUT);
    expect(shapeOp.mock.calls[1][2]).toBe('weld_vertices');
    expect(appendLog.mock.calls.some(call => String(call[0]).includes('save as is not available'))).toBe(false);
    await waitFor(() => expect(byName('Save As…').disabled).toBe(true));
  });

  it('replays mesh ops from the dropped copy straight into the chosen file when no field changed (detects the copy being written or skipped)', async () => {
    const shapeOp = vi.fn<ShapeOpFn>(async (_input, out) => report('weld_vertices', out));
    const { applyEdits, browseSave, onSaved } = renderEditor(shapeOp, true);

    fireEvent.click(screen.getByText('Weld vertices'));
    await waitFor(() => expect(byName('Save As…').disabled).toBe(false));
    fireEvent.click(byName('Save As…'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    expect(browseSave).toHaveBeenCalledTimes(1);
    expect(applyEdits).not.toHaveBeenCalled();
    expect(shapeOp).toHaveBeenCalledTimes(2);
    expect(shapeOp.mock.calls[1][0]).toBe(A);
    expect(shapeOp.mock.calls[1][1]).toBe(OUT);
  });

  it('still refuses Save As over pending mesh ops on a NIF that is not a dropped copy (detects Save As writing a regular file through the mesh path)', async () => {
    const shapeOp = vi.fn<ShapeOpFn>(async (_input, out) => report('weld_vertices', out));
    const { browseSave, appendLog } = renderEditor(shapeOp, false);

    fireEvent.click(screen.getByText('Weld vertices'));
    await waitFor(() => expect(byName('Save As…').disabled).toBe(false));
    fireEvent.click(byName('Save As…'));

    await waitFor(() => expect(appendLog.mock.calls.some(call => String(call[0]).includes('save as is not available while mesh ops are pending'))).toBe(true));
    expect(browseSave).not.toHaveBeenCalled();
    expect(shapeOp).toHaveBeenCalledTimes(1);
  });
});
