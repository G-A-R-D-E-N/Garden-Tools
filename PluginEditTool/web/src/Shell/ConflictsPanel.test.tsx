import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { Backend, ConflictEntry, ConflictMatrix } from '../backend';
import { DialogProvider } from '../dialogs';
import MainShell from '../MainShell';

const pickPlugin = vi.hoisted(() => vi.fn());
vi.mock('../useDialogs', async importOriginal => {
  const original = await importOriginal<typeof import('../useDialogs')>();
  return { ...original, useDialogs: () => ({ ...original.useDialogs(), pickPlugin }) };
});

const entry = (FormKey: string, EditorID: string): ConflictEntry => ({
  FormKey, EditorID, Type: 'WEAP', Plugins: ['Master.esm', 'Later.esp'], Winner: 'Later.esp', InvolvesMod: false,
});
const all = [entry('000801:Master.esm', 'RecA'), entry('000802:Master.esm', 'RecB'),
  entry('000803:Master.esm', 'RecC'), entry('000804:Master.esm', 'RecD')];
const matrix = (FormKey: string, EditorID: string): ConflictMatrix => ({
  FormKey, EditorID, Type: 'WEAP', Winner: 'Later.esp', Plugins: ['Master.esm', 'Later.esp'], Rows: [], Level: 'conflict',
});

const getConflicts = vi.fn<Backend['GetConflicts']>();
const getMatrix = vi.fn<Backend['GetConflictMatrix']>();
const batch = vi.fn<Backend['CopyAsOverrideMany']>();

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
  pickPlugin.mockResolvedValue({ target: 'Patch.esp', extra: '' });
  getConflicts.mockReset().mockResolvedValue(JSON.stringify(all));
  getMatrix.mockReset().mockImplementation(async key => JSON.stringify(matrix(key, all.find(c => c.FormKey === key)?.EditorID ?? '')));
  batch.mockReset().mockResolvedValue(JSON.stringify({ total: 2, ok: 2, failed: 0 }));
  vi.stubGlobal('chrome', { webview: { addEventListener: vi.fn(), removeEventListener: vi.fn(), hostObjects: { backend: {
    CopyAsOverrideMany: batch,
    GetConflicts: getConflicts,
    GetConflictMatrix: getMatrix,
    GetEditablePlugins: vi.fn(async () => '["Patch.esp"]'),
    GetContainmentPath: vi.fn(async () => '[]'),
    GetProblems: vi.fn(async () => '[]'),
    GetReferencedBy: vi.fn(async () => '[]'),
    GetDependencies: vi.fn(async () => '[]'),
    GetHistory: vi.fn(async () => '[]'),
  } } } });
});

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

async function openConflicts() {
  localStorage.setItem('treeMode', 'conflicts');
  render(<DialogProvider><MainShell /></DialogProvider>);
  await screen.findByRole('checkbox', { name: 'RecA' });
}

const list = () => document.querySelector('.conflicts-list') as HTMLElement;
const row = (name: string) => screen.getByText(name).closest('.conflict-row') as HTMLElement;
const button = (name: RegExp) => screen.getByRole('button', { name }) as HTMLButtonElement;
const selected = (name: string) => row(name).classList.contains('selected');

