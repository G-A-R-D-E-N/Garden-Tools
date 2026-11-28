import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import QuestEditor from '../QuestEditor';
import { openQuest, openTab, stubBackend, undoButton } from './questTestHarness';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('Detects: an edited objective text is written, and Undo writes the old text back and shows it again', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  openTab('Objectives');
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

it('Detects: an objective with null text and textMissing shows a note and opens with empty text', async () => {
  stubBackend({
    GetQuestObjectives: vi.fn(async () => JSON.stringify({ objectives: [
      { index: 10, flags: 0, displayText: null, textMissing: true, targets: [] },
    ] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  expect(await screen.findByText(/Text not found in the string table/)).toBeTruthy();
  expect((screen.getByLabelText('Objective 10 text') as HTMLTextAreaElement).value).toBe('');
});

it('Detects: renumbering an objective with textMissing clears textMissing in the payload, so the native setter does not keep the old text ID for the new number', async () => {
  const backend = stubBackend({
    GetQuestObjectives: vi.fn(async () => JSON.stringify({ objectives: [
      { index: 10, flags: 0, displayText: null, textMissing: true, targets: [] },
    ] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await screen.findByText(/Text not found in the string table/);
  const number = screen.getByLabelText('Objective 10 number');
  fireEvent.change(number, { target: { value: '11' } });
  fireEvent.blur(number);
  await waitFor(() => expect(backend.SetQuestObjectives).toHaveBeenCalledTimes(1));
  const sent = JSON.parse(backend.SetQuestObjectives.mock.calls[0][2]);
  expect(sent[0].index).toBe(11);
  expect(sent[0].textMissing).toBeUndefined();
  expect(sent[0].displayText).toBe('');
});

it('Detects: typing new text into an objective with textMissing clears textMissing in the payload and sends the typed text', async () => {
  const backend = stubBackend({
    GetQuestObjectives: vi.fn(async () => JSON.stringify({ objectives: [
      { index: 10, flags: 0, displayText: null, textMissing: true, targets: [] },
    ] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  const text = await screen.findByLabelText('Objective 10 text');
  fireEvent.change(text, { target: { value: 'Meet the courier' } });
  fireEvent.blur(text);
  await waitFor(() => expect(backend.SetQuestObjectives).toHaveBeenCalledTimes(1));
  const sent = JSON.parse(backend.SetQuestObjectives.mock.calls[0][2]);
  expect(sent[0].textMissing).toBeUndefined();
  expect(sent[0].displayText).toBe('Meet the courier');
});

it('Detects: two objectives with the same number show an inline warning and SetQuestObjectives is not called', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  openTab('Objectives');
  const number = await screen.findByLabelText('Objective 20 number');
  fireEvent.change(number, { target: { value: '10' } });
  fireEvent.blur(number);
  expect(await screen.findByText(/same number \(10\)/)).toBeTruthy();
  expect(backend.SetQuestObjectives).not.toHaveBeenCalled();
});
