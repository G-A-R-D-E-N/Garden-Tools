import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';
import type { CellEditChanges, CellPlacedReference } from './backend';
import { cellEditKey, type CellEditKey } from './util/cellEditKeys';

type ViewportProps = {
  references: CellPlacedReference[];
  selectedKey?: string | null;
  hiddenRefs?: ReadonlySet<string>;
  onSelect?: (ref: CellPlacedReference | null) => void;
  onHideSelected?: () => void;
  onUndoEdit?: () => void;
  onRedoEdit?: () => void;
  onDeleteSelected?: () => void;
  onDuplicateSelected?: () => void;
};

const rpc = vi.hoisted(() => ({
  viewport: null as ViewportProps | null,
  pick: vi.fn(),
  backend: { SavePlugin: vi.fn() },
  cell: {
    GetPlugins: vi.fn(),
    SearchCells: vi.fn(),
    SearchWorldspaces: vi.fn(),
    ResolveWorldspace: vi.fn(),
    GetPlacedReferencesAtGrid: vi.fn(),
    GetPlacedReferences: vi.fn(),
    EditPlacedReference: vi.fn(),
    DeletePlacedReference: vi.fn(),
    DuplicatePlacedReference: vi.fn(),
    UndoCellEdit: vi.fn(),
    RedoCellEdit: vi.fn(),
    GetCellEditHistory: vi.fn(),
  },
}));

vi.mock('./backend', () => ({
  getCell: () => rpc.cell,
  getBackend: () => rpc.backend,
}));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ pickPlugin: rpc.pick }) }));
vi.mock('./CellViewport', () => ({
  default: (props: ViewportProps) => {
    rpc.viewport = props;
    return null;
  },
}));

const CELL = '00ABCD:Fallout4.esm';
const PATCH = 'Patch.esp';
const NEW_KEY = '000900:Patch.esp';
const REF: CellPlacedReference = {
  formKey: '000801:Fallout4.esm',
  editorId: 'Door01',
  recordType: 'REFR',
  baseType: 'DOOR',
  baseFormKey: '000802:Fallout4.esm',
  baseEditorId: 'Door',
  modelPath: null,
  position: { x: 1, y: 2, z: 3 },
  rotation: { x: 0, y: 0, z: 0 },
  scale: 1,
};
const OTHER: CellPlacedReference = { ...REF, formKey: '000811:Fallout4.esm', editorId: 'Door03' };

let refs: CellPlacedReference[] = [];

const listing = () => JSON.stringify({ cellFormKey: CELL, cellEditorId: 'TestCell', references: refs });

function applied(r: CellPlacedReference, c: CellEditChanges): CellPlacedReference {
  return {
    ...r,
    ...(c.transform ? {
      position: { x: c.transform.x, y: c.transform.y, z: c.transform.z },
      rotation: { x: c.transform.rx, y: c.transform.ry, z: c.transform.rz },
    } : {}),
    ...(c.scale !== undefined ? { scale: c.scale ?? 1 } : {}),
    ...(c.editorId !== undefined ? { editorId: c.editorId } : {}),
  };
}

const view = (): ViewportProps => {
  if (!rpc.viewport) throw new Error('CellViewport is not mounted');
  return rpc.viewport;
};
const valueOf = (label: string) => (screen.getByLabelText(label) as HTMLInputElement).value;
const undoEditButton = () => screen.getByRole('button', { name: /^Undo edit/ }) as HTMLButtonElement;

function commitField(label: string, value: string) {
  const input = screen.getByLabelText(label);
  fireEvent.change(input, { target: { value } });
  fireEvent.keyDown(input, { key: 'Enter' });
}

async function openSelected() {
  render(<CellPanel onClose={() => {}} />);
  await waitFor(() => expect((screen.getByRole('button', { name: 'Load Cell' }) as HTMLButtonElement).disabled).toBe(false));
  fireEvent.click(screen.getByRole('button', { name: 'Load Cell' }));
  await waitFor(() => expect(rpc.viewport?.references).toHaveLength(refs.length));
  act(() => view().onSelect?.(view().references[0]));
  await screen.findByLabelText('Pos X');
}

