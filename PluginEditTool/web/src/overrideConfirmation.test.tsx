import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { Backend, ConflictMatrix } from './backend';
import { DialogProvider } from './dialogs';
import RecordView from './RecordView';
import MainShell from './MainShell';

const pickPlugin = vi.hoisted(() => vi.fn());
vi.mock('./useDialogs', async importOriginal => {
  const original = await importOriginal<typeof import('./useDialogs')>();
  return { ...original, useDialogs: () => ({ ...original.useDialogs(), pickPlugin }) };
});

const matrix: ConflictMatrix = {
  FormKey: '000800:Master.esm', EditorID: 'TestWeapon', Type: 'WEAP',
  Winner: 'Later.esp', Plugins: ['Master.esm', 'Later.esp'], Rows: [], Level: 'conflict',
};
const warning = 'LOSES: Patch.esp loads before Later.esp. The copied override will not win.';
const copy = vi.fn<Backend['CopyAsOverride']>();
const batch = vi.fn<Backend['CopyAsOverrideMany']>();
const reload = vi.fn(async () => {});
const losingBatch = JSON.stringify({
  total: 1, ok: 0, failed: 0, requiresLoadOrderConfirmation: true,
  losing: [{ formKey: matrix.FormKey, loadsAfter: ['Later.esp'] }],
});

beforeEach(() => {
  vi.clearAllMocks();
  copy.mockReset().mockResolvedValue(warning);
  batch.mockReset().mockResolvedValue(losingBatch);
  localStorage.clear();
  pickPlugin.mockResolvedValue({ target: 'Patch.esp', extra: '' });
  vi.stubGlobal('chrome', { webview: { addEventListener: vi.fn(), removeEventListener: vi.fn(), hostObjects: { backend: {
    CopyAsOverride: copy, CopyAsOverrideMany: batch,
    GetConflicts: vi.fn(async () => JSON.stringify([{ ...matrix, Count: 2 }])),
    GetConflictMatrix: vi.fn(async () => JSON.stringify(matrix)),
    GetEditablePlugins: vi.fn(async () => '["Patch.esp"]'),
    GetContainmentPath: vi.fn(async () => '[]'),
    GetProblems: vi.fn(async () => '[]'),
    GetReferencedBy: vi.fn(async () => '[]'),
    GetDependencies: vi.fn(async () => '[]'),
    GetHistory: vi.fn(async () => '[]'),
  } } } });
});

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

async function openRecordCopy() {
  render(<DialogProvider><RecordView matrix={matrix} plugin="Later.esp" onReload={reload} /></DialogProvider>);
  fireEvent.click(screen.getByRole('button', { name: /Actions/ }));
  fireEvent.click(screen.getByRole('button', { name: /Copy as override into/ }));
  await screen.findByText('Override will not win');
}

async function openBatchCopy() {
  localStorage.setItem('treeMode', 'conflicts');
  render(<DialogProvider><MainShell /></DialogProvider>);
  fireEvent.click(await screen.findByRole('checkbox'));
  fireEvent.click(screen.getByRole('button', { name: /Copy selected/ }));
}

