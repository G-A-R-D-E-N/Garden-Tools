import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';
import type { Backend, CellHost } from './backend';

const bridge = vi.hoisted(() => ({
  load: vi.fn<CellHost['GetPlacedReferencesAtGrid']>(),
  create: vi.fn<Backend['CreateRecord']>(),
  save: vi.fn<Backend['SavePlugin']>(),
  pick: vi.fn(),
  resolve: vi.fn<CellHost['ResolveWorldspace']>(),
}));

vi.mock('./backend', () => ({
  getCell: () => ({
    GetPlugins: async () => '["Fallout4.esm"]',
    SearchCells: async () => '[]',
    SearchWorldspaces: async () => '[]',
    GetPlacedReferencesAtGrid: bridge.load,
    ResolveWorldspace: bridge.resolve,
  }),
  getBackend: () => ({ CreateRecord: bridge.create, SavePlugin: bridge.save }),
}));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ pickPlugin: bridge.pick }) }));

beforeEach(() => {
  vi.resetAllMocks();
  localStorage.clear();
  localStorage.setItem('cell.pickMode', 'grid');
  localStorage.setItem('cell.worldspace', '00003C:Fallout4.esm');
  bridge.load.mockResolvedValue('{"references":[]}');
  bridge.create.mockResolvedValue('{"formKey":"000900:Patch.esp"}');
  bridge.save.mockResolvedValue('Saved');
  bridge.pick.mockResolvedValue({ target: 'Patch.esp', extra: 'MyCell' });
});
afterEach(() => { cleanup(); localStorage.clear(); });

async function openGrid(x: string, y: string) {
  localStorage.setItem('cell.gridX', x);
  localStorage.setItem('cell.gridY', y);
  render(<CellPanel onClose={() => {}} />);
  const inputs = screen.getAllByRole('spinbutton') as HTMLInputElement[];
  await waitFor(() => expect(inputs.every(input => !input.disabled)).toBe(true));
  return inputs;
}

describe('Cell Viewer exterior grid dispatch', () => {
  it.each([
    ['12abc', '0'], ['1.5', '0'], ['1e3', '0'],
    ['9007199254740992', '0'], ['', '0'], ['0', '1.5'],
  ])('refuses X=%s Y=%s through buttons and Enter', async (x, y) => {
    const inputs = await openGrid(x, y);
    const load = screen.getByRole('button', { name: 'Load Cell' }) as HTMLButtonElement;
    const create = screen.getByRole('button', { name: 'Create exterior CELL' }) as HTMLButtonElement;
    expect(load.disabled).toBe(true);
    expect(create.disabled).toBe(true);
    fireEvent.click(load);
    fireEvent.click(create);
    for (const input of inputs) fireEvent.keyDown(input, { key: 'Enter' });
    expect(bridge.load).not.toHaveBeenCalled();
    expect(bridge.pick).not.toHaveBeenCalled();
    expect(bridge.create).not.toHaveBeenCalled();
    expect(bridge.save).not.toHaveBeenCalled();
  });

  it('loads signed coordinates after editing an invalid value', async () => {
    const [x] = await openGrid('1.5', '-33');
    fireEvent.change(x, { target: { value: '32' } });
    fireEvent.click(screen.getByRole('button', { name: 'Load Cell' }));
    await waitFor(() => expect(bridge.load).toHaveBeenCalledExactlyOnceWith('00003C:Fallout4.esm', 32, -33));
    expect(localStorage.getItem('cell.gridX')).toBe('32');
    expect(bridge.create).not.toHaveBeenCalled();
  });

  it('creates, saves and reloads the same signed coordinates', async () => {
    await openGrid('-33', '+32');
    fireEvent.click(screen.getByRole('button', { name: 'Create exterior CELL' }));
    await waitFor(() => expect(bridge.load).toHaveBeenCalledExactlyOnceWith('00003C:Fallout4.esm', -33, 32));
    expect(bridge.create).toHaveBeenCalledExactlyOnceWith('Patch.esp', 'CELL', {
      worldspace: '00003C:Fallout4.esm', x: -33, y: 32, editorId: 'MyCell',
    });
    expect(bridge.save).toHaveBeenCalledExactlyOnceWith('Patch.esp', '');
  });

  it('shows the worldspace the backend resolves for the loaded cell, not the typed text', async () => {
    localStorage.setItem('cell.worldspace', 'Commonwealth');
    bridge.load.mockResolvedValue('{"cellFormKey":"0000ABCD:Fallout4.esm","cellEditorId":"MyCell","references":[]}');
    bridge.resolve.mockResolvedValue('{"cellFormKey":"0000ABCD:Fallout4.esm","worldspaceFormKey":"00003C:Fallout4.esm"}');
    await openGrid('1', '0');
    fireEvent.click(screen.getByRole('button', { name: 'Load Cell' }));
    const stat = (await screen.findByText('Worldspace')).closest('.cell-stat');
    expect(stat?.querySelector('strong')?.textContent).toBe('00003C:Fallout4.esm');
    expect(bridge.resolve).toHaveBeenCalledExactlyOnceWith('0000ABCD:Fallout4.esm');
  });

  it('keeps the loaded cell when the worldspace lookup fails', async () => {
    bridge.load.mockResolvedValue('{"cellFormKey":"0000ABCD:Fallout4.esm","cellEditorId":"MyCell","references":[]}');
    bridge.resolve.mockRejectedValue(new Error('CellReadFailed'));
    await openGrid('1', '0');
    fireEvent.click(screen.getByRole('button', { name: 'Load Cell' }));
    expect(await screen.findByText('MyCell')).toBeTruthy();
    expect(await screen.findByText('Loaded -- no placeable meshes in this cell.')).toBeTruthy();
    expect(screen.queryByText('Worldspace')).toBeNull();
    expect(bridge.resolve).toHaveBeenCalledExactlyOnceWith('0000ABCD:Fallout4.esm');
  });
});
