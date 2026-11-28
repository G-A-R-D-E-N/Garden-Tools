import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';
import type { Backend, CellHost } from './backend';

const bridge = vi.hoisted(() => ({
  refs: vi.fn<CellHost['GetPlacedReferences']>(),
  resolve: vi.fn<CellHost['ResolveWorldspace']>(),
  getLand: vi.fn<CellHost['GetCellLand']>(),
  setLand: vi.fn<CellHost['SetCellLand']>(),
  getWorld: vi.fn<CellHost['GetWorldProperties']>(),
  setWorld: vi.fn<CellHost['SetWorldProperties']>(),
  save: vi.fn<Backend['SavePlugin']>(),
  pick: vi.fn(),
}));

vi.mock('./backend', () => ({
  getCell: () => ({
    GetPlugins: async () => '["Fallout4.esm","Patch.esp"]',
    SearchCells: async () => '[]',
    SearchWorldspaces: async () => '[]',
    GetPlacedReferences: bridge.refs,
    ResolveWorldspace: bridge.resolve,
    GetCellProperties: async () => '{"values":{},"descriptors":[]}',
    GetCellLand: bridge.getLand,
    SetCellLand: bridge.setLand,
    GetWorldProperties: bridge.getWorld,
    SetWorldProperties: bridge.setWorld,
  }),
  getBackend: () => ({
    SavePlugin: bridge.save,
    GetActivePlugins: async () => '[]',
    CreatePlugin: async () => 'ok',
  }),
}));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ pickPlugin: bridge.pick }) }));

const CELL_A = '000A01:Fallout4.esm';
const CELL_B = '000B02:Fallout4.esm';
const WORLD = '00003C:Fallout4.esm';

const land = (cell: string, heights: number[]) =>
  JSON.stringify({ cellFormKey: cell, landFormKey: `L${cell}`, width: 2, height: 2, offset: 0, heights });
const world = (water: number) => JSON.stringify({
  formKey: WORLD,
  descriptors: [{ name: 'WaterHeight', label: 'Water height', kind: 'scalar', minimum: -3.4e38, maximum: 3.4e38, targets: [] }],
  values: { WaterHeight: water },
});

beforeEach(() => {
  vi.resetAllMocks();
  localStorage.clear();
  bridge.refs.mockImplementation(async id => JSON.stringify({ cellFormKey: id, cellEditorId: `Cell${id}`, interior: false, references: [] }));
  bridge.resolve.mockResolvedValue(JSON.stringify({ worldspaceFormKey: WORLD }));
  bridge.getLand.mockImplementation(async id => land(id, id === CELL_A ? [1, 2, 3, 4] : [5, 6, 7, 8]));
  bridge.setLand.mockResolvedValue('ok');
  bridge.getWorld.mockResolvedValue(world(12.5));
  bridge.setWorld.mockResolvedValue('ok');
  bridge.save.mockResolvedValue('Saved');
  bridge.pick.mockResolvedValue({ target: 'Patch.esp' });
});
afterEach(() => { cleanup(); localStorage.clear(); });

async function loadCell(id: string) {
  const search = await screen.findByPlaceholderText(/Search cells/);
  await waitFor(() => expect((search as HTMLInputElement).disabled).toBe(false));
  fireEvent.change(search, { target: { value: id } });
  fireEvent.click(screen.getByRole('button', { name: 'Load Cell' }));
  await screen.findByRole('tab', { name: 'Terrain' });
}

const heights = () => (screen.getByLabelText('Heights') as HTMLTextAreaElement).value;

