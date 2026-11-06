import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import QuestEditor from '../QuestEditor';
import { openQuest, stubBackend, undoButton } from './questTestHarness';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('Detects: an edited objective text is written, and Undo writes the old text back and shows it again', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  const text = await screen.findByLabelText('Objective 10 text');
  fireEvent.change(text, { target: { value: 'Meet the courier' } });
  fireEvent.blur(text);
  await waitFor(() => expect(backend.SetQuestObjectives).toHaveBeenCalledTimes(1));
  expect(JSON.parse(backend.SetQuestObjectives.mock.calls[0][2])[0].displayText).toBe('Meet the courier');
  await waitFor(() => expect(undoButton().disabled).toBe(false));
  fireEvent.click(undoButton());
  await waitFor(() => expect(backend.SetQuestObjectives).toHaveBeenCalledTimes(2));
  expect(JSON.parse(backend.SetQuestObjectives.mock.calls[1][2])[0].displayText).toBe('Locate the courier at the docks');
  await waitFor(() => expect((screen.getByLabelText('Objective 10 text') as HTMLInputElement).value)
    .toBe('Locate the courier at the docks'));
});

it('Detects: two objectives with the same number show an inline warning and SetQuestObjectives is not called', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  const number = await screen.findByLabelText('Objective 20 number');
  fireEvent.change(number, { target: { value: '10' } });
  fireEvent.blur(number);
  expect(await screen.findByText(/same number \(10\)/)).toBeTruthy();
  expect(backend.SetQuestObjectives).not.toHaveBeenCalled();
});
