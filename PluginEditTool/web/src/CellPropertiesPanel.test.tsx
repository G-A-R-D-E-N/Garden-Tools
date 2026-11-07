import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';
import CellPropertiesPanel from './CellPropertiesPanel';
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
    fireEvent.click(screen.getByRole('button', { name: 'Clear Image space' }));
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

const FLAGS_D: CellPropertiesResult['descriptors'][number] = { name: 'Flags', label: 'Flags', kind: 'flags', minimum: 0, maximum: 65535, multiple: false, targets: [] };
const OWNER_D: CellPropertiesResult['descriptors'][number] = { name: 'Owner', label: 'Owner', kind: 'link', minimum: 0, maximum: 0, multiple: false, targets: ['NPC_', 'FACT'] };
const FACT_HIT = { FormKey: '000D01:Fallout4.esm', EditorID: 'FactionNew', Type: 'FACT', Plugin: 'Fallout4.esm' };
const IMGS_HIT = { FormKey: '000C33:Fallout4.esm', EditorID: 'ImgsNew', Type: 'IMGS', Plugin: 'Fallout4.esm' };
const REGN_HIT = { FormKey: '000C44:Fallout4.esm', EditorID: 'RegionNew', Type: 'REGN', Plugin: 'Fallout4.esm' };

function stubSearch() {
  const SearchRecords = vi.fn(async (_query: string, type: string) => {
    const hits = ({ IMGS: [IMGS_HIT], REGN: [REGN_HIT], FACT: [FACT_HIT], NPC_: [] } as Record<string, unknown[]>)[type] ?? [];
    return JSON.stringify(hits);
  });
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { SearchRecords } } } });
  return SearchRecords;
}