describe('Cell Viewer terrain and worldspace tabs', () => {
  it('mounts the terrain and world panels through the detail tabs after a load', async () => {
    render(<CellPanel onClose={() => {}} />);
    await loadCell(CELL_A);
    fireEvent.click(screen.getByRole('tab', { name: 'Terrain' }));
    await waitFor(() => expect(heights()).toBe('1 2\n3 4'));
    fireEvent.click(screen.getByRole('tab', { name: 'World' }));
    expect(await screen.findByLabelText('World Water height')).toBeTruthy();
    expect(bridge.getLand).toHaveBeenCalledExactlyOnceWith(CELL_A);
    expect(bridge.getWorld).toHaveBeenCalledExactlyOnceWith(WORLD);
  });

  it('drops the previous cell terrain when another cell loads', async () => {
    localStorage.setItem('cell.detailTab', 'terrain');
    render(<CellPanel onClose={() => {}} />);
    await loadCell(CELL_A);
    await waitFor(() => expect(heights()).toBe('1 2\n3 4'));
    let release: (raw: string) => void = () => {};
    bridge.getLand.mockImplementationOnce(() => new Promise<string>(resolve => { release = resolve; }));
    fireEvent.change(screen.getByPlaceholderText(/Search cells/), { target: { value: CELL_B } });
    fireEvent.click(screen.getByRole('button', { name: 'Load Cell' }));
    await waitFor(() => expect(screen.queryByLabelText('Heights')).toBeNull());
    release(land(CELL_B, [5, 6, 7, 8]));
    await waitFor(() => expect(heights()).toBe('5 6\n7 8'));
  });

  it('shows a terrain read failure instead of the no-terrain placeholder', async () => {
    bridge.getLand.mockRejectedValue(new Error('terrain read failed'));
    render(<CellPanel onClose={() => {}} />);
    await loadCell(CELL_A);
    fireEvent.click(screen.getByRole('tab', { name: 'Terrain' }));
    expect(await screen.findByText('terrain read failed')).toBeTruthy();
    expect(screen.queryByText('This cell has no terrain.')).toBeNull();
  });

  it('keeps terrain on screen when a reload fails', async () => {
    localStorage.setItem('cell.detailTab', 'terrain');
    render(<CellPanel onClose={() => {}} />);
    await loadCell(CELL_A);
    await waitFor(() => expect(heights()).toBe('1 2\n3 4'));
    bridge.getLand.mockRejectedValueOnce(new Error('reload failed'));
    fireEvent.change(screen.getByLabelText('Heights'), { target: { value: '9 9 9 9' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save terrain' }));
    await waitFor(() => expect(bridge.setLand).toHaveBeenCalled());
    expect(screen.getByLabelText('Heights')).toBeTruthy();
    expect(await screen.findByText('reload failed')).toBeTruthy();
    expect(screen.queryByText('This cell has no terrain.')).toBeNull();
  });

  it('saves terrain and world edits into the chosen patch plugin', async () => {
    localStorage.setItem('cell.detailTab', 'terrain');
    render(<CellPanel onClose={() => {}} />);
    await loadCell(CELL_A);
    await waitFor(() => expect(heights()).toBe('1 2\n3 4'));
    fireEvent.change(screen.getByLabelText('Heights'), { target: { value: '9 9 9 9' } });
    bridge.getLand.mockResolvedValue(land(CELL_A, [9, 9, 9, 9]));
    fireEvent.click(screen.getByRole('button', { name: 'Save terrain' }));
    await waitFor(() => expect(bridge.setLand).toHaveBeenCalledExactlyOnceWith(CELL_A, 'Patch.esp', [9, 9, 9, 9]));
    await waitFor(() => expect(heights()).toBe('9 9\n9 9'));
    expect(bridge.save).toHaveBeenCalledWith('Patch.esp', '');
    expect(bridge.refs).toHaveBeenCalledTimes(1);

    fireEvent.click(screen.getByRole('tab', { name: 'World' }));
    const water = await screen.findByLabelText('World Water height');
    bridge.getWorld.mockResolvedValue(world(40));
    fireEvent.change(water, { target: { value: '40' } });
    fireEvent.keyDown(water, { key: 'Enter' });
    await waitFor(() => expect(bridge.setWorld).toHaveBeenCalledExactlyOnceWith(WORLD, 'Patch.esp', { WaterHeight: 40 }));
    await waitFor(() => expect((screen.getByLabelText('World Water height') as HTMLInputElement).value).toBe('40'));
    expect(bridge.refs).toHaveBeenCalledTimes(1);
  });
});
