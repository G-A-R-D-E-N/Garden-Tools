import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import type { Backend, SearchHit } from './backend';
import { DialogProvider } from './dialogs';
import MainShell from './MainShell';

const pickPlugin = vi.hoisted(() => vi.fn());
vi.mock('./useDialogs', async importOriginal => {
  const original = await importOriginal<typeof import('./useDialogs')>();
  return { ...original, useDialogs: () => ({ ...original.useDialogs(), pickPlugin }) };
});

const SHARED = '000800:Master.esm';
const OTHER = '000801:Master.esm';
const hit = (Plugin: string, FormKey: string, EditorID: string): SearchHit => ({ FormKey, EditorID, Type: 'WEAP', Plugin });
const fromA = hit('A.esp', SHARED, 'SharedWeapon');
const fromB = hit('B.esp', SHARED, 'SharedWeapon');
const otherC = hit('C.esp', OTHER, 'OtherWeapon');

const searchResults: Record<string, SearchHit[]> = { shared: [fromA], later: [fromB, otherC], twins: [fromA, fromB, otherC] };
const batch = vi.fn<Backend['CopyAsOverrideMany']>();
const getMatrix = vi.fn<(key: string) => Promise<string>>();

beforeEach(() => {
  localStorage.clear();
  pickPlugin.mockResolvedValue({ target: 'Patch.esp', extra: '' });
  batch.mockReset().mockResolvedValue(JSON.stringify({ total: 2, ok: 2, failed: 0 }));
  getMatrix.mockReset().mockImplementation(async key => JSON.stringify({
    FormKey: key, EditorID: 'SharedWeapon', Type: 'WEAP', Winner: 'A.esp', Plugins: ['A.esp', 'B.esp'], Rows: [], Level: 'conflict',
  }));
  vi.stubGlobal('chrome', { webview: { addEventListener: vi.fn(), removeEventListener: vi.fn(), hostObjects: {
    appInterop: {
      GetPlugins: vi.fn(async () => JSON.stringify([{ Key: 'A.esp', ConflictStatus: 0, IsRecordNode: false, HasChildren: false, FilePath: 'A.esp' }])),
    },
    backend: {
      SearchRecords: vi.fn(async (query: string) => JSON.stringify(searchResults[query] ?? [])),
      CopyAsOverrideMany: batch,
      GetConflictMatrix: getMatrix,
      GetRecordTypeIndex: vi.fn(async () => '[]'),
      GetLoadOrderSummary: vi.fn(async () => JSON.stringify({ TotalRecords: 3, PluginCount: 3, Plugins: ['A.esp', 'B.esp', 'C.esp'] })),
      GetEditablePlugins: vi.fn(async () => '[]'),
      GetContainmentPath: vi.fn(async () => '[]'),
      GetProblems: vi.fn(async () => '[]'),
      GetReferencedBy: vi.fn(async () => '[]'),
      GetDependencies: vi.fn(async () => '[]'),
      GetHistory: vi.fn(async () => '[]'),
    },
  } } });
});

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('keeps the plugin a selected record was picked from when a later search shows the same FormKey', async () => {
  render(<DialogProvider><MainShell /></DialogProvider>);
  const input = await screen.findByPlaceholderText('Search plugins & records…');
  fireEvent.change(input, { target: { value: 'shared' } });
  fireEvent.click(await screen.findByRole('checkbox', { name: 'SharedWeapon' }));

  fireEvent.change(input, { target: { value: 'later' } });
  await screen.findByRole('checkbox', { name: 'OtherWeapon' });
  fireEvent.click(screen.getByRole('checkbox', { name: 'OtherWeapon' }));

  fireEvent.click(screen.getByRole('button', { name: /Copy selected as override/ }));
  await waitFor(() => expect(batch).toHaveBeenCalledWith(
    JSON.stringify([{ formKey: SHARED, source: 'A.esp' }, { formKey: OTHER, source: 'C.esp' }]),
    'Patch.esp', false, false,
  ));
}, 20000);

it('selects the same FormKey from two plugins as separate rows and disables Compare and Copy for them', async () => {
  render(<DialogProvider><MainShell /></DialogProvider>);
  const input = await screen.findByPlaceholderText('Search plugins & records…');
  fireEvent.change(input, { target: { value: 'twins' } });
  await screen.findByRole('checkbox', { name: 'OtherWeapon' });
  const [rowA, rowB] = screen.getAllByRole('checkbox', { name: 'SharedWeapon' }) as HTMLInputElement[];

  fireEvent.click(rowA);
  expect(rowA.checked).toBe(true);
  expect(rowB.checked).toBe(false);
  expect(screen.getByText('1 selected (0 hidden)')).toBeTruthy();

  fireEvent.click(rowB);
  expect(rowB.checked).toBe(true);
  expect(screen.getByText('2 selected (0 hidden)')).toBeTruthy();
  expect((screen.getByRole('button', { name: /Compare selected/ }) as HTMLButtonElement).disabled).toBe(true);
  expect((screen.getByRole('button', { name: /Copy selected as override/ }) as HTMLButtonElement).disabled).toBe(true);
  expect(getMatrix).not.toHaveBeenCalled();
  expect(batch).not.toHaveBeenCalled();
});

it('copies the plugin of the same-FormKey row that was ticked', async () => {
  render(<DialogProvider><MainShell /></DialogProvider>);
  const input = await screen.findByPlaceholderText('Search plugins & records…');
  fireEvent.change(input, { target: { value: 'twins' } });
  await screen.findByRole('checkbox', { name: 'OtherWeapon' });
  const [rowA, rowB] = screen.getAllByRole('checkbox', { name: 'SharedWeapon' }) as HTMLInputElement[];

  fireEvent.click(rowB);
  expect(rowA.checked).toBe(false);
  fireEvent.click(screen.getByRole('checkbox', { name: 'OtherWeapon' }));
  fireEvent.click(screen.getByRole('button', { name: /Copy selected as override/ }));
  await waitFor(() => expect(batch).toHaveBeenCalledWith(
    JSON.stringify([{ formKey: SHARED, source: 'B.esp' }, { formKey: OTHER, source: 'C.esp' }]),
    'Patch.esp', false, false,
  ));
});
