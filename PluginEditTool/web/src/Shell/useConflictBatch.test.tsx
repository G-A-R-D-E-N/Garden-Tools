import { act, cleanup, renderHook, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { DialogProvider } from '../dialogs';
import { useCompareSelected, useConflictBatch, useOpenSelectedInTabs } from './useConflictBatch';

const pickPlugin = vi.hoisted(() => vi.fn());
vi.mock('../useDialogs', async importOriginal => {
  const original = await importOriginal<typeof import('../useDialogs')>();
  return { ...original, useDialogs: () => ({ ...original.useDialogs(), pickPlugin }) };
});

const batch = vi.fn<(itemsJson: string, patch: string, overwrite: boolean, allowLosing?: boolean) => Promise<string>>();
const setStatus = vi.fn<(message: string) => void>();
const setErrorStr = vi.fn<(message: string) => void>();
const picked = [{ formKey: '000800:M.esm', source: 'L.esp', editorId: 'RecA' }, { formKey: '000801:M.esm', source: 'L.esp' }];

const renderBatch = () => renderHook(
  () => useConflictBatch({ activeTab: 'other', treeMode: 'files', setStatus, setErrorStr }),
  { wrapper: DialogProvider },
);
const stubBridge = () => vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { CopyAsOverrideMany: batch } } } });

beforeEach(() => {
  vi.clearAllMocks();
  pickPlugin.mockResolvedValue({ target: 'Patch.esp', extra: '' });
});

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('copyMany returns false with a no-details line when the backend reports failures without a list', async () => {
  batch.mockResolvedValue(JSON.stringify({ total: 1, ok: 0, failed: 1 }));
  stubBridge();
  const { result } = renderBatch();
  let copied = true;
  await act(async () => { copied = await result.current.copyMany([{ formKey: '000800:M.esm', source: 'L.esp' }]); });
  expect(copied).toBe(false);
  expect(setErrorStr).toHaveBeenLastCalledWith('1 of 1 records could not be copied into Patch.esp:\n(no details returned)');
  expect(setStatus).toHaveBeenLastCalledWith(expect.stringContaining('1 failed -- see the error list.'));
});

it('names a failed record by FormKey and EditorID, and falls back for missing fields', async () => {
  batch.mockResolvedValue(JSON.stringify({
    total: 2, ok: 0, failed: 2, failures: [{ formKey: '000800:M.esm' }, { reason: 'x' }],
  }));
  stubBridge();
  const { result } = renderBatch();
  let copied = true;
  await act(async () => { copied = await result.current.copyMany(picked); });
  expect(copied).toBe(false);
  expect(setErrorStr).toHaveBeenLastCalledWith(
    '2 of 2 records could not be copied into Patch.esp:\n000800:M.esm (RecA): no reason given\nunknown record: x',
  );
});

it('a cancelled target picker copies nothing', async () => {
  pickPlugin.mockResolvedValue(null);
  stubBridge();
  const { result } = renderBatch();
  let copied = true;
  await act(async () => { copied = await result.current.copyMany(picked); });
  expect(copied).toBe(false);
  expect(batch).not.toHaveBeenCalled();
  expect(result.current.conflictBusy).toBe(false);
});

it('without a bridge it returns false and never opens the picker', async () => {
  vi.stubGlobal('chrome', undefined);
  const { result } = renderBatch();
  let copied = true;
  await act(async () => { copied = await result.current.copyMany(picked); });
  expect(copied).toBe(false);
  expect(pickPlugin).not.toHaveBeenCalled();
});

it('a clean copy returns true, clears the previous error first and reports counts', async () => {
  batch.mockResolvedValue(JSON.stringify({ total: 2, ok: 2, failed: 0 }));
  stubBridge();
  const { result } = renderBatch();
  let copied = false;
  await act(async () => { copied = await result.current.copyMany(picked); });
  expect(copied).toBe(true);
  expect(setErrorStr.mock.calls[0]).toEqual(['']);
  expect(setStatus).toHaveBeenLastCalledWith(expect.stringContaining('Copied 2/2 into Patch.esp.'));
});

