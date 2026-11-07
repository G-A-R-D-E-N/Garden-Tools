import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
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

const npcFormKey = '000900:W0.esp';
const npcMatrix: ConflictMatrix = {
  FormKey: npcFormKey, EditorID: 'TestNpc', Type: 'NPC_', Winner: 'W0.esp', Plugins: ['W0.esp'], Level: 'onlyone', Rows: [],
};
const npc = {
  formKey: npcFormKey,
  factions: [{ faction: '000900:W0.esp', rank: 1 }],
  perks: [],
  inventory: [],
};

const weaponMatrix: ConflictMatrix = {
  FormKey: '000B00:W0.esp', EditorID: 'TestWeapon', Type: 'WEAP', Winner: 'W0.esp', Plugins: ['W0.esp', 'Other.esp'], Level: 'conflict', Rows: [],
};

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('saves the leveled list settings as numbers and refreshes', async () => {
  const save = vi.fn(async () => 'Updated settings.');
  const onChanged = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetLeveledList: vi.fn(async () => JSON.stringify(list)), SetLeveledListSettings: save } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={onChanged} />);
  await screen.findByText('000802:W0.esp');
  fireEvent.change(screen.getAllByLabelText('Chance none')[0], { target: { value: '7' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save settings' }));
  expect(await screen.findByText('Updated settings.')).toBeTruthy();
  expect(save).toHaveBeenCalledWith('W0.esp', formKey, 7, 1, 0);
  await waitFor(() => expect(onChanged).toHaveBeenCalledOnce());
});

it('shows a refused settings save as an alert and does not refresh', async () => {
  const onChanged = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetLeveledList: vi.fn(async () => JSON.stringify(list)),
    SetLeveledListSettings: vi.fn(async () => { throw new Error('chance out of range'); }),
  } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={onChanged} />);
  await screen.findByText('000802:W0.esp');
  fireEvent.click(screen.getByRole('button', { name: 'Save settings' }));
  expect((await screen.findByRole('alert')).textContent).toBe('Settings edit failed: chance out of range');
  expect(onChanged).not.toHaveBeenCalled();
});

it('adds an entry at the end index without an extra field', async () => {
  const add = vi.fn(async () => 'Added leveled list entry.');
  const onChanged = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetLeveledList: vi.fn(async () => JSON.stringify(list)), EditLeveledListEntry: add } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={onChanged} />);
  const entries = within(await screen.findByRole('group', { name: 'Entries' }));
  fireEvent.change(entries.getByLabelText('Level'), { target: { value: '3' } });
  fireEvent.change(entries.getByLabelText('Item'), { target: { value: '000803:W0.esp' } });
  fireEvent.change(entries.getByLabelText('Count'), { target: { value: '4' } });
  fireEvent.change(entries.getByLabelText('Chance none'), { target: { value: '5' } });
  fireEvent.click(entries.getByRole('button', { name: 'Add' }));
  expect(await screen.findByText('Added leveled list entry.')).toBeTruthy();
  expect(add).toHaveBeenCalledWith('W0.esp', formKey, 'add', 2, { level: 3, item: '000803:W0.esp', count: 4, chanceNone: 5 });
  await waitFor(() => expect(onChanged).toHaveBeenCalledOnce());
});

it('removes the picked NPC faction by index with the list name', async () => {
  const remove = vi.fn(async () => 'Removed factions entry.');
  const onChanged = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetNpc: vi.fn(async () => JSON.stringify(npc)), EditNpcEntries: remove } } } });
  render(<TypedRecordPanel matrix={npcMatrix} plugin="W0.esp" onChanged={onChanged} />);
  fireEvent.click(await screen.findByText('000900:W0.esp'));
  fireEvent.click(screen.getAllByRole('button', { name: 'Remove' })[0]);
  expect(await screen.findByText('Removed factions entry.')).toBeTruthy();
  expect(remove).toHaveBeenCalledWith('W0.esp', npcFormKey, 'factions', 'remove', 0, undefined);
  await waitFor(() => expect(onChanged).toHaveBeenCalledOnce());
});

it('shows a read failure and a missing bridge', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetLeveledList: vi.fn(async () => { throw new Error('boom'); }) } } } });
  const { unmount } = render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={async () => {}} />);
  expect((await screen.findByRole('alert')).textContent).toBe('Could not read record: boom');
  unmount();
  vi.stubGlobal('chrome', undefined);
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={async () => {}} />);
  expect((await screen.findByRole('alert')).textContent).toBe('Could not read record: No backend bridge.');
});

it('does not show the previous record while the next one loads and shows the empty weapon list', async () => {
  const read = vi.fn(async () => JSON.stringify(list));
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetLeveledList: read,
    GetWeaponDamageTypes: vi.fn(async () => JSON.stringify({ formKey: weaponMatrix.FormKey, damageTypes: [] })),
  } } } });
  const { rerender } = render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={async () => {}} />);
  expect(await screen.findByText('000802:W0.esp')).toBeTruthy();
  read.mockImplementation(() => new Promise<string>(() => {}));
  rerender(<TypedRecordPanel matrix={{ ...matrix, FormKey: '000850:W0.esp' }} plugin="W0.esp" onChanged={async () => {}} />);
  expect(await screen.findByText('Reading the record…')).toBeTruthy();
  expect(screen.queryByText('000802:W0.esp')).toBeNull();
  rerender(<TypedRecordPanel matrix={weaponMatrix} plugin="Other.esp" onChanged={async () => {}} />);
  expect(await screen.findByText('No damage types.')).toBeTruthy();
  expect(screen.queryByText(/Showing the winning version/)).toBeNull();
});
