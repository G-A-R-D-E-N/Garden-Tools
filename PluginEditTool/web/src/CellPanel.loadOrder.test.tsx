import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';

const bridge = vi.hoisted(() => ({
  load: vi.fn<(...args: unknown[]) => Promise<string>>(),
  geometry: vi.fn<(...args: unknown[]) => Promise<string>>(),
  progress: vi.fn<(...args: unknown[]) => Promise<string>>(),
}));

vi.mock('./backend', () => ({
  getCell: () => ({
    GetPlugins: async () => '["Fallout4.esm"]',
    SearchCells: async () => '[]',
    SearchWorldspaces: async () => '[]',
    GetPlacedReferencesAtGrid: bridge.load,
    GetGeometryBatch: bridge.geometry,
    GetGeometryBatchProgress: bridge.progress,
    GetCellProperties: async () => '{"values":{},"descriptors":[]}',
  }),
  getBackend: () => ({}),
}));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ pickPlugin: vi.fn() }) }));
vi.mock('./CellViewport', () => ({ default: () => null }));

beforeEach(() => {
  vi.resetAllMocks();
  localStorage.clear();
  localStorage.setItem('cell.pickMode', 'grid');
  localStorage.setItem('cell.worldspace', '00003C:Fallout4.esm');
  localStorage.setItem('cell.gridX', '1');
  localStorage.setItem('cell.gridY', '0');
});
afterEach(() => { cleanup(); localStorage.clear(); });

describe('Cell Viewer load ordering', () => {
  it('keeps the newest grid load when an older load resolves last', async () => {
    const resolvers: Array<(raw: string) => void> = [];
    bridge.load.mockImplementation(() => new Promise<string>(resolve => { resolvers.push(resolve); }));
    render(<CellPanel onClose={() => {}} />);
    const inputs = screen.getAllByRole('spinbutton') as HTMLInputElement[];
    await waitFor(() => expect(inputs.every(input => !input.disabled)).toBe(true));
    fireEvent.keyDown(inputs[0], { key: 'Enter' });
    fireEvent.change(inputs[0], { target: { value: '2' } });
    fireEvent.keyDown(inputs[0], { key: 'Enter' });
    await waitFor(() => expect(resolvers).toHaveLength(2));
    await act(async () => { resolvers[1]('{"references":[]}'); });
    await act(async () => { resolvers[0]('{"error":"stale cell"}'); });
    expect(await screen.findByText('Loaded -- no placeable meshes in this cell.')).toBeTruthy();
    expect(screen.queryByText('stale cell')).toBeNull();
  });

  it('keeps the newer conversion progress when an older conversion finishes last', async () => {
    const geometry: Array<(raw: string) => void> = [];
    bridge.geometry.mockImplementation(() => new Promise<string>(resolve => { geometry.push(resolve); }));
    bridge.load
      .mockResolvedValueOnce('{"references":[{"modelPath":"A.nif"}]}')
      .mockResolvedValueOnce('{"references":[{"modelPath":"B.nif"},{"modelPath":"C.nif"}]}');
    render(<CellPanel onClose={() => {}} />);
    const inputs = screen.getAllByRole('spinbutton') as HTMLInputElement[];
    await waitFor(() => expect(inputs.every(input => !input.disabled)).toBe(true));
    fireEvent.keyDown(inputs[0], { key: 'Enter' });
    await waitFor(() => expect(geometry).toHaveLength(1));
    fireEvent.change(inputs[0], { target: { value: '2' } });
    fireEvent.keyDown(inputs[0], { key: 'Enter' });
    await waitFor(() => expect(geometry).toHaveLength(2));
    expect(await screen.findByText('0/2')).toBeTruthy();
    await act(async () => { geometry[0]('{"count":1,"geometry":{}}'); });
    expect(screen.getByText('0/2')).toBeTruthy();
  });

  it('clears the conversion progress of a superseded load when the newer load has no meshes', async () => {
    const geometry: Array<(raw: string) => void> = [];
    bridge.geometry.mockImplementation(() => new Promise<string>(resolve => { geometry.push(resolve); }));
    bridge.load
      .mockResolvedValueOnce('{"references":[{"modelPath":"A.nif"}]}')
      .mockResolvedValueOnce('{"references":[]}');
    render(<CellPanel onClose={() => {}} />);
    const inputs = screen.getAllByRole('spinbutton') as HTMLInputElement[];
    await waitFor(() => expect(inputs.every(input => !input.disabled)).toBe(true));
    fireEvent.keyDown(inputs[0], { key: 'Enter' });
    expect(await screen.findByText('Converting meshes…')).toBeTruthy();
    fireEvent.change(inputs[0], { target: { value: '2' } });
    fireEvent.keyDown(inputs[0], { key: 'Enter' });
    await waitFor(() => expect(bridge.load).toHaveBeenCalledTimes(2));
    await act(async () => { geometry[0]('{"count":1,"geometry":{}}'); });
    expect(screen.queryByText('Converting meshes…')).toBeNull();
  });

  it('shows archive indexing progress while the native index is still being built', async () => {
    const geometry: Array<(raw: string) => void> = [];
    bridge.geometry.mockImplementation(() => new Promise<string>(resolve => { geometry.push(resolve); }));
    bridge.progress.mockResolvedValue('{"done":0,"total":1,"phase":"indexing","indexDone":3,"indexTotal":79}');
    bridge.load.mockResolvedValueOnce('{"references":[{"modelPath":"A.nif"}]}');
    render(<CellPanel onClose={() => {}} />);
    const inputs = screen.getAllByRole('spinbutton') as HTMLInputElement[];
    await waitFor(() => expect(inputs.every(input => !input.disabled)).toBe(true));
    fireEvent.keyDown(inputs[0], { key: 'Enter' });
    expect(await screen.findByText('Indexing archives…')).toBeTruthy();
    expect(await screen.findByText('3/79')).toBeTruthy();
    expect(screen.queryByText('Converting meshes…')).toBeNull();
    await act(async () => { geometry[0]('{"count":1,"geometry":{}}'); });
    expect(screen.queryByText('Indexing archives…')).toBeNull();
  });

  it('shows mesh conversion progress once the polled phase is converting', async () => {
    const geometry: Array<(raw: string) => void> = [];
    bridge.geometry.mockImplementation(() => new Promise<string>(resolve => { geometry.push(resolve); }));
    bridge.progress.mockResolvedValue('{"done":1,"total":2,"phase":"converting","indexDone":79,"indexTotal":79}');
    bridge.load.mockResolvedValueOnce('{"references":[{"modelPath":"A.nif"},{"modelPath":"B.nif"}]}');
    render(<CellPanel onClose={() => {}} />);
    const inputs = screen.getAllByRole('spinbutton') as HTMLInputElement[];
    await waitFor(() => expect(inputs.every(input => !input.disabled)).toBe(true));
    fireEvent.keyDown(inputs[0], { key: 'Enter' });
    expect(await screen.findByText('Converting meshes…')).toBeTruthy();
    expect(await screen.findByText('1/2')).toBeTruthy();
    expect(screen.queryByText('Indexing archives…')).toBeNull();
    await act(async () => { geometry[0]('{"count":2,"geometry":{}}'); });
  });
});