describe('Cell Viewer property controls', () => {
  afterEach(() => { vi.unstubAllGlobals(); });

  it('picks an Image space record from a search filtered to IMGS and sends its FormKey', async () => {
    const search = stubSearch();
    await openCell();
    fireEvent.click(screen.getByRole('button', { name: 'Pick Image space' }));
    fireEvent.click(await screen.findByText('ImgsNew'));
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledWith(CELL, PATCH, { ImageSpace: '000C33:Fallout4.esm' }));
    expect(search).toHaveBeenCalledWith('', 'IMGS');
    expect(screen.queryByText('ImgsNew')).toBeNull();
  });

  it('adds a region to a multi-link field and sends the whole list', async () => {
    stubSearch();
    await openCell();
    fireEvent.click(screen.getByRole('button', { name: 'Pick Regions' }));
    fireEvent.click(await screen.findByText('RegionNew'));
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledWith(CELL, PATCH, {
      Regions: ['000B22:Fallout4.esm', '000B23:Fallout4.esm', '000C44:Fallout4.esm'],
    }));
  });

  it('searches every target type of a link and can pick from either', async () => {
    const search = stubSearch();
    props = { ...START, descriptors: [...DESCRIPTORS, OWNER_D], values: { ...START.values, Owner: null } };
    await openCell();
    fireEvent.click(screen.getByRole('button', { name: 'Pick Owner' }));
    fireEvent.click(await screen.findByText('FactionNew'));
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledWith(CELL, PATCH, { Owner: '000D01:Fallout4.esm' }));
    expect(search).toHaveBeenCalledWith('', 'NPC_');
    expect(search).toHaveBeenCalledWith('', 'FACT');
  });

  it('the colour swatch and the RGB triplet stay in sync and commit one array', async () => {
    await openCell();
    fireEvent.change(screen.getByLabelText('Ambient color swatch'), { target: { value: '#334455' } });
    expect(valueOf('Ambient color')).toBe('51 68 85');
    fireEvent.blur(screen.getByLabelText('Ambient color swatch'));
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledWith(CELL, PATCH, { LightingAmbient: [51, 68, 85] }));
  });

  it('a typed triplet commits on Enter, and an out-of-range triplet is refused without a save', async () => {
    await openCell();
    commitField('Ambient color', '1 2 300');
    expect(await screen.findByText(/enter three values from 0 to 255/)).toBeTruthy();
    expect(rpc.cell.SetCellProperties).not.toHaveBeenCalled();
    expect(valueOf('Ambient color')).toBe('10 20 30');
    commitField('Ambient color', '1, 2, 3');
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledWith(CELL, PATCH, { LightingAmbient: [1, 2, 3] }));
  });

  it('named flag checkboxes toggle the bitmask, and the interior bit is locked', async () => {
    props = { ...START, descriptors: [...DESCRIPTORS, FLAGS_D], values: { ...START.values, Flags: 3 } };
    await openCell();
    expect((screen.getByLabelText('Interior') as HTMLInputElement).disabled).toBe(true);
    expect((screen.getByLabelText('Has water') as HTMLInputElement).checked).toBe(true);
    fireEvent.click(screen.getByLabelText('Public area'));
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledWith(CELL, PATCH, { Flags: 35 }));
  });

  it('unchecking a named flag clears only its bit', async () => {
    props = { ...START, descriptors: [...DESCRIPTORS, FLAGS_D], values: { ...START.values, Flags: 3 } };
    await openCell();
    fireEvent.click(screen.getByLabelText('Has water'));
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledWith(CELL, PATCH, { Flags: 1 }));
  });

  it('unknown bits are a numeric field that keeps the named flags intact', async () => {
    props = { ...START, descriptors: [...DESCRIPTORS, FLAGS_D], values: { ...START.values, Flags: 19 } };
    await openCell();
    expect((screen.getByLabelText('Unknown bits') as HTMLInputElement).value).toBe('16');
    commitField('Unknown bits', '0');
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledWith(CELL, PATCH, { Flags: 3 }));
  });

  it('two flag clicks made before the first save finishes are both sent, in order', async () => {
    props = { ...START, descriptors: [...DESCRIPTORS, FLAGS_D], values: { ...START.values, Flags: 3 } };
    await openCell();
    fireEvent.click(screen.getByLabelText('Public area'));
    fireEvent.click(screen.getByLabelText('Show sky'));
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledTimes(2));
    expect(rpc.cell.SetCellProperties).toHaveBeenNthCalledWith(1, CELL, PATCH, { Flags: 35 });
    expect(rpc.cell.SetCellProperties).toHaveBeenNthCalledWith(2, CELL, PATCH, { Flags: 163 });
  });

  it('a swatch change commits on the colour input change event, without a blur', async () => {
    await openCell();
    fireEvent.change(screen.getByLabelText('Ambient color swatch'), { target: { value: '#334455' } });
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledWith(CELL, PATCH, { LightingAmbient: [51, 68, 85] }));
    expect(rpc.cell.SetCellProperties).toHaveBeenCalledTimes(1);
    fireEvent.blur(screen.getByLabelText('Ambient color swatch'));
    expect(rpc.cell.SetCellProperties).toHaveBeenCalledTimes(1);
  });

  it('a multi-link picker stays open so several regions can be added in one visit', async () => {
    const REGN_TWO = { FormKey: '000C55:Fallout4.esm', EditorID: 'RegionTwo', Type: 'REGN', Plugin: 'Fallout4.esm' };
    vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { SearchRecords: vi.fn(async () => JSON.stringify([REGN_HIT, REGN_TWO])) } } } });
    await openCell();
    fireEvent.click(screen.getByRole('button', { name: 'Pick Regions' }));
    fireEvent.click(await screen.findByText('RegionNew'));
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledTimes(1));
    fireEvent.click(await screen.findByText('RegionTwo'));
    await waitFor(() => expect(rpc.cell.SetCellProperties).toHaveBeenCalledTimes(2));
    expect(rpc.cell.SetCellProperties).toHaveBeenLastCalledWith(CELL, PATCH, {
      Regions: ['000B22:Fallout4.esm', '000B23:Fallout4.esm', '000C44:Fallout4.esm', '000C55:Fallout4.esm'],
    });
  });
});

type Outcome = string | null | Error;

