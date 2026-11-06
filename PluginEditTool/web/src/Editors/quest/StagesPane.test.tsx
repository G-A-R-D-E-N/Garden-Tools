import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import QuestEditor from '../QuestEditor';
import { openQuest, stubBackend, undoButton } from './questTestHarness';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('Detects: a log entry offers no Conditions editor, because native rejects Stages[n].LogEntries[m].Conditions', async () => {
  stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await screen.findByLabelText('Stage 10 log entry 1 text');
  expect(screen.queryByRole('button', { name: /Conditions/ })).toBeNull();
});

it('Detects: the Stages pane warns that saving can remove log-entry conditions this screen cannot show', async () => {
  stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  expect(await screen.findByText(/cannot show log-entry conditions yet/)).toBeTruthy();
});

it('Detects: clicking a flow chart box highlights that stage card, so the chart and the stage list stay in step', async () => {
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify({ stages: [
      { index: 10, flags: 0, logEntries: [] },
      { index: 20, flags: 0, logEntries: [] },
    ] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  fireEvent.click(await screen.findByRole('button', { name: /^Stage 20/ }));
  expect(screen.getByRole('button', { name: /^Stage 20/ }).getAttribute('aria-pressed')).toBe('true');
  expect(screen.getByRole('group', { name: 'Stage 20 flags' }).closest('.quest-card')?.classList.contains('quest-card-selected')).toBe(true);
  expect(screen.getByRole('group', { name: 'Stage 10 flags' }).closest('.quest-card')?.classList.contains('quest-card-selected')).toBe(false);
});

it('Detects: changing the outcome of a log entry with no journal text does not write an empty log text', async () => {
  const backend = stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify({ stages: [{ index: 10, flags: 0, logEntries: [{ logText: '', note: '', flags: 0 }] }] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  const outcome = await screen.findByRole('group', { name: 'Stage 10 log entry 1 outcome' });
  fireEvent.click(within(outcome).getByRole('checkbox', { name: 'Completes the quest' }));
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  expect(JSON.parse(backend.SetQuestStages.mock.calls[0][2])[0].logEntries[0]).toEqual({ flags: 1 });
});

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
