import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from './backend';
import TypedRecordPanel from './TypedRecordPanel';

const formKey = '00080D:W0.esp';
const matrix: ConflictMatrix = {
  FormKey: formKey, EditorID: 'RadProbeSpell', Type: 'SPEL', Winner: 'W0.esp', Plugins: ['W0.esp'], Level: 'onlyone', Rows: [],
};
const effects = {
  formKey,
  count: 1,
  effects: [{ index: 0, effect: '000804:W0.esp', magnitude: -10, area: 0, duration: 0 }],
};

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

// Detects: an effect list from a different winning plugin must remain read-only.
it('renders the effect list of the winning SPEL read-only from another plugin', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetMagicEffects: vi.fn(async () => JSON.stringify(effects)) } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="Other.esp" onChanged={async () => {}} />);
  expect(await screen.findByText('000804:W0.esp')).toBeTruthy();
  expect(screen.getByText('-10')).toBeTruthy();
  expect(screen.getByText(/Showing the winning version/)).toBeTruthy();
  expect(screen.queryAllByRole('button')).toHaveLength(0);
});

// Detects: adding an effect must pass numeric fields and refresh the record.
it('adds an effect with numeric magnitude, area and duration and refreshes', async () => {
  const add = vi.fn(async () => 'Added effect.');
  const onChanged = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetMagicEffects: vi.fn(async () => JSON.stringify(effects)),
    AddMagicEffect: add,
  } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={onChanged} />);
  const group = within(await screen.findByRole('group', { name: 'Effects' }));
  fireEvent.change(group.getByLabelText('Effect'), { target: { value: '000805:W0.esp' } });
  fireEvent.change(group.getByLabelText('Magnitude'), { target: { value: '12.5' } });
  fireEvent.change(group.getByLabelText('Area'), { target: { value: '5' } });
  fireEvent.change(group.getByLabelText('Duration'), { target: { value: '30' } });
  fireEvent.click(group.getByRole('button', { name: 'Add' }));
  expect(await screen.findByText('Added effect.')).toBeTruthy();
  expect(add).toHaveBeenCalledWith('W0.esp', formKey, '000805:W0.esp', 12.5, 5, 30);
  await waitFor(() => expect(onChanged).toHaveBeenCalledOnce());
});

// Detects: editing an effect must update the selected index.
it('sets the picked effect entry by index', async () => {
  const set = vi.fn(async () => 'Updated effect.');
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetMagicEffects: vi.fn(async () => JSON.stringify(effects)),
    SetMagicEffect: set,
  } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={async () => {}} />);
  fireEvent.click(await screen.findByText('000804:W0.esp'));
  const group = within(screen.getByRole('group', { name: 'Effects' }));
  fireEvent.change(group.getByLabelText('Magnitude'), { target: { value: '20' } });
  fireEvent.click(group.getByRole('button', { name: 'Set' }));
  expect(await screen.findByText('Updated effect.')).toBeTruthy();
  expect(set).toHaveBeenCalledWith('W0.esp', formKey, 0, '000804:W0.esp', 20, 0, 0);
});

// Detects: removing an effect must delete the selected index.
it('removes the picked effect by index', async () => {
  const remove = vi.fn(async () => 'Removed effect.');
  const onChanged = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetMagicEffects: vi.fn(async () => JSON.stringify(effects)),
    RemoveMagicEffect: remove,
  } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={onChanged} />);
  fireEvent.click(await screen.findByText('000804:W0.esp'));
  const group = within(screen.getByRole('group', { name: 'Effects' }));
  fireEvent.click(group.getByRole('button', { name: 'Remove' }));
  expect(await screen.findByText('Removed effect.')).toBeTruthy();
  expect(remove).toHaveBeenCalledWith('W0.esp', formKey, 0);
  await waitFor(() => expect(onChanged).toHaveBeenCalledOnce());
});

// Detects: a refused effect edit must show an alert without refreshing.
it('shows a refused effect edit as an alert and does not refresh', async () => {
  const onChanged = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetMagicEffects: vi.fn(async () => JSON.stringify(effects)),
    RemoveMagicEffect: vi.fn(async () => { throw new Error('NotMagicEffect: effect must be a magic effect (MGEF) FormKey'); }),
  } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="W0.esp" onChanged={onChanged} />);
  fireEvent.click(await screen.findByText('000804:W0.esp'));
  fireEvent.click(screen.getByRole('button', { name: 'Remove' }));
  expect((await screen.findByRole('alert')).textContent).toContain('Effect edit failed');
  expect(onChanged).not.toHaveBeenCalled();
});
