import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import ConditionsEditor from './ConditionsEditor';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('keeps native row metadata through edit, reorder and browser JSON transport', async () => {
  const first = { function: 'GetScale', operator: 'Unknown (6)', value: -0, runOn: 'Subject',
    _native: { ctda: [1, 2, 3], cis1: 'first', cis2: null } };
  const second = { ...first, operator: '==', value: 1,
    _native: { ctda: [4, 5, 6], cis1: 'second', cis2: null } };
  const set = vi.fn(async (..._arguments: string[]) => 'staged');
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
