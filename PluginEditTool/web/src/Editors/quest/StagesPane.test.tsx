import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import QuestEditor from '../QuestEditor';
import { openQuest, stubBackend, undoButton } from './questTestHarness';

const scrollIntoView = vi.fn();

beforeEach(() => { scrollIntoView.mockClear(); Element.prototype.scrollIntoView = scrollIntoView; });
afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('Detects: a log entry offers no Conditions editor, because native rejects Stages[n].LogEntries[m].Conditions', async () => {
  stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await screen.findByLabelText('Stage 10 log entry 1 text');
  expect(screen.queryByRole('button', { name: /Conditions/ })).toBeNull();
});

it('Detects: a log entry whose text is missing from the string table shows a note and keeps textMissing when a stage is saved', async () => {
  const backend = stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify({ stages: [
      { index: 10, flags: 0, logEntries: [{ logText: null, flags: 0, textMissing: true, conditions: [] }] },
    ] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  expect(await screen.findByText(/Text not found in the string table/)).toBeTruthy();
  const note = screen.getByLabelText('Stage 10 log entry 1 note');
  fireEvent.change(note, { target: { value: 'Note' } });
  fireEvent.blur(note);
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  const sent = JSON.parse(backend.SetQuestStages.mock.calls[0][2]);
  expect(sent[0].logEntries[0].textMissing).toBe(true);
  expect(sent[0].logEntries[0].logText).toBe('');
});

it('Detects: typing new log text into a log entry with textMissing clears textMissing in the payload and sends the typed text', async () => {
  const backend = stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify({ stages: [
      { index: 10, flags: 0, logEntries: [{ logText: null, logTextId: 999, flags: 0, textMissing: true, conditions: [] }] },
    ] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  const text = await screen.findByLabelText('Stage 10 log entry 1 text');
  fireEvent.change(text, { target: { value: 'New journal line' } });
  fireEvent.blur(text);
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  const sent = JSON.parse(backend.SetQuestStages.mock.calls[0][2]);
  expect(sent[0].logEntries[0].textMissing).toBeUndefined();
  expect(sent[0].logEntries[0].logText).toBe('New journal line');
  expect(sent[0].logEntries[0].logTextId).toBe(999);
});

it('Detects: the Stages pane says conditions stay with their log entry when you save and that this screen cannot edit them', async () => {
  stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  expect(await screen.findByText(/Conditions stay with their log entry when you save/)).toBeTruthy();
  expect(screen.getByText(/This editor cannot show or edit conditions/)).toBeTruthy();
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
  expect(scrollIntoView).toHaveBeenCalledTimes(1);
});

it('Detects: clicking the already selected flow chart box scrolls its card into view again', async () => {
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify({ stages: [
      { index: 10, flags: 0, logEntries: [] },
      { index: 20, flags: 0, logEntries: [] },
    ] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  const box = await screen.findByRole('button', { name: /^Stage 20/ });
  fireEvent.click(box);
  fireEvent.click(box);
  expect(scrollIntoView).toHaveBeenCalledTimes(2);
});

it('Detects: the + next step button on a flow chart box saves the stages with a new stage after it', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  fireEvent.click(await screen.findByRole('button', { name: '+ next step' }));
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  expect(JSON.parse(backend.SetQuestStages.mock.calls[0][2]).map((item: { index: number }) => item.index)).toEqual([10, 20]);
  expect(await screen.findByRole('complementary', { name: 'Stage 20 details' })).toBeTruthy();
});

it('Detects: editing the journal text in the flow chart panel saves it, and Undo writes the old text back', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  fireEvent.click(await screen.findByRole('button', { name: /^Stage 10/ }));
  const journal = await screen.findByLabelText('Stage 10 journal text');
  fireEvent.change(journal, { target: { value: 'Meet at the dock' } });
  fireEvent.blur(journal);
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  expect(JSON.parse(backend.SetQuestStages.mock.calls[0][2])[0].logEntries[0].logText).toBe('Meet at the dock');
  await waitFor(() => expect(undoButton().disabled).toBe(false));
  fireEvent.click(undoButton());
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(2));
  expect(JSON.parse(backend.SetQuestStages.mock.calls[1][2])[0].logEntries[0].logText).toBe('Find the courier');
});

it('Detects: the Completes the quest checkbox in the flow chart panel saves the flag on the first log entry', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  fireEvent.click(await screen.findByRole('button', { name: /^Stage 10/ }));
  const outcome = await screen.findByRole('group', { name: 'Stage 10 outcome' });
  fireEvent.click(within(outcome).getByRole('checkbox', { name: 'Completes the quest' }));
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  expect(JSON.parse(backend.SetQuestStages.mock.calls[0][2])[0].logEntries[0].flags).toBe(1);
  await waitFor(() => expect((within(screen.getByRole('group', { name: 'Stage 10 outcome' }))
    .getByRole('checkbox', { name: 'Completes the quest' }) as HTMLInputElement).checked).toBe(true));
});

it('Detects: Remove step names the objective that is kept and saves nothing until it is confirmed', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  fireEvent.click(await screen.findByRole('button', { name: /^Stage 10/ }));
  fireEvent.click(await screen.findByRole('button', { name: 'Remove step' }));
  expect(screen.getByText(/Objective 10 is NOT removed/)).toBeTruthy();
  expect(backend.SetQuestStages).not.toHaveBeenCalled();
  fireEvent.click(screen.getByRole('button', { name: 'Keep stage 10' }));
  expect(screen.getByRole('button', { name: 'Remove step' })).toBeTruthy();
  expect(backend.SetQuestStages).not.toHaveBeenCalled();
});

it('Detects: confirming Remove step saves an empty stage list and closes the panel', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  fireEvent.click(await screen.findByRole('button', { name: /^Stage 10/ }));
  fireEvent.click(await screen.findByRole('button', { name: 'Remove step' }));
  fireEvent.click(screen.getByRole('button', { name: 'Yes, remove stage 10' }));
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  expect(JSON.parse(backend.SetQuestStages.mock.calls[0][2])).toEqual([]);
  expect(screen.queryByRole('complementary', { name: 'Stage 10 details' })).toBeNull();
});

