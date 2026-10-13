import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from './backend';
import TypedRecordPanel from './TypedRecordPanel';

const formKey = '000A00:W0.esp';
const matrix: ConflictMatrix = {
  FormKey: formKey, EditorID: 'TestFaction', Type: 'FACT', Winner: 'W0.esp', Plugins: ['W0.esp'], Level: 'onlyone', Rows: [],
};
const fact = {
  formKey,
  relations: [{ faction: '000A01:W0.esp', modifier: -10, reaction: 'enemy' }],
  ranks: [{ number: 0, maleTitle: 'Raider', femaleTitle: { stringId: 42 }, hasInsignia: false }],
};

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('renders relations and ranks with localized titles of the winning FACT', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetFaction: vi.fn(async () => JSON.stringify(fact)) } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={async () => {}} />);
  expect(await screen.findByText('000A01:W0.esp')).toBeTruthy();
  expect(screen.getByText('enemy')).toBeTruthy();
  expect(screen.getByText('Raider')).toBeTruthy();
  expect(screen.getByText('string 42')).toBeTruthy();
});

it('sets a relation at its index with the relation list and refreshes', async () => {
  const read = vi.fn(async () => JSON.stringify(fact));
  const edit = vi.fn(async () => 'Updated relations entry in ' + formKey + ' in W0.esp.');
  const onChanged = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetFaction: read, EditFactionEntries: edit } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={onChanged} />);
  fireEvent.click(await screen.findByText('000A01:W0.esp'));
  fireEvent.change(screen.getByLabelText('Modifier'), { target: { value: '-20' } });
  fireEvent.click(screen.getAllByRole('button', { name: 'Set' })[0]);
  await waitFor(() => expect(read).toHaveBeenCalledTimes(2));
  expect(edit).toHaveBeenCalledWith('W0.esp', formKey, 'relations', 'set', 0, {
    faction: '000A01:W0.esp', modifier: -20, reaction: 'enemy',
  });
  expect(onChanged).toHaveBeenCalledOnce();
});