beforeEach(() => {
  vi.resetAllMocks();
  localStorage.clear();
  localStorage.setItem('cell.pickMode', 'grid');
  localStorage.setItem('cell.worldspace', '00003C:Fallout4.esm');
  localStorage.setItem('cell.gridX', '0');
  localStorage.setItem('cell.gridY', '0');
  refs = [REF];
  rpc.viewport = null;
  rpc.pick.mockResolvedValue({ target: PATCH, extra: '' });
  rpc.backend.SavePlugin.mockResolvedValue('Saved');
  rpc.cell.GetPlugins.mockResolvedValue('["Fallout4.esm"]');
  rpc.cell.SearchCells.mockResolvedValue('[]');
  rpc.cell.SearchWorldspaces.mockResolvedValue('[]');
  rpc.cell.ResolveWorldspace.mockResolvedValue('{}');
  rpc.cell.GetPlacedReferencesAtGrid.mockImplementation(async () => listing());
  rpc.cell.GetPlacedReferences.mockImplementation(async () => listing());
  rpc.cell.EditPlacedReference.mockImplementation(async (key: string, _patch: string, changes: CellEditChanges) => {
    refs = refs.map(r => (r.formKey === key ? applied(r, changes) : r));
    return '{}';
  });
  rpc.cell.DeletePlacedReference.mockImplementation(async (key: string) => {
    refs = refs.filter(r => r.formKey !== key);
    return '{"status":"deleted"}';
  });
  rpc.cell.DuplicatePlacedReference.mockImplementation(async () => {
    refs = [...refs, { ...REF, formKey: NEW_KEY }];
    return `{"newFormKey":"${NEW_KEY}"}`;
  });
  rpc.cell.UndoCellEdit.mockImplementation(async () => {
    refs = [REF];
    return '{}';
  });
  rpc.cell.RedoCellEdit.mockResolvedValue('{}');
  rpc.cell.GetCellEditHistory.mockResolvedValue('{"undoCount":1,"redoCount":0}');
});
afterEach(() => { cleanup(); localStorage.clear(); });