describe('conflict list selection', () => {
  it('ctrl-click toggles one row and a plain click selects only that row', async () => {
    await openConflicts();
    fireEvent.click(row('RecA'), { ctrlKey: true });
    fireEvent.click(row('RecC'), { ctrlKey: true });
    expect(selected('RecA') && selected('RecC') && !selected('RecB')).toBe(true);
    expect(screen.getByText('2 selected')).toBeTruthy();
    fireEvent.click(row('RecA'), { ctrlKey: true });
    expect(selected('RecA')).toBe(false);
    fireEvent.click(row('RecB'));
    expect(selected('RecB') && !selected('RecC')).toBe(true);
    expect(screen.getByText('1 selected')).toBeTruthy();
  });

  it('shift-click selects the range from the last anchor', async () => {
    await openConflicts();
    fireEvent.click(row('RecA'));
    fireEvent.click(row('RecC'), { shiftKey: true });
    expect(['RecA', 'RecB', 'RecC'].every(selected) && !selected('RecD')).toBe(true);
    expect(screen.getByText('3 selected')).toBeTruthy();
  });

  it('a checkbox click toggles without clearing the other selected rows', async () => {
    await openConflicts();
    fireEvent.click(row('RecA'));
    fireEvent.click(row('RecB').querySelector('input') as HTMLInputElement);
    expect(selected('RecA') && selected('RecB')).toBe(true);
  });

  it('Escape clears the selection', async () => {
    await openConflicts();
    fireEvent.click(row('RecA'), { ctrlKey: true });
    fireEvent.click(row('RecB'), { ctrlKey: true });
    fireEvent.keyDown(list(), { key: 'Escape' });
    expect(screen.getByText('0 selected')).toBeTruthy();
    expect(['RecA', 'RecB'].some(selected)).toBe(false);
  });

  it('keeps the selection until an explicit Rescan drops the keys that disappeared', async () => {
    await openConflicts();
    fireEvent.click(row('RecA'), { ctrlKey: true });
    fireEvent.click(row('RecB'), { ctrlKey: true });
    getConflicts.mockResolvedValueOnce(JSON.stringify([all[0], all[2]]));
    fireEvent.click(button(/Rescan/));
    expect(await screen.findByText('1 selected')).toBeTruthy();
    expect(selected('RecA')).toBe(true);
  });

  it('copies the selected records through CopyAsOverrideMany and clears the selection on success', async () => {
    await openConflicts();
    fireEvent.click(row('RecC'));
    fireEvent.click(row('RecA'), { ctrlKey: true });
    fireEvent.click(button(/Copy selected/));
    await waitFor(() => expect(batch).toHaveBeenCalledWith(
      JSON.stringify([{ formKey: '000801:Master.esm', source: 'Later.esp' }, { formKey: '000803:Master.esm', source: 'Later.esp' }]),
      'Patch.esp', false, false,
    ));
    expect(await screen.findByText(/Copied 2\/2 into Patch.esp/)).toBeTruthy();
    expect(screen.getByText('0 selected')).toBeTruthy();
  });

  it('disables copy with a tooltip when nothing is selected', async () => {
    await openConflicts();
    expect(button(/Copy selected/).disabled).toBe(true);
    expect(button(/Copy selected/).title).toBe('Select at least one record to copy');
    fireEvent.click(row('RecA'));
    expect(button(/Copy selected/).disabled).toBe(false);
    expect(button(/Copy selected/).title).toBe('');
  });

  it('enables Compare Selected only for exactly two records and opens both in order', async () => {
    await openConflicts();
    expect(button(/Compare Selected/).disabled).toBe(true);
    expect(button(/Compare Selected/).title).toBe('Select exactly two records to compare');
    fireEvent.click(row('RecA'));
    expect(button(/Compare Selected/).disabled).toBe(true);
    fireEvent.click(row('RecC'), { ctrlKey: true });
    expect(button(/Compare Selected/).disabled).toBe(false);
    fireEvent.click(row('RecD'), { ctrlKey: true });
    expect(button(/Compare Selected/).disabled).toBe(true);
    fireEvent.click(row('RecD'), { ctrlKey: true });
    fireEvent.click(button(/Compare Selected/));
    await waitFor(() => expect(getMatrix).toHaveBeenCalledWith('000803:Master.esm'));
    const opened = getMatrix.mock.calls.map(([key]) => key);
    expect(opened.indexOf('000801:Master.esm')).toBeGreaterThanOrEqual(0);
    expect(opened.indexOf('000801:Master.esm')).toBeLessThan(opened.indexOf('000803:Master.esm'));
    expect(await screen.findByText('RecA', { selector: '.editor-tab-label' })).toBeTruthy();
    expect(await screen.findByText('RecC', { selector: '.editor-tab-label' })).toBeTruthy();
    expect(screen.queryByText('RecD', { selector: '.editor-tab-label' })).toBeNull();
  });

  it('drops the conflict list and its selection when the load order reloads', async () => {
    const summaries: Array<(raw: string) => void> = [];
    const summary = (plugins: string[]) => JSON.stringify({ TotalRecords: plugins.length, PluginCount: plugins.length, Plugins: plugins });
    vi.stubGlobal('chrome', { webview: { addEventListener: vi.fn(), removeEventListener: vi.fn(), hostObjects: {
      backend: {
        GetConflicts: getConflicts,
        GetRecordTypeIndex: vi.fn(async () => '[]'),
        GetLoadOrderSummary: vi.fn(() => new Promise<string>(resolve => { summaries.push(resolve); })),
      },
      appInterop: { GetPlugins: vi.fn(async () => '[]'), LoadEnvironment: vi.fn(async () => {}) },
    } } });
    await openConflicts();
    await waitFor(() => expect(summaries).toHaveLength(1));
    await act(async () => { summaries[0](summary(['Master.esm', 'Later.esp'])); });
    fireEvent.click(row('RecA'));
    expect(selected('RecA')).toBe(true);
    getConflicts.mockResolvedValueOnce(JSON.stringify([all[1]]));
    fireEvent.click(button(/Load Env/));
    await waitFor(() => expect(summaries).toHaveLength(2));
    await act(async () => { summaries[1](summary(['Master.esm'])); });
    expect(await screen.findByRole('checkbox', { name: 'RecB' })).toBeTruthy();
    expect(screen.queryByRole('checkbox', { name: 'RecA' })).toBeNull();
    expect(screen.getByText('0 selected')).toBeTruthy();
  });
});
