import { act, cleanup, fireEvent, render, renderHook, screen, waitFor, within } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import QuestEditor from '../QuestEditor';
import type { QuestSnapshot } from './questBackend';
import { openQuest, QUEST, stubBackend } from './questTestHarness';
import { useQuestCheck } from './useQuestCheck';

const scrollIntoView = vi.fn();

const TWO_STAGES = {
  stages: [
    { index: 10, flags: 0, logEntries: [{ logText: 'Find the courier', note: '', flags: 0 }] },
    { index: 20, flags: 0, logEntries: [{ logText: 'Done', note: '', flags: 1 }] },
  ],
};
const BRANCH = '000810:Patch.esp';
const OUT_OF_DATE = 'This list is out of date. Check this quest again.';
const PLUGIN = 'Patch.esp';
const EMPTY_SNAPSHOT: QuestSnapshot = { stages: [], objectives: [], aliases: [], jumps: [], fragmentScripts: [] };
const questRow = (subrecord: string, message: string) =>
  JSON.stringify([{ Code: 'unresolved_link', Severity: 'error', FormKey: QUEST, Subrecord: subrecord, Message: message }]);
const deferredReply = () => {
  let resolve: (raw: string) => void = () => {};
  const promise = new Promise<string>(done => { resolve = done; });
  return { promise, resolve };
};

const checkButton = () => screen.getByRole('button', { name: 'Check this quest' }) as HTMLButtonElement;
const runCheck = async () => {
  await waitFor(() => expect(checkButton().disabled).toBe(false));
  fireEvent.click(checkButton());
};

beforeEach(() => { scrollIntoView.mockClear(); Element.prototype.scrollIntoView = scrollIntoView; });
afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('Detects: Check this quest stays disabled, with the reason shown, until the quest has loaded', async () => {
  stubBackend({ GetQuestStages: vi.fn(() => new Promise<string>(() => {})) });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  expect(checkButton().disabled).toBe(true);
  expect(screen.getByText('Wait for the quest to load.')).toBeTruthy();
});

it('Detects: while the record check runs the panel says it is checking, and the result replaces that text when the check ends', async () => {
  let finish: (raw: string) => void = () => {};
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)),
    CheckForErrors: vi.fn(() => new Promise<string>(resolve => { finish = resolve; })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  expect(await screen.findByText('Checking this quest...')).toBeTruthy();
  finish('[]');
  expect(await screen.findByText('No problems found.')).toBeTruthy();
  expect(screen.queryByText('Checking this quest...')).toBeNull();
});

it('Detects: the counts header matches the list, and errors are listed before warnings even when the record checker reports a warning first', async () => {
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)),
    GetDialogue: vi.fn(async () => JSON.stringify({ branches: [{ formKey: BRANCH }], topics: [] })),
    CheckForErrors: vi.fn(async () => JSON.stringify([
      { Code: 'dialogue_branch_category_invalid', Severity: 'warning', FormKey: BRANCH, Message: 'Branch category is invalid' },
      { Code: 'unresolved_link', Severity: 'error', FormKey: QUEST, Subrecord: 'ALID', Message: 'Alias link is broken' },
    ])),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  const list = await screen.findByRole('list', { name: 'Problems found' });
  expect(screen.getByText('1 error')).toBeTruthy();
  expect(screen.getByText('1 warning')).toBeTruthy();
  expect(within(list).getAllByRole('listitem').map(item => item.querySelector('.quest-check-text')?.textContent))
    .toEqual(['Alias link is broken', 'Branch category is invalid']);
});

it('Detects: a dialogue finding shows its record id as copyable text, so the record can still be found by hand', async () => {
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)),
    GetDialogue: vi.fn(async () => JSON.stringify({ branches: [{ formKey: BRANCH }], topics: [] })),
    CheckForErrors: vi.fn(async () => JSON.stringify([
      { Code: 'dialogue_branch_category_invalid', Severity: 'error', FormKey: BRANCH, Message: 'Branch category is invalid' },
    ])),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  const list = await screen.findByRole('list', { name: 'Problems found' });
  expect(within(list).getByText(BRANCH).tagName).toBe('CODE');
});

it('Detects: a quest with no problems shows the No problems found state with the time it was checked', async () => {
  stubBackend({ GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)) });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  expect(await screen.findByText('No problems found.')).toBeTruthy();
  expect(screen.getByText('0 errors')).toBeTruthy();
  expect(screen.getByText('0 warnings')).toBeTruthy();
  expect(screen.getByText(/^Checked at /)).toBeTruthy();
});

it('Detects: Show stage on a stage finding selects that stage in the flow chart and the stage list', async () => {
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify({ stages: [...TWO_STAGES.stages, { index: 30, flags: 0, logEntries: [] }] })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  fireEvent.click(await screen.findByRole('button', { name: 'Show stage 30' }));
  expect(screen.getByRole('button', { name: /^Stage 30/ }).getAttribute('aria-pressed')).toBe('true');
  expect(screen.getByRole('group', { name: 'Stage 30 flags' }).closest('.quest-card')?.classList.contains('quest-card-selected')).toBe(true);
});

it('Detects: Show the objectives pane on an objective finding moves focus to that pane', async () => {
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)),
    GetQuestObjectives: vi.fn(async () => JSON.stringify({
      objectives: [{ index: 10, flags: 0, displayText: 'Locate the courier', targets: [{ alias: 7, flags: 0, aliasName: '' }] }],
    })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  fireEvent.click(await screen.findByRole('button', { name: 'Show the objectives pane' }));
  expect(document.activeElement?.id).toBe('quest-objectives-title');
});

