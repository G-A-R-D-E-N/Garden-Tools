import { act, renderHook } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from './backend';
import { f2Edit, useRecordActions } from './recordActions';

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
  expect(validate('testlist')).toContain('an EditorID different from TestList');
  expect(validate('  TESTLIST ')).toContain('an EditorID different from TestList');
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

it('sends null when the wrapped EditorID is blank', async () => {
  dialogs.prompt.mockResolvedValue('   ');
  const result = renderActions();

  await act(async () => { await result.current.copyAsWrapperInto(0); });

  expect(CopyAsWrapper).toHaveBeenCalledWith('Patch.esp', '000800:Patch.esp', 'Target.esp', null);
});

it('does nothing when the target picker is cancelled', async () => {
  dialogs.pickPlugin.mockResolvedValue(null);
  const result = renderActions();

  await act(async () => { await result.current.copyAsWrapperInto(0); });

  expect(dialogs.prompt).not.toHaveBeenCalled();
  expect(CopyAsWrapper).not.toHaveBeenCalled();
  expect(onReload).not.toHaveBeenCalled();
});

it('does nothing when the EditorID prompt is cancelled', async () => {
  dialogs.prompt.mockResolvedValue(null);
  const result = renderActions();

  await act(async () => { await result.current.copyAsWrapperInto(0); });

  expect(CopyAsWrapper).not.toHaveBeenCalled();
  expect(onReload).not.toHaveBeenCalled();
  expect(setStatus).not.toHaveBeenCalledWith('Creating wrapper…');
});

it('reports a backend failure and does not reload', async () => {
  dialogs.prompt.mockResolvedValue('W');
  CopyAsWrapper.mockRejectedValueOnce(new Error('disk full'));
  const result = renderActions();

  await act(async () => { await result.current.copyAsWrapperInto(0); });

  expect(setStatus).toHaveBeenLastCalledWith('Copy failed: disk full');
  expect(onReload).not.toHaveBeenCalled();
});

it('checkForErrors includes the detail in parentheses only when present', async () => {
  const CheckForErrors = vi.fn(async () => JSON.stringify([
    { Code: 'c1', Detail: 'd1', Severity: 'error', FormKey: '000800:Patch.esp', Message: 'm1' },
    { Code: 'c2', Detail: '', Severity: 'warning', FormKey: '000801:Patch.esp', Message: 'm2' },
  ]));
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { CopyAsWrapper, CheckForErrors } } } });
  const result = renderActions();

  await act(async () => { await result.current.checkForErrors(0); });

  const status = String(setStatus.mock.lastCall?.[0]);
  expect(status).toContain('error c1 (d1) 000800:Patch.esp: m1');
  expect(status).toContain('warning c2 000801:Patch.esp: m2');
  expect(status).toContain('1 error(s), 1 warning(s) in Patch.esp');
});

it('f2Edit fires only for F2 on the element itself', () => {
  const edit = vi.fn();
  const p = f2Edit(edit);
  const el = {};
  const preventDefault = vi.fn();

  p.onKeyDown({ key: 'F2', target: el, currentTarget: el, preventDefault } as never);
  expect(edit).toHaveBeenCalledOnce();
  expect(preventDefault).toHaveBeenCalledOnce();

  p.onKeyDown({ key: 'F2', target: {}, currentTarget: el, preventDefault } as never);
  p.onKeyDown({ key: 'Enter', target: el, currentTarget: el, preventDefault } as never);
  expect(edit).toHaveBeenCalledOnce();
  expect(preventDefault).toHaveBeenCalledOnce();
  expect(p.tabIndex).toBe(-1);
});
