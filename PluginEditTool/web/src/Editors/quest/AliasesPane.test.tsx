import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import QuestEditor from '../QuestEditor';
import { openQuest, openTab, stubBackend, undoButton } from './questTestHarness';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('Detects: switching an alias to collection with no entries is blocked, so native never receives an empty collection', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  openTab('Aliases');
  fireEvent.change(await screen.findByLabelText('Alias 0 kind'), { target: { value: 'collection' } });
  expect((await screen.findByRole('alert')).textContent).toContain('needs at least one entry');
  expect((screen.getByLabelText('Alias 0 kind') as HTMLSelectElement).value).toBe('reference');
  expect(backend.SetQuestAliases).not.toHaveBeenCalled();
});

it('Detects: the Aliases pane says saving keeps fill rules, keywords and items, which the editor cannot show or edit', async () => {
  stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  openTab('Aliases');
  expect(await screen.findByText(/Fill rules, keywords and items are kept when you save/)).toBeTruthy();
});

it('Detects: Save in an alias Conditions editor with no edits writes nothing', async () => {
  const condition = { function: 'GetIsID', operator: '==', param1: '000800:Patch.esp', value: 1, runOn: 'Subject' };
  const backend = stubBackend({
    GetQuestAliases: vi.fn(async () => JSON.stringify({ aliases: [
      { kind: 'reference', id: 0, name: 'Courier', flags: 0, conditions: [condition] },
    ] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  openTab('Aliases');
  fireEvent.click(await screen.findByRole('button', { name: 'Conditions (1)' }));
  fireEvent.click(screen.getByRole('button', { name: 'Save' }));
  await waitFor(() => expect(screen.queryByRole('button', { name: 'Save' })).toBeNull());
  expect(backend.SetQuestAliases).not.toHaveBeenCalled();
});

it('Detects: editing an alias condition value and saving writes the new value in SetQuestAliases, and Cancel writes nothing', async () => {
  const condition = { function: 'GetIsID', operator: '==', param1: '000800:Patch.esp', value: 1, runOn: 'Subject' };
  const backend = stubBackend({
    GetQuestAliases: vi.fn(async () => JSON.stringify({ aliases: [
      { kind: 'reference', id: 0, name: 'Courier', flags: 0, conditions: [condition] },
    ] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  openTab('Aliases');
  fireEvent.click(await screen.findByRole('button', { name: 'Conditions (1)' }));
  fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
  expect(backend.SetQuestAliases).not.toHaveBeenCalled();
  fireEvent.click(screen.getByRole('button', { name: 'Conditions (1)' }));
  fireEvent.change(document.querySelector('input.ce-cmp') as HTMLInputElement, { target: { value: '0' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save' }));
  await waitFor(() => expect(backend.SetQuestAliases).toHaveBeenCalledTimes(1));
  expect(JSON.parse(backend.SetQuestAliases.mock.calls[0][2])[0].conditions).toEqual([{ ...condition, value: 0 }]);
});

it('Detects: an alias whose conditions could not be read shows a note and no Conditions button', async () => {
  stubBackend({
    GetQuestAliases: vi.fn(async () => JSON.stringify({ aliases: [
      { kind: 'reference', id: 0, name: 'Courier', flags: 0, conditionsUnreadable: true },
    ] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  openTab('Aliases');
  const panel = await screen.findByRole('tabpanel');
  expect(await within(panel).findByText(/Conditions could not be read/)).toBeTruthy();
  expect(within(panel).queryByRole('button', { name: /^Conditions/ })).toBeNull();
});

it('Detects: an edited alias name is written, and Undo writes the old name back and shows it again', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  openTab('Aliases');
  const name = await screen.findByLabelText('Alias 0 name');
  fireEvent.change(name, { target: { value: 'Courier Ann' } });
  fireEvent.blur(name);
  await waitFor(() => expect(backend.SetQuestAliases).toHaveBeenCalledTimes(1));
  expect(JSON.parse(backend.SetQuestAliases.mock.calls[0][2])).toEqual([{ kind: 'reference', id: 0, name: 'Courier Ann', flags: 0 }]);
  await waitFor(() => expect(undoButton().disabled).toBe(false));
  fireEvent.click(undoButton());
  await waitFor(() => expect(backend.SetQuestAliases).toHaveBeenCalledTimes(2));
  expect(JSON.parse(backend.SetQuestAliases.mock.calls[1][2])).toEqual([{ kind: 'reference', id: 0, name: 'Courier', flags: 0 }]);
  await waitFor(() => expect((screen.getByLabelText('Alias 0 name') as HTMLInputElement).value).toBe('Courier'));
});
