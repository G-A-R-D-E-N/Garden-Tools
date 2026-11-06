import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import QuestEditor from '../QuestEditor';
import { openQuest, stubBackend, undoButton } from './questTestHarness';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('Detects: an edited alias name is written, and Undo writes the old name back and shows it again', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
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
