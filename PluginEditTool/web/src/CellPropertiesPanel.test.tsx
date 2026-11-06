import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';
import type { CellPlacedReference, CellPropertiesResult, CellPropertyChanges } from './backend';

type ViewportProps = {
  references: CellPlacedReference[];
  onSelect?: (ref: CellPlacedReference | null) => void;
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
    GetCellProperties: vi.fn(),
    SetCellProperties: vi.fn(),
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

const DESCRIPTORS: CellPropertiesResult['descriptors'] = [
  { name: 'WaterHeight', label: 'Water height', kind: 'scalar', minimum: -3.4e38, maximum: 3.4e38, multiple: false, targets: [] },
  { name: 'LightingAmbient', label: 'Ambient color', kind: 'colour', minimum: 0, maximum: 255, multiple: false, targets: [] },
  { name: 'ImageSpace', label: 'Image space', kind: 'link', minimum: 0, maximum: 0, multiple: false, targets: ['IMGS'] },
  { name: 'Water', label: 'Water', kind: 'link', minimum: 0, maximum: 0, multiple: false, targets: ['WATR'] },
  { name: 'Regions', label: 'Regions', kind: 'link', minimum: 0, maximum: 0, multiple: true, targets: ['REGN'] },
];
const START: CellPropertiesResult = {
  cellFormKey: CELL,
  cellEditorId: 'TestCell',
  descriptors: DESCRIPTORS,
  values: {
    WaterHeight: 12.5,
    LightingAmbient: [10, 20, 30],
    ImageSpace: '000A11:Fallout4.esm',
    Water: null,
    Regions: ['000B22:Fallout4.esm', '000B23:Fallout4.esm'],
  },
};

let refs: CellPlacedReference[] = [];
let props: CellPropertiesResult = START;
let undoCount = 0;

const listing = () => JSON.stringify({ cellFormKey: CELL, cellEditorId: 'TestCell', references: refs });
const valueOf = (label: string) => (screen.getByLabelText(label) as HTMLInputElement).value;
const undoEditButton = () => screen.getByRole('button', { name: /^Undo edit/ }) as HTMLButtonElement;

function commitField(label: string, value: string) {
  const input = screen.getByLabelText(label);
  fireEvent.change(input, { target: { value } });
  fireEvent.keyDown(input, { key: 'Enter' });
}

async function openCell() {
  render(<CellPanel onClose={() => {}} />);
  await waitFor(() => expect((screen.getByRole('button', { name: 'Load Cell' }) as HTMLButtonElement).disabled).toBe(false));
  fireEvent.click(screen.getByRole('button', { name: 'Load Cell' }));
  await screen.findByLabelText('Water height');
}

beforeEach(() => {
  vi.resetAllMocks();
  localStorage.clear();
  localStorage.setItem('cell.pickMode', 'grid');
  localStorage.setItem('cell.worldspace', '00003C:Fallout4.esm');
  localStorage.setItem('cell.gridX', '0');
  localStorage.setItem('cell.gridY', '0');
  refs = [REF];
  props = START;
  undoCount = 0;
  rpc.viewport = null;
  rpc.pick.mockResolvedValue({ target: PATCH, extra: '' });
  rpc.backend.SavePlugin.mockResolvedValue('Saved');
  rpc.cell.GetPlugins.mockResolvedValue('["Fallout4.esm"]');
  rpc.cell.SearchCells.mockResolvedValue('[]');
  rpc.cell.SearchWorldspaces.mockResolvedValue('[]');
  rpc.cell.ResolveWorldspace.mockResolvedValue('{}');
  rpc.cell.GetPlacedReferencesAtGrid.mockImplementation(async () => listing());
  rpc.cell.GetPlacedReferences.mockImplementation(async () => listing());
  rpc.cell.GetCellProperties.mockImplementation(async () => JSON.stringify(props));
  rpc.cell.SetCellProperties.mockImplementation(async (_cell: string, _patch: string, changes: CellPropertyChanges) => {
    props = { ...props, values: { ...props.values, ...changes } };
    undoCount += 1;
    return '{}';
  });
  rpc.cell.GetCellEditHistory.mockImplementation(async () => JSON.stringify({ undoCount, redoCount: 0 }));
});
afterEach(() => { cleanup(); localStorage.clear(); });

describe('Cell Viewer cell properties', () => {
  it('shows the values the backend returned, with a null link as an empty field', async () => {
    await openCell();
    expect(valueOf('Water height')).toBe('12.5');
    expect(valueOf('Ambient color')).toBe('10 20 30');
    expect(valueOf('Image space')).toBe('000A11:Fallout4.esm');
    expect(valueOf('Water')).toBe('');
    expect(valueOf('Regions')).toBe('000B22:Fallout4.esm 000B23:Fallout4.esm');
  });

  it('a committed field sends one SetCellProperties with only that key, and a later blur sends nothing', async () => {
    await openCell();
    commitField('Water height', '20');
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledTimes(1));
    expect(rpc.cell.SetCellProperties).toHaveBeenLastCalledWith(CELL, PATCH, { WaterHeight: 20 });
    await waitFor(() => expect(rpc.cell.GetCellEditHistory).toHaveBeenCalledTimes(1));
    fireEvent.blur(screen.getByLabelText('Water height'));
    expect(rpc.cell.SetCellProperties).toHaveBeenCalledTimes(1);
  });

  it('clearing a link field sends null, not an empty string', async () => {
    await openCell();
    commitField('Image space', '');
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledTimes(1));
    expect(rpc.cell.SetCellProperties).toHaveBeenLastCalledWith(CELL, PATCH, { ImageSpace: null });
  });

  it('an error reply is shown, the input reverts, and the patch is not saved', async () => {
    rpc.cell.SetCellProperties.mockRejectedValue(new Error('CellPropertyInvalid: A value for WaterHeight is out of range'));
    await openCell();
    commitField('Water height', '20');
    expect(await screen.findByText(/out of range/)).toBeTruthy();
    expect(valueOf('Water height')).toBe('12.5');
    expect(rpc.backend.SavePlugin).not.toHaveBeenCalled();
    expect(rpc.cell.GetCellProperties).toHaveBeenCalledTimes(1);
  });

  it('a cell property edit refreshes the undo count so Undo edit is enabled', async () => {
    await openCell();
    act(() => rpc.viewport?.onSelect?.(REF));
    await screen.findByRole('button', { name: /^Undo edit/ });
    expect(undoEditButton().disabled).toBe(true);
    commitField('Water height', '20');
    await waitFor(() => expect(undoEditButton().disabled).toBe(false));
    expect(rpc.cell.GetCellEditHistory).toHaveBeenCalledWith(PATCH);
  });
});