function gatedEdit() {
  const waiting: Array<{ resolve: (failure: string | null) => void; reject: (e: Error) => void }> = [];
  const onEdit = vi.fn((_changes: CellPropertyChanges) => new Promise<string | null>((resolve, reject) => {
    waiting.push({ resolve, reject });
  }));
  const settle = async (outcome: Outcome = null) => {
    const call = waiting.shift();
    if (!call) throw new Error('no save is waiting');
    await act(async () => { if (outcome instanceof Error) call.reject(outcome); else call.resolve(outcome); });
  };
  return { onEdit, settle };
}

const flagsProps = (flags: number): CellPropertiesResult => ({
  cellFormKey: CELL,
  cellEditorId: 'TestCell',
  descriptors: [FLAGS_D],
  values: { Flags: flags },
});
const boxChecked = (label: string) => (screen.getByLabelText(label) as HTMLInputElement).checked;

describe('Cell Viewer optimistic property queue', () => {
  it('a properties refresh while a save is queued keeps the queued value, so a third click builds on it', async () => {
    const edit = gatedEdit();
    const { rerender } = render(<CellPropertiesPanel properties={flagsProps(3)} onEdit={edit.onEdit} />);
    fireEvent.click(screen.getByLabelText('Public area'));
    fireEvent.click(screen.getByLabelText('Show sky'));
    rerender(<CellPropertiesPanel properties={flagsProps(35)} onEdit={edit.onEdit} />);
    expect(boxChecked('Show sky')).toBe(true);
    fireEvent.click(screen.getByLabelText('Has water'));
    expect(boxChecked('Has water')).toBe(false);
    await edit.settle();
    await waitFor(() => expect(edit.onEdit).toHaveBeenCalledTimes(2));
    expect(edit.onEdit).toHaveBeenNthCalledWith(1, { Flags: 35 });
    expect(edit.onEdit).toHaveBeenNthCalledWith(2, { Flags: 161 });
    await edit.settle();
  });

  it('a second click on the same flag during a save toggles it back and the box follows the pending value', async () => {
    const edit = gatedEdit();
    render(<CellPropertiesPanel properties={flagsProps(3)} onEdit={edit.onEdit} />);
    fireEvent.click(screen.getByLabelText('Public area'));
    expect(boxChecked('Public area')).toBe(true);
    fireEvent.click(screen.getByLabelText('Public area'));
    expect(boxChecked('Public area')).toBe(false);
    await edit.settle();
    await waitFor(() => expect(edit.onEdit).toHaveBeenCalledTimes(2));
    expect(edit.onEdit).toHaveBeenNthCalledWith(1, { Flags: 35 });
    expect(edit.onEdit).toHaveBeenNthCalledWith(2, { Flags: 3 });
    await edit.settle();
  });

  it('a refresh with no save pending replaces the optimistic value with the backend value', async () => {
    const edit = gatedEdit();
    const { rerender } = render(<CellPropertiesPanel properties={flagsProps(3)} onEdit={edit.onEdit} />);
    fireEvent.click(screen.getByLabelText('Public area'));
    await edit.settle();
    rerender(<CellPropertiesPanel properties={flagsProps(35)} onEdit={edit.onEdit} />);
    expect(boxChecked('Public area')).toBe(true);
    rerender(<CellPropertiesPanel properties={flagsProps(3)} onEdit={edit.onEdit} />);
    expect(boxChecked('Public area')).toBe(false);
    expect(edit.onEdit).toHaveBeenCalledTimes(1);
  });

  it.each<[string, Outcome, RegExp]>([
    ['a returned failure', 'CellPropertyInvalid: out of range', /out of range/],
    ['a rejected call', new Error('backend unavailable'), /backend unavailable/],
  ])('%s drops the queued change and reverts the optimistic value', async (_name, outcome, message) => {
    const edit = gatedEdit();
    render(<CellPropertiesPanel properties={flagsProps(3)} onEdit={edit.onEdit} />);
    fireEvent.click(screen.getByLabelText('Public area'));
    fireEvent.click(screen.getByLabelText('Show sky'));
    await edit.settle(outcome);
    expect(await screen.findByText(message)).toBeTruthy();
    expect(boxChecked('Public area')).toBe(false);
    expect(boxChecked('Show sky')).toBe(false);
    expect(boxChecked('Has water')).toBe(true);
    expect(edit.onEdit).toHaveBeenCalledTimes(1);
  });
});
