import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from './backend';
import TypedRecordPanel from './TypedRecordPanel';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('keeps NPC list editors isolated after adding a faction', async () => {
  const matrix: ConflictMatrix = {
    FormKey: '000900:Patch.esp', EditorID: 'TestNpc', Type: 'NPC_', Winner: 'Patch.esp',
    Plugins: ['Patch.esp'], Level: 'onlyone', Rows: [],
  };
  let npc = { formKey: matrix.FormKey, factions: [] as { faction: string; rank: number }[], perks: [], inventory: [] };
  const read = vi.fn(async () => JSON.stringify(npc));
  const edit = vi.fn(async () => {
    npc = { ...npc, factions: [{ faction: '000901:Base.esm', rank: 1 }] };
    return 'Added faction.';
  });
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetNpc: read, EditNpcEntries: edit } } } });
  const onChanged = vi.fn(async () => {
    view.rerender(<TypedRecordPanel matrix={{ ...matrix }} plugin="Patch.esp" onChanged={onChanged} />);
  });
  const view = render(<TypedRecordPanel matrix={matrix} plugin="Patch.esp" onChanged={onChanged} />);
  const inventory = within(await screen.findByRole('group', { name: 'Inventory' }));
  fireEvent.change(inventory.getByLabelText('Item'), { target: { value: '000902:Base.esm' } });
  fireEvent.change(inventory.getByLabelText('Count'), { target: { value: '7' } });
  const factions = within(screen.getByRole('group', { name: 'Factions' }));
  fireEvent.change(factions.getByLabelText('Faction'), { target: { value: '000901:Base.esm' } });
  fireEvent.change(factions.getByLabelText('Rank'), { target: { value: '1' } });
  fireEvent.click(factions.getByRole('button', { name: 'Add' }));
  await waitFor(() => expect(read).toHaveBeenCalledTimes(2));
  await screen.findByText('000901:Base.esm');
  expect(screen.getAllByRole('group').map(group => group.querySelector('legend')?.textContent)).toEqual(['Factions', 'Perks', 'Inventory']);
  expect((within(screen.getByRole('group', { name: 'Inventory' })).getByLabelText('Item') as HTMLInputElement).value).toBe('000902:Base.esm');
});

it('keeps FACT list editors isolated after adding a relation', async () => {
  const matrix: ConflictMatrix = {
    FormKey: '000900:Patch.esp', EditorID: 'TestFaction', Type: 'FACT', Winner: 'Patch.esp',
    Plugins: ['Patch.esp'], Level: 'onlyone', Rows: [],
  };
  let faction = { formKey: matrix.FormKey, relations: [] as { faction: string; modifier: number; reaction: string }[], ranks: [] };
  const read = vi.fn(async () => JSON.stringify(faction));
  const edit = vi.fn(async () => {
    faction = { ...faction, relations: [{ faction: '000901:Base.esm', modifier: 0, reaction: 'neutral' }] };
    return 'Added relation.';
  });
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetFaction: read, EditFactionEntries: edit } } } });
  const onChanged = vi.fn(async () => {
    view.rerender(<TypedRecordPanel matrix={{ ...matrix }} plugin="Patch.esp" onChanged={onChanged} />);
  });
  const view = render(<TypedRecordPanel matrix={matrix} plugin="Patch.esp" onChanged={onChanged} />);
  const relations = within(await screen.findByRole('group', { name: 'Relations' }));
  fireEvent.change(relations.getByLabelText('Faction'), { target: { value: '000901:Base.esm' } });
  fireEvent.change(relations.getByLabelText('Modifier'), { target: { value: '0' } });
  fireEvent.change(relations.getByLabelText('Reaction'), { target: { value: 'neutral' } });
  fireEvent.click(relations.getByRole('button', { name: 'Add' }));
  await waitFor(() => expect(read).toHaveBeenCalledTimes(2));
  await screen.findByText('000901:Base.esm');
  expect(screen.getAllByRole('group').map(group => group.querySelector('legend')?.textContent)).toEqual(['Relations', 'Ranks']);
});
