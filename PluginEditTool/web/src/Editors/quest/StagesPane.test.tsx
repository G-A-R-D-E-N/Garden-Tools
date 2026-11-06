import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import QuestEditor from '../QuestEditor';
import { openQuest, stubBackend, undoButton } from './questTestHarness';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('Detects: an edited stage log entry is written, and Undo writes the old text back and shows it again', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  const logText = await screen.findByLabelText('Stage 10 log entry 1 text');
  fireEvent.change(logText, { target: { value: 'Find the courier at the gate' } });
  fireEvent.blur(logText);
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  expect(JSON.parse(backend.SetQuestStages.mock.calls[0][2])[0].logEntries[0].logText).toBe('Find the courier at the gate');
  await waitFor(() => expect(undoButton().disabled).toBe(false));
  fireEvent.click(undoButton());
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(2));
  expect(JSON.parse(backend.SetQuestStages.mock.calls[1][2])[0].logEntries[0].logText).toBe('Find the courier');
  await waitFor(() => expect((screen.getByLabelText('Stage 10 log entry 1 text') as HTMLInputElement).value)
    .toBe('Find the courier'));
});
