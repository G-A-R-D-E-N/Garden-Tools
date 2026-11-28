import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';
import { LIGHTING_SETTINGS_STORAGE_KEY } from './cellLighting';

const bridge = vi.hoisted(() => ({
  load: vi.fn<(...args: unknown[]) => Promise<string>>(),
}));

vi.mock('./backend', () => ({
  getCell: () => ({
    GetPlugins: async () => '["Fallout4.esm"]',
    SearchCells: async () => '[]',
    SearchWorldspaces: async () => '[]',
    GetPlacedReferencesAtGrid: bridge.load,
    GetReferenceDetails: async () => '{}',
  }),
  getBackend: () => ({}),
}));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ pickPlugin: vi.fn() }) }));
vi.mock('./CellViewport', () => ({ default: () => null }));

const ref = {
  formKey: '00000ABC:Plugin.esp',
  editorId: 'Ref1',
  recordType: 'REFR',
  baseType: 'STAT',
  baseFormKey: '00012345:Base.esm',
  baseEditorId: 'Rock01',
  modelPath: null,
  position: { x: 1, y: 2, z: 3 },
  rotation: { x: 0, y: 0, z: 0 },
  scale: 1,
};

async function loadCell() {
  render(<CellPanel onClose={() => {}} />);
  const inputs = screen.getAllByRole('spinbutton') as HTMLInputElement[];
  await waitFor(() => expect(inputs.every(input => !input.disabled)).toBe(true));
  fireEvent.keyDown(inputs[0], { key: 'Enter' });
  await screen.findByText('Cell');
}

const storedLayers = () => JSON.parse(localStorage.getItem(LIGHTING_SETTINGS_STORAGE_KEY) ?? '{}').layers;

beforeEach(() => {
  vi.resetAllMocks();
  localStorage.clear();
  localStorage.setItem('cell.pickMode', 'grid');
  localStorage.setItem('cell.worldspace', '00003C:Fallout4.esm');
  localStorage.setItem('cell.gridX', '1');
  localStorage.setItem('cell.gridY', '0');
  bridge.load.mockResolvedValue(JSON.stringify({ references: [ref] }));
});

afterEach(() => {
  cleanup();
  localStorage.clear();
});

describe('Cell Viewer Layers tab', () => {
  it('moves the layer list out of the Info tab into its own Layers tab', async () => {
    await loadCell();
    expect(screen.queryByRole('button', { name: /^Layers \(/ })).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Layers' }));
    expect(await screen.findByRole('button', { name: 'Layers (1)' })).toBeTruthy();
    expect(screen.getByText('(markers)')).toBeTruthy();
    expect(screen.getByRole('checkbox', { name: 'Room bounds' })).toBeTruthy();
    expect(screen.getByRole('checkbox', { name: 'Cell fog' })).toBeTruthy();
  });

  it('hiding a record type from the Layers tab offers Show all', async () => {
    await loadCell();
    fireEvent.click(screen.getByRole('button', { name: 'Layers' }));
    fireEvent.click(screen.getByText('(markers)'));
    expect(screen.getByRole('button', { name: 'Show all' })).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Show all' }));
    expect(screen.queryByRole('button', { name: 'Show all' })).toBeNull();
  });

  it('category toggles update the stored lighting settings', async () => {
    await loadCell();
    fireEvent.click(screen.getByRole('button', { name: 'Layers' }));
    fireEvent.click(screen.getByRole('checkbox', { name: 'Triggers' }));
    expect(storedLayers().trigger).toBe(false);
    fireEvent.click(screen.getByRole('checkbox', { name: 'Triggers' }));
    expect(storedLayers().trigger).toBe(true);
  });

  it('the Neutral toggle persists through the stored lighting settings', async () => {
    await loadCell();
    fireEvent.click(screen.getByRole('button', { name: 'Layers' }));
    fireEvent.click(screen.getByRole('button', { name: 'Neutral lighting' }));
    expect(JSON.parse(localStorage.getItem(LIGHTING_SETTINGS_STORAGE_KEY) ?? '{}').neutral).toBe(true);
  });
});
