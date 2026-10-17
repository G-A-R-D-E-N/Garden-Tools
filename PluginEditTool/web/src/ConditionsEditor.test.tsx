import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import ConditionsEditor from './ConditionsEditor';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('keeps native row metadata through edit, reorder and browser JSON transport', async () => {
  const first = { function: 'GetScale', operator: 'Unknown (6)', value: -0, runOn: 'Subject',
    _native: { ctda: [1, 2, 3], cis1: 'first', cis2: null } };
  const second = { ...first, operator: '==', value: 1,
    _native: { ctda: [4, 5, 6], cis1: 'second', cis2: null } };
  const set = vi.fn<(...args: string[]) => Promise<string>>(async () => 'staged');
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetConditionsAt: vi.fn(async () => JSON.stringify([first, second])),
    GetConditionFunctions: vi.fn(async () => '["GetScale"]'),
    GetConditionRunOnTypes: vi.fn(async () => '["Subject"]'),
    GetConditionFunctionParams: vi.fn(async () => '{}'),
    SetConditionsAt: set,
  } } } });
  const { container } = render(<ConditionsEditor plugin="Patch.esp" record="000800:Patch.esp"
    path="Conditions" label="Test" onClose={() => {}} onSaved={() => {}} />);
  await screen.findByText('Unknown (6)');
  const inputs = container.querySelectorAll<HTMLInputElement>('input.ce-cmp');
  expect(inputs[0].value).toBe('0');
  fireEvent.change(inputs[0], { target: { value: '2' } });
  fireEvent.change(inputs[0], { target: { value: '0' } });
  fireEvent.click(screen.getAllByTitle('Move down')[0]);
  fireEvent.click(screen.getByText('Save'));
  await waitFor(() => expect(set).toHaveBeenCalledOnce());
  const saved = JSON.parse(set.mock.calls[0][3]);
  expect(saved[0]._native).toEqual(second._native);
  expect(saved[0]._edited).toBeUndefined();
  expect(saved[1]._native).toEqual(first._native);
  expect(saved[1]._edited).toEqual(['value']);
  expect(saved[1].value).toBe(0);
  expect(saved[1].operator).toBe('Unknown (6)');
});

it('drops the reference when a row moves off Reference run-on', async () => {
  const row = { function: 'GetScale', operator: '==', value: 1, runOn: 'Reference', reference: '000803:Patch.esp' };
  const set = vi.fn<(...args: string[]) => Promise<string>>(async () => 'staged');
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetConditionsAt: vi.fn(async () => JSON.stringify([row])),
    GetConditionFunctions: vi.fn(async () => '["GetScale"]'),
    GetConditionRunOnTypes: vi.fn(async () => '["Subject","Reference"]'),
    GetConditionFunctionParams: vi.fn(async () => '{}'),
    ResolveFormKeyLabels: vi.fn(async () => '{}'),
    SetConditionsAt: set,
  } } } });
  render(<ConditionsEditor plugin="Patch.esp" record="000800:Patch.esp"
    path="Conditions" label="Test" onClose={() => {}} onSaved={() => {}} />);
  fireEvent.change(await screen.findByDisplayValue('Reference'), { target: { value: 'Subject' } });
  fireEvent.click(screen.getByText('Save'));
  await waitFor(() => expect(set).toHaveBeenCalledOnce());
  const saved = JSON.parse(set.mock.calls[0][3]);
  expect(saved[0].runOn).toBe('Subject');
  expect(saved[0]).not.toHaveProperty('reference');
});

it('marks runOn edited when a preserved row leaves Reference run-on', async () => {
  const row = { function: 'GetScale', operator: '==', value: 1, runOn: 'Reference', reference: '000803:Patch.esp',
    _native: { ctda: [1, 2, 3], cis1: null, cis2: null } };
  const set = vi.fn<(...args: string[]) => Promise<string>>(async () => 'staged');
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetConditionsAt: vi.fn(async () => JSON.stringify([row])),
    GetConditionFunctions: vi.fn(async () => '["GetScale"]'),
    GetConditionRunOnTypes: vi.fn(async () => '["Subject","Reference"]'),
    GetConditionFunctionParams: vi.fn(async () => '{}'),
    ResolveFormKeyLabels: vi.fn(async () => '{}'),
    SetConditionsAt: set,
  } } } });
  render(<ConditionsEditor plugin="Patch.esp" record="000800:Patch.esp"
    path="Conditions" label="Test" onClose={() => {}} onSaved={() => {}} />);
  fireEvent.change(await screen.findByDisplayValue('Reference'), { target: { value: 'Subject' } });
  fireEvent.click(screen.getByText('Save'));
  await waitFor(() => expect(set).toHaveBeenCalledOnce());
  const saved = JSON.parse(set.mock.calls[0][3]);
  expect(saved[0].runOn).toBe('Subject');
  expect(saved[0]).not.toHaveProperty('reference');
  expect(saved[0]._edited).toContain('runOn');
});

it('locks the function select on preserved rows only', async () => {
  const native = { function: 'GetScale', operator: '==', value: 1, runOn: 'Subject',
    _native: { ctda: [1, 2, 3], cis1: null, cis2: null } };
  const plain = { function: 'GetScale', operator: '==', value: 1, runOn: 'Subject' };
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetConditionsAt: vi.fn(async () => JSON.stringify([native, plain])),
    GetConditionFunctions: vi.fn(async () => '["GetScale"]'),
    GetConditionRunOnTypes: vi.fn(async () => '["Subject"]'),
    GetConditionFunctionParams: vi.fn(async () => '{}'),
    SetConditionsAt: vi.fn(async () => 'staged'),
  } } } });
  const { container } = render(<ConditionsEditor plugin="Patch.esp" record="000800:Patch.esp"
    path="Conditions" label="Test" onClose={() => {}} onSaved={() => {}} />);
  await screen.findAllByDisplayValue('GetScale');
  const selects = container.querySelectorAll<HTMLSelectElement>('select.ce-fn');
  expect(selects[0].disabled).toBe(true);
  expect(selects[0].title).toBe('Replace the row to change its function');
  expect(selects[1].disabled).toBe(false);
});

it('shows an error instead of swallowing a failed record name lookup', async () => {
  const row = { function: 'GetScale', operator: '==', value: 1, runOn: 'Reference', reference: '000803:Patch.esp' };
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetConditionsAt: vi.fn(async () => JSON.stringify([row])),
    GetConditionFunctions: vi.fn(async () => '["GetScale"]'),
    GetConditionRunOnTypes: vi.fn(async () => '["Subject","Reference"]'),
    GetConditionFunctionParams: vi.fn(async () => '{}'),
    ResolveFormKeyLabels: vi.fn(async () => { throw new Error('lookup offline'); }),
  } } } });
  render(<ConditionsEditor plugin="Patch.esp" record="000800:Patch.esp"
    path="Conditions" label="Test" onClose={() => {}} onSaved={() => {}} />);
  expect(await screen.findByText('Could not resolve record names: lookup offline')).toBeTruthy();
});

it('shows an error instead of swallowing a failed function list read', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetConditionsAt: vi.fn(async () => '[]'),
    GetConditionFunctions: vi.fn(async () => { throw new Error('functions offline'); }),
    GetConditionRunOnTypes: vi.fn(async () => '["Subject"]'),
    GetConditionFunctionParams: vi.fn(async () => '{}'),
  } } } });
  render(<ConditionsEditor plugin="Patch.esp" record="000800:Patch.esp"
    path="Conditions" label="Test" onClose={() => {}} onSaved={() => {}} />);
  expect(await screen.findByText('Could not read condition functions: functions offline')).toBeTruthy();
});
