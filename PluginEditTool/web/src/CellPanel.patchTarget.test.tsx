import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';

const bridge = vi.hoisted(() => ({
  active: vi.fn<(...args: unknown[]) => Promise<string>>(),
  createPlugin: vi.fn<(...args: unknown[]) => Promise<string>>(),
  history: vi.fn<(...args: unknown[]) => Promise<string>>(),
  pick: vi.fn<(...args: unknown[]) => Promise<unknown>>(),
}));

vi.mock('./backend', () => ({
  getCell: () => ({
    GetPlugins: async () => '["Fallout4.esm"]',
    SearchCells: async () => '[]',
    SearchWorldspaces: async () => '[]',
    GetCellProperties: async () => '{"values":{},"descriptors":[]}',
    GetCellEditHistory: bridge.history,
  }),
  getBackend: () => ({
    SavePlugin: async () => 'Saved',
    GetActivePlugins: bridge.active,
    CreatePlugin: bridge.createPlugin,
  }),
}));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ pickPlugin: bridge.pick }) }));

const FALLOUT = { Name: 'Fallout4.esm', Kind: 'master', LoadOrder: 0, Editable: false };

beforeEach(() => {
  vi.resetAllMocks();
  localStorage.clear();
  bridge.active.mockResolvedValue(JSON.stringify([FALLOUT]));
  bridge.createPlugin.mockResolvedValue("Created new plugin 'ConflictPatch.esp' in MO2 overwrite.");
  bridge.history.mockResolvedValue('{"undoCount":0,"redoCount":0}');
  bridge.pick.mockResolvedValue({ target: 'ConflictPatch.esp', extra: '' });
});

afterEach(() => { cleanup(); localStorage.clear(); });

describe('Cell Viewer patch plugin choice', () => {
  it('creates a new patch plugin before it becomes the chosen patch, then refreshes edit counts', async () => {
    render(<CellPanel onClose={() => {}} />);
    fireEvent.click(screen.getByRole('button', { name: /^Choose$/ }));

    expect(await screen.findByText('ConflictPatch.esp')).toBeTruthy();
    expect(bridge.createPlugin).toHaveBeenCalledExactlyOnceWith('ConflictPatch.esp');
    expect(localStorage.getItem('cell.gizmoPatch')).toBe('ConflictPatch.esp');
    await waitFor(() => expect(bridge.history).toHaveBeenCalledWith('ConflictPatch.esp'));
    expect(bridge.createPlugin.mock.invocationCallOrder[0]).toBeLessThan(bridge.history.mock.invocationCallOrder[0]);
  });

  it('does not create or keep the patch when creation fails, and shows the error', async () => {
    bridge.createPlugin.mockRejectedValue(new Error('NewPluginRootUnavailable: No trusted MO2 overwrite directory is configured.'));
    render(<CellPanel onClose={() => {}} />);
    fireEvent.click(screen.getByRole('button', { name: /^Choose$/ }));

    const alert = await screen.findByRole('alert');
    expect(alert.textContent).toContain('Could not create ConflictPatch.esp');
    expect(screen.getByText('None chosen')).toBeTruthy();
    expect(localStorage.getItem('cell.gizmoPatch')).toBeNull();
    expect(bridge.history).not.toHaveBeenCalled();
  });

  it('clears a stored patch that is no longer loaded instead of querying it', async () => {
    localStorage.setItem('cell.gizmoPatch', 'Gone.esp');
    render(<CellPanel onClose={() => {}} />);

    expect(await screen.findByText('None chosen')).toBeTruthy();
    expect(localStorage.getItem('cell.gizmoPatch')).toBeNull();
    expect(bridge.history).not.toHaveBeenCalled();
  });

  it('keeps a stored patch that is still loaded and refreshes its edit counts', async () => {
    localStorage.setItem('cell.gizmoPatch', 'Mine.esp');
    bridge.active.mockResolvedValue(JSON.stringify([FALLOUT, { Name: 'Mine.esp', Kind: 'plugin', LoadOrder: 1, Editable: true }]));
    render(<CellPanel onClose={() => {}} />);

    expect(await screen.findByText('Mine.esp')).toBeTruthy();
    await waitFor(() => expect(bridge.history).toHaveBeenCalledWith('Mine.esp'));
    expect(localStorage.getItem('cell.gizmoPatch')).toBe('Mine.esp');
  });
});
