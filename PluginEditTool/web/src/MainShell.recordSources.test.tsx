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

const searchResults: Record<string, SearchHit[]> = { shared: [fromA], later: [fromB, otherC] };
const batch = vi.fn<Backend['CopyAsOverrideMany']>();

beforeEach(() => {
  localStorage.clear();
  pickPlugin.mockResolvedValue({ target: 'Patch.esp', extra: '' });
  batch.mockReset().mockResolvedValue(JSON.stringify({ total: 2, ok: 2, failed: 0 }));
  vi.stubGlobal('chrome', { webview: { addEventListener: vi.fn(), removeEventListener: vi.fn(), hostObjects: {
    appInterop: {
      GetPlugins: vi.fn(async () => JSON.stringify([{ Key: 'A.esp', ConflictStatus: 0, IsRecordNode: false, HasChildren: false, FilePath: 'A.esp' }])),
    },
    backend: {
      SearchRecords: vi.fn(async (query: string) => JSON.stringify(searchResults[query] ?? [])),
      CopyAsOverrideMany: batch,
      GetRecordTypeIndex: vi.fn(async () => '[]'),
      GetLoadOrderSummary: vi.fn(async () => JSON.stringify({ TotalRecords: 3, PluginCount: 3, Plugins: ['A.esp', 'B.esp', 'C.esp'] })),
      GetEditablePlugins: vi.fn(async () => '[]'),
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
});
