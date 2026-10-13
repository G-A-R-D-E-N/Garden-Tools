import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from './backend';
import TypedRecordPanel from './TypedRecordPanel';

const formKey = '000800:W0.esp';
const matrix: ConflictMatrix = {
  FormKey: formKey, EditorID: 'TestList', Type: 'LVLI', Winner: 'W0.esp', Plugins: ['W0.esp'], Level: 'onlyone', Rows: [],
};
const list = {
  formKey, chanceNone: 5, maxCount: 1, flags: 0, useGlobal: null, epicLootChance: null,
  entries: [
    { level: 1, item: '000801:W0.esp', count: 1, chanceNone: 0, extra: null },
    { level: 10, item: '000802:W0.esp', count: 2, chanceNone: 25, extra: { owner: null, globalOrRank: 0, itemCondition: 1 } },
  ],
};

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('renders the settings and entries of the winning leveled list', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetLeveledList: vi.fn(async () => JSON.stringify(list)) } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={async () => {}} />);
  expect(await screen.findByText('000802:W0.esp')).toBeTruthy();
  expect(screen.getByText('25')).toBeTruthy();
  expect(screen.getByDisplayValue('5')).toBeTruthy();
});

it('sets an entry at its index, refreshes the list and reports the native message', async () => {
  const read = vi.fn(async () => JSON.stringify(list));
  const edit = vi.fn(async () => 'Updated leveled list entry 1 in ' + formKey + ' in W0.esp.');
  const onChanged = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetLeveledList: read, EditLeveledListEntry: edit } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={onChanged} />);
  fireEvent.click(await screen.findByText('000802:W0.esp'));
  fireEvent.change(screen.getByLabelText('Level'), { target: { value: '12' } });
  fireEvent.click(screen.getByRole('button', { name: 'Set' }));
  await waitFor(() => expect(read).toHaveBeenCalledTimes(2));
  expect(edit).toHaveBeenCalledWith('W0.esp', formKey, 'set', 1, {
    level: 12, item: '000802:W0.esp', count: 2, chanceNone: 25,
    extra: { owner: null, globalOrRank: 0, itemCondition: 1 },
  });
  expect(onChanged).toHaveBeenCalledOnce();
  expect(await screen.findByText('Updated leveled list entry 1 in ' + formKey + ' in W0.esp.')).toBeTruthy();
});

it('shows a refused leveled list edit with its error text and does not refresh', async () => {
  const read = vi.fn(async () => JSON.stringify(list));
  const onChanged = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetLeveledList: read,
    EditLeveledListEntry: vi.fn(async () => { throw new Error('Leveled list entry index must be a non-negative integer'); }),
  } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={onChanged} />);
  await screen.findByText('000802:W0.esp');
  fireEvent.click(screen.getByRole('button', { name: 'Remove' }));
  expect(await screen.findByText('Entry edit failed: Leveled list entry index must be a non-negative integer')).toBeTruthy();
  expect(read).toHaveBeenCalledOnce();
  expect(onChanged).not.toHaveBeenCalled();
});