describe('Cell Viewer reference editing', () => {
  it('sends one EditPlacedReference per committed field and nothing on a later blur', async () => {
    await openSelected();
    commitField('Pos X', '5');
    fireEvent.blur(screen.getByLabelText('Pos X'));
    await waitFor(() => expect(view().references[0].position.x).toBe(5));
    expect(rpc.cell.EditPlacedReference).toHaveBeenCalledTimes(1);
    expect(rpc.cell.EditPlacedReference).toHaveBeenLastCalledWith(REF.formKey, PATCH, {
      transform: { x: 5, y: 2, z: 3, rx: 0, ry: 0, rz: 0 },
    });
    fireEvent.blur(screen.getByLabelText('Pos X'));
    expect(rpc.cell.EditPlacedReference).toHaveBeenCalledTimes(1);

    commitField('Scale', '2');
    await waitFor(() => expect(rpc.cell.EditPlacedReference).toHaveBeenCalledTimes(2));
    expect(rpc.cell.EditPlacedReference).toHaveBeenLastCalledWith(REF.formKey, PATCH, { scale: 2 });
    await waitFor(() => expect(view().references[0].scale).toBe(2));

    commitField('EditorID', 'Door02');
    await waitFor(() => expect(rpc.cell.EditPlacedReference).toHaveBeenCalledTimes(3));
    expect(rpc.cell.EditPlacedReference).toHaveBeenLastCalledWith(REF.formKey, PATCH, { editorId: 'Door02' });
    await waitFor(() => expect(view().references[0].editorId).toBe('Door02'));
    fireEvent.blur(screen.getByLabelText('EditorID'));
    expect(rpc.cell.EditPlacedReference).toHaveBeenCalledTimes(3);
  });

  it('a field commit made while another edit is in flight is dropped and cannot revert the first field', async () => {
    await openSelected();
    commitField('Pos X', '5');
    commitField('Pos Y', '7');
    await waitFor(() => expect(view().references[0].position.x).toBe(5));
    expect(rpc.cell.EditPlacedReference).toHaveBeenCalledTimes(1);
    expect(view().references[0].position).toEqual({ x: 5, y: 2, z: 3 });
  });

  it('the Delete action calls DeletePlacedReference for the selected reference and clears the selection', async () => {
    refs = [REF, OTHER];
    await openSelected();
    act(() => view().onDeleteSelected?.());
    await waitFor(() => expect(rpc.cell.DeletePlacedReference).toHaveBeenCalledTimes(1));
    expect(rpc.cell.DeletePlacedReference).toHaveBeenCalledWith(REF.formKey, PATCH);
    await waitFor(() => expect(view().references.map(r => r.formKey)).toEqual([OTHER.formKey]));
    expect(view().selectedKey).toBeNull();
  });

  it('a second delete pressed while the first is running opens the patch picker once and sends one delete', async () => {
    await openSelected();
    act(() => { view().onDeleteSelected?.(); view().onDeleteSelected?.(); });
    await waitFor(() => expect(rpc.cell.DeletePlacedReference).toHaveBeenCalledTimes(1));
    expect(rpc.pick).toHaveBeenCalledTimes(1);
  });

  it('the Duplicate action calls DuplicatePlacedReference and selects the new copy', async () => {
    await openSelected();
    act(() => view().onDuplicateSelected?.());
    await waitFor(() => expect(rpc.cell.DuplicatePlacedReference).toHaveBeenCalledTimes(1));
    expect(rpc.cell.DuplicatePlacedReference).toHaveBeenCalledWith(REF.formKey, PATCH, null);
    await waitFor(() => expect(view().selectedKey).toBe(NEW_KEY));
  });

  it('a selection made during an in-flight edit survives the reload', async () => {
    refs = [REF, OTHER];
    await openSelected();
    commitField('Pos X', '5');
    act(() => view().onSelect?.(view().references[1]));
    await waitFor(() => expect(view().references[0].position.x).toBe(5));
    expect(view().selectedKey).toBe(OTHER.formKey);
  });

  it('Ctrl+Z calls UndoCellEdit, reloads the reference, and leaves hidden meshes hidden', async () => {
    await openSelected();
    commitField('Pos X', '5');
    await waitFor(() => expect(view().references[0].position.x).toBe(5));
    await waitFor(() => expect(undoEditButton().disabled).toBe(false));
    act(() => view().onHideSelected?.());
    act(() => view().onUndoEdit?.());
    await waitFor(() => expect(rpc.cell.UndoCellEdit).toHaveBeenCalledWith(PATCH));
    await waitFor(() => expect(view().references[0].position.x).toBe(1));
    expect(valueOf('Pos X')).toBe('1');
    expect(rpc.backend.SavePlugin).toHaveBeenLastCalledWith(PATCH, '');
    expect(view().hiddenRefs?.has(REF.formKey)).toBe(true);
  });

  it('a second undo pressed while the first is running is dropped', async () => {
    await openSelected();
    commitField('Pos X', '5');
    await waitFor(() => expect(undoEditButton().disabled).toBe(false));
    act(() => { view().onUndoEdit?.(); view().onUndoEdit?.(); });
    await waitFor(() => expect(view().references[0].position.x).toBe(1));
    expect(rpc.cell.UndoCellEdit).toHaveBeenCalledTimes(1);
  });

  it('the visibility Undo button restores hidden meshes and never calls UndoCellEdit', async () => {
    await openSelected();
    act(() => view().onHideSelected?.());
    await waitFor(() => expect(view().hiddenRefs?.has(REF.formKey)).toBe(true));
    fireEvent.click(screen.getByRole('button', { name: 'Undo' }));
    await waitFor(() => expect(view().hiddenRefs?.has(REF.formKey)).toBe(false));
    expect(rpc.cell.UndoCellEdit).not.toHaveBeenCalled();
  });
});

const keyEvent = (key: string, mods: { ctrlKey?: boolean; altKey?: boolean; shiftKey?: boolean } = {}) =>
  ({ key, ctrlKey: false, altKey: false, shiftKey: false, ...mods });

describe('cellEditKey', () => {
  const cases: [string, { ctrlKey?: boolean }, CellEditKey | null][] = [
    ['Delete', {}, 'delete'],
    ['d', { ctrlKey: true }, 'duplicate'],
    ['z', { ctrlKey: true }, 'undo'],
    ['y', { ctrlKey: true }, 'redo'],
    ['d', {}, null],
    ['Delete', { ctrlKey: true }, null],
  ];
  it.each(cases)('maps %s with %o to %s', (key, mods, expected) => {
    expect(cellEditKey(keyEvent(key, mods))).toBe(expected);
  });
});
