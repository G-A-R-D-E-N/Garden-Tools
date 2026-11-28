import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import QuestEditor from '../QuestEditor';
import QuestWorkspace from './QuestWorkspace';
import { openQuest, openTab, QUEST, stubBackend } from './questTestHarness';

const scrollIntoView = vi.fn();

const THREE_STAGES = {
  stages: [
    { index: 10, flags: 0, logEntries: [{ logText: 'Find the courier', note: '', flags: 0 }] },
    { index: 20, flags: 0, logEntries: [{ logText: 'Talk to her', note: '', flags: 1 }] },
    { index: 30, flags: 0, logEntries: [{ logText: 'Leave', note: '', flags: 0 }] },
  ],
};

const tabCount = (name: RegExp) => screen.getByRole('tab', { name }).querySelector('.editor-tab-count')?.textContent ?? null;

beforeEach(() => { scrollIntoView.mockClear(); Element.prototype.scrollIntoView = scrollIntoView; });
afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('Detects: switching tabs shows only the chosen section, and the other sections are hidden', async () => {
  stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await screen.findByRole('button', { name: /^Stage 10/ });
  expect(screen.getByRole('tabpanel', { name: /^Overview/ }).querySelector('.quest-flow')).toBeTruthy();
  expect(screen.getAllByRole('tabpanel')).toHaveLength(1);
  openTab('Objectives');
  const objectives = screen.getByRole('tabpanel', { name: /^Objectives/ });
  expect(await within(objectives).findByLabelText('Objective 10 text')).toBeTruthy();
  expect(screen.queryByRole('tabpanel', { name: /^Overview/ })).toBeNull();
  expect(screen.queryByRole('button', { name: 'Hide chart' })).toBeNull();
  openTab('Aliases');
  expect(await screen.findByRole('button', { name: 'Add alias' })).toBeTruthy();
  expect(screen.queryByRole('button', { name: 'Add objective' })).toBeNull();
  openTab('Fragments');
  expect(screen.getByRole('tabpanel', { name: /^Fragments/ })).toBeTruthy();
  expect(screen.queryByRole('tabpanel', { name: /^Aliases/ })).toBeNull();
});

it('Detects: Edit in Stages on the chart detail panel opens the Stages tab with that stage selected and scrolled into view', async () => {
  stubBackend({ GetQuestStages: vi.fn(async () => JSON.stringify(THREE_STAGES)) });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  fireEvent.click(await screen.findByRole('button', { name: /^Stage 20/ }));
  expect(screen.getByRole('tab', { name: /^Overview/ }).getAttribute('aria-selected')).toBe('true');
  expect(scrollIntoView).not.toHaveBeenCalled();
  fireEvent.click(within(screen.getByRole('complementary', { name: 'Stage 20 details' })).getByRole('button', { name: 'Edit in Stages' }));
  const stages = screen.getByRole('tabpanel', { name: /^Stages/ });
  expect(screen.getByRole('tab', { name: /^Stages/ }).getAttribute('aria-selected')).toBe('true');
  expect(within(stages).getByRole('group', { name: 'Stage 20 flags' }).closest('.quest-card')?.classList.contains('quest-card-selected')).toBe(true);
  expect(within(stages).getByRole('group', { name: 'Stage 10 flags' }).closest('.quest-card')?.classList.contains('quest-card-selected')).toBe(false);
  expect(scrollIntoView).toHaveBeenCalledTimes(1);
});

it('Detects: the tab counts reflect the stages, objectives and aliases of the quest, and Fragments has no count', async () => {
  stubBackend({ GetQuestStages: vi.fn(async () => JSON.stringify(THREE_STAGES)) });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await waitFor(() => expect(tabCount(/^Stages/)).toBe('3'));
  expect(tabCount(/^Objectives/)).toBe('2');
  expect(tabCount(/^Aliases/)).toBe('1');
  expect(tabCount(/^Fragments/)).toBeNull();
  expect(screen.getByRole('tab', { name: /^Overview/ }).querySelector('.editor-tab-count')).toBeNull();
});

it('Detects: Overview shows a problem badge and a problem count once a check finds problems', async () => {
  stubBackend({
    GetQuestStages: vi.fn(async () => JSON.stringify(THREE_STAGES)),
    CheckQuest: vi.fn(async () => JSON.stringify([
      { Code: 'unresolved_link', Severity: 'error', FormKey: QUEST, Subrecord: 'ALID', Message: 'Alias link is broken' },
    ])),
  });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  expect(screen.getByRole('tab', { name: /^Overview/ }).textContent).not.toContain('problem');
  await waitFor(() => expect((screen.getByRole('button', { name: 'Check this quest' }) as HTMLButtonElement).disabled).toBe(false));
  fireEvent.click(screen.getByRole('button', { name: 'Check this quest' }));
  expect(await screen.findByText(/^1 problem found$/)).toBeTruthy();
  expect(screen.getByRole('tab', { name: /^Overview/ }).textContent).toContain('problem');
  expect(screen.getByLabelText('Quest summary').textContent).toContain('3 stages');
});

it('Detects: the chosen tab stays selected after an edit, and a different quest starts on Overview', async () => {
  const backend = stubBackend({ GetQuestStages: vi.fn(async () => JSON.stringify(THREE_STAGES)) });
  const view = render(<QuestWorkspace key="first" formKey={QUEST} plugin="Patch.esp" />);
  await screen.findByText(`Editing ${QUEST}`);
  openTab('Stages');
  const logText = await screen.findByLabelText('Stage 10 log entry 1 text');
  fireEvent.change(logText, { target: { value: 'Find the courier at the gate' } });
  fireEvent.blur(logText);
  await waitFor(() => expect(backend.SetQuestStages).toHaveBeenCalledTimes(1));
  expect(screen.getByRole('tab', { name: /^Stages/ }).getAttribute('aria-selected')).toBe('true');
  view.rerender(<QuestWorkspace key="second" formKey="000900:Patch.esp" plugin="Patch.esp" />);
  await screen.findByText('Editing 000900:Patch.esp');
  expect(screen.getByRole('tab', { name: /^Overview/ }).getAttribute('aria-selected')).toBe('true');
});

it('Detects: Check this quest started from another tab switches to Overview, where the check results appear', async () => {
  stubBackend({ GetQuestStages: vi.fn(async () => JSON.stringify(THREE_STAGES)) });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await screen.findByRole('button', { name: /^Stage 10/ });
  openTab('Stages');
  await waitFor(() => expect((screen.getByRole('button', { name: 'Check this quest' }) as HTMLButtonElement).disabled).toBe(false));
  fireEvent.click(screen.getByRole('button', { name: 'Check this quest' }));
  expect(screen.getByRole('tab', { name: /^Overview/ }).getAttribute('aria-selected')).toBe('true');
  expect(await screen.findByText('No problems found.')).toBeTruthy();
});
