import { act, renderHook } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from './backend';
import { useRecordActions } from './recordActions';

const dialogs = vi.hoisted(() => ({ prompt: vi.fn(), pickPlugin: vi.fn() }));

vi.mock('./useDialogs', () => ({
  useDialogs: () => ({ confirm: vi.fn(), prompt: dialogs.prompt, pickPlugin: dialogs.pickPlugin, compare: vi.fn() }),
}));

const matrix: ConflictMatrix = {
  FormKey: '000800:Patch.esp', EditorID: 'TestList', Type: 'LVLI', Winner: 'Patch.esp',
  Plugins: ['Patch.esp'], Rows: [], Level: '',
};

const CopyAsWrapper = vi.fn(async () => 'Copied 000800:Patch.esp as wrapper in Target.esp.');
const onReload = vi.fn(async () => {});
const setStatus = vi.fn();
const closeMenu = vi.fn();

function renderActions() {
  return renderHook(() => useRecordActions({
    matrix, editablePlugins: ['Target.esp'], closeMenu, setStatus, onReload,
  })).result;
}

beforeEach(() => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { CopyAsWrapper } } } });
  dialogs.pickPlugin.mockResolvedValue({ target: 'Target.esp', extra: '' });
});

afterEach(() => {
  vi.unstubAllGlobals();
  vi.clearAllMocks();
});

it('sends the source, FormKey, target and trimmed EditorID to CopyAsWrapper', async () => {
  dialogs.prompt.mockResolvedValue('  WrapList ');
  const result = renderActions();

  await act(async () => { await result.current.copyAsWrapperInto(0); });

  expect(CopyAsWrapper).toHaveBeenCalledWith('Patch.esp', '000800:Patch.esp', 'Target.esp', 'WrapList');
  expect(onReload).toHaveBeenCalled();
});

it('refuses a wrapped EditorID equal to the source EditorID, ignoring case', async () => {
  dialogs.prompt.mockResolvedValue(null);
  const result = renderActions();

  await act(async () => { await result.current.copyAsWrapperInto(0); });

  const { validate } = dialogs.prompt.mock.calls[0][0];
  expect(validate('testlist')).toContain('different EditorID');
  expect(validate('  TESTLIST ')).toContain('different EditorID');
  expect(validate('WrapList')).toBeNull();
  expect(validate('')).toBeNull();
  expect(CopyAsWrapper).not.toHaveBeenCalled();
});

it('reports a LOSES wrapper result as not copied', async () => {
  dialogs.prompt.mockResolvedValue('WrapList');
  CopyAsWrapper.mockResolvedValueOnce('LOSES: Target.esp loads before Other.esp, which also override 000800:Patch.esp.');
  const result = renderActions();

  await act(async () => { await result.current.copyAsWrapperInto(0); });

  expect(setStatus).toHaveBeenLastCalledWith('Not copied -- Target.esp loads before Other.esp, which also override 000800:Patch.esp.');
});