it('Detects: a rejected record check shows a friendly error with its details, instead of an uncaught exception', async () => {
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)),
    CheckForErrors: vi.fn(async () => { throw new Error('Native check crashed'); }),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  expect(await screen.findByText('Checking this quest failed.')).toBeTruthy();
  expect(screen.getByText('Details: Native check crashed')).toBeTruthy();
  expect(document.querySelector('.quest-check-counts')).toBeNull();
});

it('Detects: a record check reply that is not JSON shows the friendly error and stops the checking text, instead of hanging or crashing', async () => {
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)),
    CheckForErrors: vi.fn(async () => 'not json'),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  expect(await screen.findByText('Checking this quest failed.')).toBeTruthy();
  expect(screen.getByText('Details: The game record check returned a result the editor could not read.')).toBeTruthy();
  expect(screen.queryByText('Checking this quest...')).toBeNull();
});

it('Detects: a saved stage makes the check results out of date, and checking again clears that line', async () => {
  const backend = stubBackend({ GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)) });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  await screen.findByText('No problems found.');
  expect(screen.queryByText(OUT_OF_DATE)).toBeNull();
  const logText = await screen.findByLabelText('Stage 10 log entry 1 text');
  fireEvent.change(logText, { target: { value: 'Find the courier at the gate' } });
  fireEvent.blur(logText);
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  expect(await screen.findByText(OUT_OF_DATE)).toBeTruthy();
  await runCheck();
  await waitFor(() => expect(screen.queryByText(OUT_OF_DATE)).toBeNull());
  expect(screen.getByText('No problems found.')).toBeTruthy();
});

it('Detects: a slow first record check that finishes after a second run started is dropped, so the second run result is the one shown', async () => {
  const first = deferredReply();
  const second = deferredReply();
  stubBackend({ CheckForErrors: vi.fn().mockReturnValueOnce(first.promise).mockReturnValueOnce(second.promise) });
  const { result } = renderHook(() => useQuestCheck(PLUGIN, QUEST, EMPTY_SNAPSHOT));
  act(() => result.current.run());
  act(() => result.current.run());
  await act(async () => { second.resolve(questRow('ALID', 'Second run finding')); });
  await waitFor(() => expect(result.current.result?.findings.map(finding => finding.message)).toEqual(['Second run finding']));
  await act(async () => {
    first.resolve(questRow('ALID', 'First run finding'));
    await new Promise(done => setTimeout(done, 0));
  });
  expect(result.current.result?.findings.map(finding => finding.message)).toEqual(['Second run finding']);
  expect(result.current.running).toBe(false);
});

it('Detects: an edit saved while the record check is still running marks the result out of date when it arrives', async () => {
  let finish: (raw: string) => void = () => {};
  const backend = stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)),
    CheckForErrors: vi.fn(() => new Promise<string>(resolve => { finish = resolve; })),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  expect(await screen.findByText('Checking this quest...')).toBeTruthy();
  const logText = await screen.findByLabelText('Stage 10 log entry 1 text');
  fireEvent.change(logText, { target: { value: 'Find the courier at the gate' } });
  fireEvent.blur(logText);
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  finish('[]');
  expect(await screen.findByText('No problems found.')).toBeTruthy();
  expect(screen.getByText(OUT_OF_DATE)).toBeTruthy();
});

it('Detects: Show the aliases pane on an alias finding moves focus to that pane', async () => {
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)),
    CheckForErrors: vi.fn(async () => questRow('ALID', 'Alias link is broken')),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  fireEvent.click(await screen.findByRole('button', { name: 'Show the aliases pane' }));
  expect(document.activeElement?.id).toBe('quest-aliases-title');
});

it('Detects: Show the fragments pane on a script finding moves focus to that pane', async () => {
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)),
    CheckForErrors: vi.fn(async () => questRow('VMAD', 'Fragment script is broken')),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  fireEvent.click(await screen.findByRole('button', { name: 'Show the fragments pane' }));
  expect(document.activeElement?.id).toBe('quest-fragments-title');
});

it('Detects: a saved objective makes the check results out of date, so the list is not presented as current', async () => {
  const backend = stubBackend({ GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)) });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  await screen.findByText('No problems found.');
  const text = await screen.findByLabelText('Objective 10 text');
  fireEvent.change(text, { target: { value: 'Locate the courier at the gate' } });
  fireEvent.blur(text);
  await waitFor(() => expect(backend.SetQuestObjectives).toHaveBeenCalledTimes(1));
  expect(await screen.findByText(OUT_OF_DATE)).toBeTruthy();
});

it('Detects: a saved alias makes the check results out of date, so the list is not presented as current', async () => {
  const backend = stubBackend({ GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)) });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  await screen.findByText('No problems found.');
  const name = await screen.findByLabelText('Alias 0 name');
  fireEvent.change(name, { target: { value: 'Courier North' } });
  fireEvent.blur(name);
  await waitFor(() => expect(backend.SetQuestAliases).toHaveBeenCalledTimes(1));
  expect(await screen.findByText(OUT_OF_DATE)).toBeTruthy();
});

it('Detects: a written script function makes the check results out of date, because the fragment jumps are read again', async () => {
  const backend = stubBackend({ GetQuestStages: vi.fn(async () => JSON.stringify(TWO_STAGES)) });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await runCheck();
  await screen.findByText('No problems found.');
  fireEvent.change(screen.getByLabelText('Script name'), { target: { value: 'QF_Courier_0010' } });
  fireEvent.change(screen.getByLabelText('Script output folder'), { target: { value: 'C:/Out' } });
  fireEvent.change(screen.getByLabelText('Script imports folder'), { target: { value: 'C:/Imports' } });
  fireEvent.click(screen.getByRole('button', { name: 'Write script function' }));
  await waitFor(() => expect(backend.SetQuestFragment).toHaveBeenCalledTimes(1));
  expect(await screen.findByText(OUT_OF_DATE)).toBeTruthy();
});