describe('losing override confirmation', () => {
  it('cancels a record copy without retrying or reloading', async () => {
    await openRecordCopy();
    expect(copy).toHaveBeenCalledWith('Later.esp', matrix.FormKey, 'Patch.esp', false);
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    await screen.findByText('Copy cancelled -- Patch.esp was left unchanged.');
    expect(copy).toHaveBeenCalledTimes(1);
    expect(reload).not.toHaveBeenCalled();
  });

  it('keeps the losing opt-in through a subsequent overwrite confirmation', async () => {
    copy.mockResolvedValueOnce(warning)
      .mockResolvedValueOnce('EXISTS: Patch.esp already contains the record.')
      .mockResolvedValueOnce('Replaced record.');
    await openRecordCopy();
    fireEvent.click(screen.getByRole('button', { name: 'Copy anyway' }));
    await screen.findByText('Record already exists in target plugin');
    expect(copy).toHaveBeenNthCalledWith(2, 'Later.esp', matrix.FormKey, 'Patch.esp', false, true);
    fireEvent.click(screen.getByRole('button', { name: 'Overwrite' }));
    await waitFor(() => expect(reload).toHaveBeenCalledTimes(1));
    expect(copy).toHaveBeenNthCalledWith(3, 'Later.esp', matrix.FormKey, 'Patch.esp', true, true);
  });

  it('cancels overwrite after accepting the losing warning without writing', async () => {
    copy.mockResolvedValueOnce(warning).mockResolvedValueOnce('EXISTS: Record already exists.');
    await openRecordCopy();
    fireEvent.click(screen.getByRole('button', { name: 'Copy anyway' }));
    await screen.findByText('Record already exists in target plugin');
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    await screen.findByText('Copy cancelled -- the existing override was left alone.');
    expect(copy).toHaveBeenCalledTimes(2);
    expect(reload).not.toHaveBeenCalled();
  });

  it('cancels a batch without retrying', async () => {
    await openBatchCopy();
    await screen.findByText('Overrides will not win');
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    await screen.findByText('Batch copy cancelled -- Patch.esp was left unchanged.');
    expect(batch).toHaveBeenCalledTimes(1);
    expect(batch.mock.calls[0].slice(1)).toEqual(['Patch.esp', false, false]);
  });

  it('keeps batch overwrite consent through the losing confirmation', async () => {
    batch.mockResolvedValueOnce(JSON.stringify({
      total: 1, requiresOverwrite: true, existing: [{ formKey: matrix.FormKey }],
    })).mockResolvedValueOnce(losingBatch)
      .mockResolvedValueOnce(JSON.stringify({ total: 1, ok: 1, failed: 0 }));
    await openBatchCopy();
    await screen.findByText('Overwrite existing overrides?');
    fireEvent.click(screen.getByRole('button', { name: 'Overwrite and copy' }));
    await screen.findByText('Overrides will not win');
    expect(batch.mock.calls[1].slice(1)).toEqual(['Patch.esp', true, false]);
    fireEvent.click(screen.getByRole('button', { name: 'Copy anyway' }));
    await screen.findByText(/Copied 1\/1 into Patch.esp/);
    expect(batch).toHaveBeenCalledTimes(3);
    expect(batch.mock.calls[2].slice(1)).toEqual(['Patch.esp', true, true]);
    expect(JSON.parse(batch.mock.calls[2][0])).toEqual([{ formKey: matrix.FormKey, source: 'Later.esp' }]);
  });

  it('clears the batch error when a later batch succeeds', async () => {
    batch.mockResolvedValueOnce(JSON.stringify({
      total: 1, ok: 0, failed: 1, failures: [{ formKey: matrix.FormKey, reason: 'locked' }],
    })).mockResolvedValueOnce(JSON.stringify({ total: 1, ok: 1, failed: 0 }));
    await openBatchCopy();
    await screen.findByText(/1 of 1 records could not be copied into Patch.esp/);
    fireEvent.click(screen.getByRole('button', { name: /Copy selected/ }));
    await screen.findByText(/Copied 1\/1 into Patch.esp/);
    expect(screen.queryByText(/could not be copied/)).toBeNull();
  });
});

describe('deep copy losing override confirmation', () => {
  const row = {
    Field: 'FULL', DisplayLabel: 'Name', Level: 0, Values: ['DeepOld', 'DeepNew'], Differs: true,
    Statuses: [], Severity: 'conflict', Kind: 'Value' as const, Group: '', GroupLabel: '', IsSummary: false,
    HasChildren: false, EditKind: 'Text' as const, EnumOptions: null, RefType: null, RefTypes: null,
  };
  const deep = vi.fn<Backend['DeepCopyAsOverride']>();

  async function openDeepCopy() {
    const hostObjects = (window as unknown as { chrome: { webview: { hostObjects: { backend: Record<string, unknown> } } } })
      .chrome.webview.hostObjects.backend;
    hostObjects.DeepCopyAsOverride = deep;
    hostObjects.DescribeElement = vi.fn(async () => '{}');
    deep.mockReset()
      .mockResolvedValueOnce('DRY RUN: deep copy would bring 2 record(s) into Patch.esp.')
      .mockResolvedValueOnce('LOSES: Patch.esp loads before Later.esp.')
      .mockResolvedValueOnce('EXISTS: Patch.esp already contains 1 of the 2 record(s).')
      .mockResolvedValueOnce('Deep-copied 2 of 2 record(s).');
    render(<DialogProvider>
      <RecordView matrix={{ ...matrix, Rows: [row] }} plugin="Later.esp" onReload={reload} />
    </DialogProvider>);
    fireEvent.contextMenu(await screen.findByText('DeepNew'));
    fireEvent.click(await screen.findByRole('button', { name: /Deep copy as override into/ }));
    fireEvent.click(await screen.findByRole('button', { name: 'Proceed' }));
    await screen.findByText('Overrides will not win');
  }

  it('cancels without retrying the apply', async () => {
    await openDeepCopy();
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    await screen.findByText('Deep copy cancelled -- the target plugin was left unchanged.');
    expect(deep).toHaveBeenCalledTimes(2);
    expect(deep).toHaveBeenLastCalledWith('Later.esp', matrix.FormKey, 'Patch.esp', true, false);
  });

  it('keeps load-order consent through the overwrite prompt', async () => {
    await openDeepCopy();
    fireEvent.click(screen.getByRole('button', { name: 'Copy anyway' }));
    fireEvent.click(await screen.findByRole('button', { name: 'Overwrite all and copy' }));
    await screen.findByText('Deep-copied 2 of 2 record(s).');
    expect(deep).toHaveBeenNthCalledWith(3, 'Later.esp', matrix.FormKey, 'Patch.esp', true, false, 200, true);
    expect(deep).toHaveBeenNthCalledWith(4, 'Later.esp', matrix.FormKey, 'Patch.esp', true, true, 200, true);
  });
});
