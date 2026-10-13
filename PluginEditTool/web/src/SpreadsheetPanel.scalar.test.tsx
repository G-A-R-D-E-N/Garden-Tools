import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { DialogProvider } from './dialogs';
import SpreadsheetPanel from './SpreadsheetPanel';

const grid = JSON.stringify({
  columns: ['FACT\\CrimeArrest', 'FACT\\Flags'],
  rows: [{ formKey: '000800:Mod.esp', editorId: 'Fact', cells: ['1', '3'], editable: [true, true] }],
  total: 1,
  offset: 0,
  presentations: {
    'FACT\\CrimeArrest': { kind: 'enum', names: [{ value: 0, name: 'False' }, { value: 1, name: 'True' }] },
    'FACT\\Flags': { kind: 'flags', names: [{ bit: 0, name: 'Hidden From NPC' }, { bit: 1, name: 'Special Combat' }] },
  },
});

const SetField = vi.fn(async () => 'Updated.');
const GetRecordsGrid = vi.fn(async () => grid);

beforeEach(() => {
  SetField.mockClear();
  GetRecordsGrid.mockClear();
  localStorage.setItem('spreadsheet.plugin', 'Mod.esp');
  localStorage.setItem('spreadsheet.type', 'FACT');
  Object.assign(window, { chrome: { webview: { hostObjects: { backend: {
    GetActivePlugins: vi.fn(async () => JSON.stringify([{ Name: 'Mod.esp' }])),
    GetPluginRecordTypes: vi.fn(async () => JSON.stringify(['FACT (1)'])),
    GetRecordsGrid, SetField, SavePlugin: vi.fn(async () => 'Saved Mod.esp'),
  } } } } });
});

afterEach(() => {
  cleanup();
  localStorage.clear();
  Reflect.deleteProperty(window, 'chrome');
});

it('shows enum and flag names in the grid cells', async () => {
  render(<DialogProvider><SpreadsheetPanel onClose={() => {}} /></DialogProvider>);
  fireEvent.click(screen.getByText('Load'));
  expect(await screen.findByDisplayValue('True')).toBeTruthy();
  expect(screen.getByText('Hidden From NPC, Special Combat').closest('details')!.getAttribute('title')).toBe('3');
});

it('writes the picked enum integer through SetField on save', async () => {
  render(<DialogProvider><SpreadsheetPanel onClose={() => {}} /></DialogProvider>);
  fireEvent.click(screen.getByText('Load'));
  fireEvent.change(await screen.findByDisplayValue('True'), { target: { value: '0' } });
  fireEvent.click(screen.getByText('Save Changes (1)').closest('button')!);
  await vi.waitFor(() => expect(SetField).toHaveBeenCalledWith('Mod.esp', '000800:Mod.esp', 'FACT\\CrimeArrest', '0'));
});

it('writes the combined flag integer from the checkbox list', async () => {
  render(<DialogProvider><SpreadsheetPanel onClose={() => {}} /></DialogProvider>);
  fireEvent.click(screen.getByText('Load'));
  await screen.findByDisplayValue('True');
  fireEvent.click(screen.getByLabelText('Special Combat'));
  fireEvent.click(screen.getByText('Save Changes (1)').closest('button')!);
  await vi.waitFor(() => expect(SetField).toHaveBeenCalledWith('Mod.esp', '000800:Mod.esp', 'FACT\\Flags', '1'));
});
