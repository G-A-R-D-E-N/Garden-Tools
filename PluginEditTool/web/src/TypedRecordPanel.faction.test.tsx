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
  await waitFor(() => expect(onChanged).toHaveBeenCalledOnce());
  expect(read).toHaveBeenCalledOnce();
  expect(edit).toHaveBeenCalledWith('W0.esp', formKey, 'relations', 'set', 0, {
    faction: '000A01:W0.esp', modifier: -20, reaction: 'enemy',
  });
});

it('sends the whole rank with its new number and the titles it was read with', async () => {
  const rankFact = {
    formKey, relations: [],
    ranks: [{ number: 0, maleTitle: { rawId: '0A0B' }, femaleTitle: { stringId: 42 }, hasInsignia: false }],
  };
  const edit = vi.fn(async () => 'Updated ranks entry in ' + formKey + ' in W0.esp.');
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetFaction: vi.fn(async () => JSON.stringify(rankFact)), EditFactionEntries: edit } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={async () => {}} />);
  fireEvent.click(await screen.findByText('raw 0A0B'));
  fireEvent.change(screen.getByLabelText('Rank'), { target: { value: '1' } });
  fireEvent.click(screen.getAllByRole('button', { name: 'Set' })[1]);
  await waitFor(() => expect(edit).toHaveBeenCalledOnce());
  expect(edit).toHaveBeenCalledWith('W0.esp', formKey, 'ranks', 'set', 0, {
    number: 1, maleTitle: { rawId: '0A0B' }, femaleTitle: { stringId: 42 }, hasInsignia: false,
  });
});

it('hides the Index field on FACT relations and ranks, which always append', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetFaction: vi.fn(async () => JSON.stringify(fact)) } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={async () => {}} />);
  expect(await screen.findByText('000A01:W0.esp')).toBeTruthy();
  expect(screen.queryByLabelText('Index')).toBeNull();
});
