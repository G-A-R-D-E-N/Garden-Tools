import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import SpreadsheetPanel from './SpreadsheetPanel';

const grid = JSON.stringify({
  columns: ['Name'],
  rows: [{ formKey: '000800:Mod.esp', editorId: 'Npc', cells: ['Old'], editable: [true] }],
  total: 1,
  offset: 0,
});

function installBackend(setField: () => Promise<string>) {
  const backend = {
    GetActivePlugins: vi.fn(async () => JSON.stringify([{ Name: 'Mod.esp' }, { Name: 'Other.esp' }])),
    GetPluginRecordTypes: vi.fn(async () => JSON.stringify(['NPC_ (1)'])),
    GetRecordsGrid: vi.fn(async () => grid),
    SetField: vi.fn(setField),
    SavePlugin: vi.fn(async () => 'Saved Mod.esp'),
  };
  Object.assign(window, { chrome: { webview: { hostObjects: { backend } } } });
  return backend;
}

beforeEach(() => {
  localStorage.setItem('spreadsheet.plugin', 'Mod.esp');
  localStorage.setItem('spreadsheet.type', 'NPC_');
});

afterEach(() => {
  cleanup();
  localStorage.clear();
  Reflect.deleteProperty(window, 'chrome');
});

describe('SpreadsheetPanel save outcome', () => {
  it('reports a save as applied when the success message contains the word error', async () => {
    const backend = installBackend(async () => 'Updated Name for 000800:Mod.esp; no error found.');
    render(<SpreadsheetPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load'));
    await screen.findByDisplayValue('Old');
    fireEvent.change(screen.getByDisplayValue('Old'), { target: { value: 'New' } });
    fireEvent.click(screen.getByText('Save Changes (1)').closest('button')!);

    const msg = await screen.findByText(/^Applied 1 of 1 edit/);
    expect(msg.textContent).not.toContain('failed');
    expect(backend.SavePlugin).toHaveBeenCalledWith('Mod.esp', '');
  });

  it('reports a rejected SetField call as a failed edit and skips the disk save', async () => {
    const backend = installBackend(async () => {
      throw new Error('PluginReadOnly: Refusing to edit protected or game-Data plugin: Mod.esp');
    });
    render(<SpreadsheetPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load'));
    await screen.findByDisplayValue('Old');
    fireEvent.change(screen.getByDisplayValue('Old'), { target: { value: 'New' } });
    fireEvent.click(screen.getByText('Save Changes (1)').closest('button')!);

    const msg = await screen.findByText(/^Applied 0 of 1 edit/);
    expect(msg.textContent).toContain('1 failed: 000800:Mod.esp.Name: PluginReadOnly');
    expect(backend.SavePlugin).not.toHaveBeenCalled();
  });
});

describe('SpreadsheetPanel plugin selection', () => {
  it('drops the loaded grid and pending edits when another plugin is selected', async () => {
    const backend = installBackend(async () => 'Updated.');
    render(<SpreadsheetPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load'));
    await screen.findByDisplayValue('Old');
    fireEvent.change(screen.getByDisplayValue('Old'), { target: { value: 'New' } });
    expect(screen.getByText('Save Changes (1)')).toBeTruthy();

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.change(screen.getAllByRole('combobox')[0], { target: { value: 'Other.esp' } });

    expect(screen.queryByDisplayValue('New')).toBeNull();
    expect(screen.queryByDisplayValue('Old')).toBeNull();
    expect(screen.getByText('Save Changes (0)').closest('button')!.disabled).toBe(true);
    expect(backend.SetField).not.toHaveBeenCalled();
  });

  it('ignores a Load that resolves after another plugin was selected', async () => {
    const backend = installBackend(async () => 'Updated.');
    let resolveLoad: (value: string) => void = () => {};
    backend.GetRecordsGrid = vi.fn(() => new Promise<string>(resolve => { resolveLoad = resolve; }));
    render(<SpreadsheetPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load'));
    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.change(screen.getAllByRole('combobox')[0], { target: { value: 'Other.esp' } });

    await act(async () => { resolveLoad(grid); });

    expect(screen.queryByDisplayValue('Old')).toBeNull();
    expect(screen.getByText('Save Changes (0)').closest('button')!.disabled).toBe(true);
    expect(screen.getByText('Pick a plugin and a record type, then Load.')).toBeTruthy();
    expect((screen.getByText('Load').closest('button') as HTMLButtonElement).disabled).toBe(true);
  });

  it('skips the reload and banner when the plugin changes while Save is pending', async () => {
    let resolveSet: (value: string) => void = () => {};
    const backend = installBackend(() => new Promise<string>(resolve => { resolveSet = resolve; }));
    render(<SpreadsheetPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load'));
    await screen.findByDisplayValue('Old');
    fireEvent.change(screen.getByDisplayValue('Old'), { target: { value: 'New' } });
    fireEvent.click(screen.getByText('Save Changes (1)').closest('button')!);
    await screen.findByText('Saving…');

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.change(screen.getAllByRole('combobox')[0], { target: { value: 'Other.esp' } });

    await act(async () => { resolveSet('Updated.'); });

    expect(screen.queryByText(/^Applied/)).toBeNull();
    expect(screen.queryByDisplayValue('Old')).toBeNull();
    expect(screen.getByText('Pick a plugin and a record type, then Load.')).toBeTruthy();
    expect(backend.GetRecordsGrid).toHaveBeenCalledTimes(1);
    expect(screen.getByText('Save Changes (0)').closest('button')!.disabled).toBe(true);
  });
});
