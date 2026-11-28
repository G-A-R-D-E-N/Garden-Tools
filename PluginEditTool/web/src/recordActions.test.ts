import { act, renderHook } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import type { BulkTarget, ConflictFieldRow, ConflictMatrix } from './backend';
import { f2Edit, useRecordActions } from './recordActions';

const dialogs = vi.hoisted(() => ({ prompt: vi.fn(), pickPlugin: vi.fn(), confirm: vi.fn() }));

vi.mock('./useDialogs', () => ({
  useDialogs: () => ({ confirm: dialogs.confirm, prompt: dialogs.prompt, pickPlugin: dialogs.pickPlugin, compare: vi.fn() }),
}));

const matrix: ConflictMatrix = {
  FormKey: '000800:Patch.esp', EditorID: 'TestList', Type: 'LVLI', Winner: 'Patch.esp',
  Plugins: ['Patch.esp'], Rows: [], Level: '',
};

const CopyAsWrapper = vi.fn(async () => 'Copied 000800:Patch.esp as wrapper in Target.esp.');
const CopyElementToSelected = vi.fn(async () => JSON.stringify({ applied: 1, skipped: [], failures: [] }));
const RemoveElementFromSelected = vi.fn(async () => JSON.stringify({ applied: 1, skipped: [], failures: [] }));
const onReload = vi.fn(async () => {});
const InjectFormsIntoMaster = vi.fn(async (_plugin: string, _master: string, _start: string, apply: boolean) =>
  apply ? 'Injected 2 record(s) from Patch.esp into Target.esp.' : 'DRY RUN: would inject 2 of 2 record(s) of Patch.esp into Target.esp.');
const MastersList = vi.fn(async () => JSON.stringify({
  pluginName: 'Patch.esp',
  masters: [{ index: 0, name: 'Target.esp', size: 1, used: true }],
  light: false,
}));
const setStatus = vi.fn();
const closeMenu = vi.fn();

const setErrorStr = vi.fn();
const source: BulkTarget = { plugin: 'Patch.esp', formKey: '000800:Patch.esp' };
const other: BulkTarget = { plugin: 'Other.esp', formKey: '000801:Other.esp' };

function renderActions(selected: BulkTarget[] = []) {
  return renderHook(() => useRecordActions({
    matrix, editablePlugins: ['Target.esp'], closeMenu, setStatus, onReload, selected, setErrorStr,
  })).result;
}

beforeEach(() => {
  vi.stubGlobal('chrome', { webview: { hostObjects: {
    backend: { CopyAsWrapper, CopyElementToSelected, RemoveElementFromSelected, InjectFormsIntoMaster },
    masters: { List: MastersList },
  } } });
  dialogs.pickPlugin.mockResolvedValue({ target: 'Target.esp', extra: '' });
  dialogs.confirm.mockResolvedValue(true);
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

it('sends the dry run and then the applied inject with the same master and start', async () => {
  dialogs.prompt.mockResolvedValueOnce('Target.esp').mockResolvedValueOnce('000A00');
  dialogs.confirm.mockResolvedValueOnce(true);
  const result = renderActions();

  await act(async () => { await result.current.injectIntoMaster(0); });

  expect(InjectFormsIntoMaster).toHaveBeenNthCalledWith(1, 'Patch.esp', 'Target.esp', '000A00', false);
  expect(InjectFormsIntoMaster).toHaveBeenNthCalledWith(2, 'Patch.esp', 'Target.esp', '000A00', true);
  expect(setStatus).toHaveBeenLastCalledWith('Injected 2 record(s) from Patch.esp into Target.esp.');
  expect(onReload).toHaveBeenCalled();
});

it('stops after the dry run when the inject confirmation is declined', async () => {
  dialogs.prompt.mockResolvedValueOnce('Target.esp').mockResolvedValueOnce('000A00');
  dialogs.confirm.mockResolvedValueOnce(false);
  const result = renderActions();

  await act(async () => { await result.current.injectIntoMaster(0); });

  expect(InjectFormsIntoMaster).toHaveBeenCalledTimes(1);
  expect(InjectFormsIntoMaster).toHaveBeenCalledWith('Patch.esp', 'Target.esp', '000A00', false);
  expect(onReload).not.toHaveBeenCalled();
});

it('does not open the inject prompts for a plugin with no masters', async () => {
  MastersList.mockResolvedValueOnce(JSON.stringify({ pluginName: 'Patch.esp', masters: [], light: false }));
  const result = renderActions();

  await act(async () => { await result.current.injectIntoMaster(0); });

  expect(dialogs.prompt).not.toHaveBeenCalled();
  expect(InjectFormsIntoMaster).not.toHaveBeenCalled();
  expect(setStatus).toHaveBeenLastCalledWith('Patch.esp has no masters to inject into.');
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

const subrecordRow = { Field: 'Authoring\\ICON[0]', DisplayLabel: 'Icon' } as ConflictFieldRow;

it('sends the element path and every selected record except the source to CopyElementToSelected', async () => {
  const result = renderActions([source, other]);

  await act(async () => { await result.current.copyElementToSelected(0, subrecordRow); });

  expect(CopyElementToSelected).toHaveBeenCalledWith('Patch.esp', '000800:Patch.esp', 'Authoring\\ICON[0]', JSON.stringify([other]));
  expect(onReload).toHaveBeenCalledTimes(1);
});

it('sends the same targets to RemoveElementFromSelected after confirmation', async () => {
  const result = renderActions([source, other]);

  await act(async () => { await result.current.removeElementFromSelected(0, subrecordRow); });

  expect(dialogs.confirm).toHaveBeenCalled();
  expect(RemoveElementFromSelected).toHaveBeenCalledWith('Authoring\\ICON[0]', JSON.stringify([other]));
  expect(onReload).toHaveBeenCalledTimes(1);
});

it('never calls the bridge when the source is the only selected record', async () => {
  dialogs.confirm.mockResolvedValue(false);
  const result = renderActions([source]);

  await act(async () => { await result.current.copyElementToSelected(0, subrecordRow); });
  await act(async () => { await result.current.removeElementFromSelected(0, subrecordRow); });

  expect(CopyElementToSelected).not.toHaveBeenCalled();
  expect(RemoveElementFromSelected).not.toHaveBeenCalled();
});

it('lists per-record failures from a bulk copy in the error line', async () => {
  CopyElementToSelected.mockResolvedValueOnce(JSON.stringify({
    applied: 1, skipped: [], failures: [{ plugin: 'Other.esp', formKey: '000801:Other.esp', reason: 'ElementLinkNeedsMaster: x' }],
  }));
  const result = renderActions([source, other]);

  await act(async () => { await result.current.copyElementToSelected(0, subrecordRow); });

  expect(setErrorStr).toHaveBeenCalledWith(expect.stringContaining('1 of 2 records could not be copied:'));
  expect(setErrorStr).toHaveBeenCalledWith(expect.stringContaining('Other.esp 000801:Other.esp: ElementLinkNeedsMaster: x'));
});