it('useOpenSelectedInTabs opens in order, flags comparing while busy and resets after a failure', async () => {
  const { result } = renderHook(() => useOpenSelectedInTabs());
  const gates: (() => void)[] = [];
  const open = vi.fn<(formKey: string, plugin: string) => Promise<void>>(() => new Promise<void>(resolve => { gates.push(() => resolve()); }));
  const list = [{ formKey: 'a', plugin: 'P' }, { formKey: 'b', plugin: 'P' }];
  let run: Promise<void> | undefined;
  act(() => { run = result.current.compare(list, open); });
  expect(result.current.comparing).toBe(true);
  expect(open).toHaveBeenCalledTimes(1);
  expect(open).toHaveBeenLastCalledWith('a', 'P');
  await act(async () => { gates[0](); });
  await waitFor(() => expect(open).toHaveBeenCalledTimes(2));
  expect(open).toHaveBeenLastCalledWith('b', 'P');
  expect(result.current.comparing).toBe(true);
  await act(async () => { gates[1](); await run; });
  expect(result.current.comparing).toBe(false);

  const failing = vi.fn(async () => { throw new Error('x'); });
  await expect(act(() => result.current.compare([{ formKey: 'a', plugin: 'P' }], failing))).rejects.toThrow('x');
  expect(result.current.comparing).toBe(false);
});

const matrixJson = (plugins: string[], rows: { Field: string; DisplayLabel: string; Values: string[] }[]) =>
  JSON.stringify({ Plugins: plugins, Rows: rows });

it('useCompareSelected builds a side-by-side view with one column per record and aligned rows', async () => {
  const getMatrix = vi.fn<(formKey: string) => Promise<string>>(async formKey => formKey === 'a'
    ? matrixJson(['M.esm', 'L.esp'], [{ Field: 'Name', DisplayLabel: 'Name', Values: ['Old', 'Old'] }])
    : matrixJson(['M.esm', 'L.esp'], [{ Field: 'Name', DisplayLabel: 'Name', Values: ['Old', 'New'] }]));
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetConflictMatrix: getMatrix } } } });
  const { result } = renderHook(() => useCompareSelected());
  await act(async () => {
    await result.current.compare([{ formKey: 'a', plugin: 'M.esm' }, { formKey: 'b', plugin: 'L.esp' }]);
  });
  expect(getMatrix).toHaveBeenCalledTimes(2);
  expect(result.current.view).toEqual({
    columns: [{ formKey: 'a', plugin: 'M.esm' }, { formKey: 'b', plugin: 'L.esp' }],
    rows: [{
      field: 'Name', label: 'Name', differs: true,
      cells: [{ value: 'Old', differs: true }, { value: 'New', differs: true }],
    }],
  });
  act(() => result.current.closeView());
  expect(result.current.view).toBeNull();
});

it('useCompareSelected drops a column whose plugin the matrix does not list (it used to show the first plugin values)', async () => {
  const getMatrix = vi.fn<(formKey: string) => Promise<string>>(async formKey => formKey === 'a'
    ? matrixJson(['M.esm', 'L.esp'], [{ Field: 'Name', DisplayLabel: 'Name', Values: ['Old', 'Old'] }])
    : matrixJson(['M.esm'], [{ Field: 'Name', DisplayLabel: 'Name', Values: ['New'] }]));
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetConflictMatrix: getMatrix } } } });
  const { result } = renderHook(() => useCompareSelected());
  await act(async () => {
    await result.current.compare([{ formKey: 'a', plugin: 'M.esm' }, { formKey: 'b', plugin: 'L.esp' }]);
  });
  expect(result.current.view).toEqual({
    columns: [{ formKey: 'a', plugin: 'M.esm' }],
    rows: [{ field: 'Name', label: 'Name', differs: false, cells: [{ value: 'Old', differs: false }] }],
  });
});

it('useCompareSelected flags comparing while the matrices load and resets after a failure', async () => {
  const gates: (() => void)[] = [];
  const getMatrix = vi.fn<(formKey: string) => Promise<string>>(() => new Promise<string>(resolve => {
    gates.push(() => resolve(matrixJson(['M.esm'], [])));
  }));
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetConflictMatrix: getMatrix } } } });
  const { result } = renderHook(() => useCompareSelected());
  let run: Promise<void> | undefined;
  act(() => { run = result.current.compare([{ formKey: 'a', plugin: 'M.esm' }]); });
  expect(result.current.comparing).toBe(true);
  await act(async () => { gates[0](); await run; });
  expect(result.current.comparing).toBe(false);
  expect(result.current.view).not.toBeNull();

  getMatrix.mockRejectedValueOnce(new Error('boom'));
  await expect(act(() => result.current.compare([{ formKey: 'a', plugin: 'M.esm' }]))).rejects.toThrow('boom');
  expect(result.current.comparing).toBe(false);
});
