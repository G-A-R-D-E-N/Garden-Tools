import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from './backend';
import TypedRecordPanel from './TypedRecordPanel';

const formKey = '000900:W0.esp';
const matrix: ConflictMatrix = {
  FormKey: formKey, EditorID: 'TestNpc', Type: 'NPC_', Winner: 'W0.esp', Plugins: ['W0.esp'], Level: 'onlyone', Rows: [],
};
const npc = {
  formKey,
  factions: [{ faction: '000901:W0.esp', rank: 1 }],
  perks: [{ perk: '000902:W0.esp', rank: 2 }],
  inventory: [{ item: '000903:W0.esp', count: 3, extra: { owner: null, globalOrRank: 0, condition: 0.5 } }],
};

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('renders factions, perks and inventory of the winning NPC_', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetNpc: vi.fn(async () => JSON.stringify(npc)) } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={async () => {}} />);
  expect(await screen.findByText('000901:W0.esp')).toBeTruthy();
  expect(screen.getByText('000902:W0.esp')).toBeTruthy();
  expect(screen.getByText('000903:W0.esp')).toBeTruthy();
  expect(screen.getByText('yes')).toBeTruthy();
});

it('sets a faction rank at its index with the NPC list name and refreshes', async () => {
  const read = vi.fn(async () => JSON.stringify(npc));
  const edit = vi.fn(async () => 'Updated factions entry in ' + formKey + ' in W0.esp.');
  const onChanged = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetNpc: read, EditNpcEntries: edit } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={onChanged} />);
  fireEvent.click(await screen.findByText('000901:W0.esp'));
  fireEvent.change(screen.getAllByLabelText('Rank')[0], { target: { value: '3' } });
  fireEvent.click(screen.getAllByRole('button', { name: 'Set' })[0]);
  await waitFor(() => expect(onChanged).toHaveBeenCalledOnce());
  expect(read).toHaveBeenCalledOnce();
  expect(edit).toHaveBeenCalledWith('W0.esp', formKey, 'factions', 'set', 0, { faction: '000901:W0.esp', rank: 3 });
});

it('keeps Set and Remove disabled on NPC_ lists until a row is picked', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetNpc: vi.fn(async () => JSON.stringify(npc)) } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={async () => {}} />);
  fireEvent.click(await screen.findByText('000901:W0.esp'));
  expect((screen.getAllByRole('button', { name: 'Set' })[0] as HTMLButtonElement).disabled).toBe(false);
  expect((screen.getAllByRole('button', { name: 'Remove' })[0] as HTMLButtonElement).disabled).toBe(false);
  expect((screen.getAllByRole('button', { name: 'Set' })[1] as HTMLButtonElement).disabled).toBe(true);
  expect((screen.getAllByRole('button', { name: 'Remove' })[2] as HTMLButtonElement).disabled).toBe(true);
});

it('hides the Index field on NPC_ lists, which always append', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetNpc: vi.fn(async () => JSON.stringify(npc)) } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={async () => {}} />);
  expect(await screen.findByText('000901:W0.esp')).toBeTruthy();
  expect(screen.queryByLabelText('Index')).toBeNull();
});
