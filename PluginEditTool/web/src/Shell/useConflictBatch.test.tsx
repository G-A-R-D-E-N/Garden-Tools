import { act, cleanup, renderHook, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { DialogProvider } from '../dialogs';
import { useCompareSelected, useConflictBatch } from './useConflictBatch';

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

it('useCompareSelected opens in order, flags comparing while busy and resets after a failure', async () => {
  const { result } = renderHook(() => useCompareSelected());
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