it('Detects: a rejected stage save does not drop the stage it tried to remove from the next save', async () => {
  const backend = stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify({ stages: [
      { index: 10, flags: 0, logEntries: [{ logText: 'First', note: '', flags: 0 }] },
      { index: 20, flags: 0, logEntries: [{ logText: 'Second', note: '', flags: 0 }] },
    ] })),
    SetQuestStages: vi.fn<(...args: string[]) => Promise<string>>()
      .mockRejectedValueOnce(new Error('Native rejected the save'))
      .mockResolvedValue('ok'),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  fireEvent.click(await screen.findByRole('button', { name: /^Stage 20/ }));
  fireEvent.click(await screen.findByRole('button', { name: 'Remove step' }));
  fireEvent.click(screen.getByRole('button', { name: 'Yes, remove stage 20' }));
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  fireEvent.click(await screen.findByRole('button', { name: /^Stage 10/ }));
  const journal = await screen.findByLabelText('Stage 10 journal text');
  fireEvent.change(journal, { target: { value: 'Changed' } });
  fireEvent.blur(journal);
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(2));
  expect(JSON.parse(backend.SetQuestStages.mock.calls[1][2]).map((item: { index: number }) => item.index)).toEqual([10, 20]);
});

it('Detects: changing the outcome of a log entry with no journal text does not write an empty log text', async () => {
  const backend = stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify({ stages: [{ index: 10, flags: 0, logEntries: [{ logText: '', note: '', flags: 0, conditions: [] }] }] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  const outcome = await screen.findByRole('group', { name: 'Stage 10 log entry 1 outcome' });
  fireEvent.click(within(outcome).getByRole('checkbox', { name: 'Completes the quest' }));
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  expect(JSON.parse(backend.SetQuestStages.mock.calls[0][2])[0].logEntries[0]).toEqual({ flags: 1, conditions: [] });
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

it('Detects: Hide chart collapses the flow chart, keeps the selected stage and the stage list, and Show chart brings the boxes back', async () => {
  stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  fireEvent.click(await screen.findByRole('button', { name: /^Stage 10/ }));
  const hide = screen.getByRole('button', { name: 'Hide chart' });
  expect(hide.getAttribute('aria-expanded')).toBe('true');
  fireEvent.click(hide);
  expect(screen.queryByRole('button', { name: /^Stage 10/ })).toBeNull();
  expect(screen.getByLabelText('Stage 10 log entry 1 text')).toBeTruthy();
  const show = screen.getByRole('button', { name: 'Show chart' });
  expect(show.getAttribute('aria-expanded')).toBe('false');
  fireEvent.click(show);
  expect(screen.getByRole('button', { name: /^Stage 10/ }).getAttribute('aria-pressed')).toBe('true');
  expect(screen.getByRole('button', { name: 'Hide chart' }).getAttribute('aria-expanded')).toBe('true');
});

it('Detects: a long log entry shows its whole text in a multi-line textarea instead of cutting it off', async () => {
  const long = "Diamond City is the largest settlement in the Commonwealth. It's the place to find the courier before the market closes.";
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify({ stages: [
      { index: 10, flags: 0, logEntries: [{ logText: long, note: '', flags: 0 }] },
    ] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  const logText = await screen.findByLabelText('Stage 10 log entry 1 text');
  expect(logText.tagName).toBe('TEXTAREA');
  expect((logText as HTMLTextAreaElement).value).toBe(long);
});
