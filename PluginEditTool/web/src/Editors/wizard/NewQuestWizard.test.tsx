import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { stubBackend } from '../quest/questTestHarness';
import NewQuestWizard from './NewQuestWizard';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

const CREATED = '000D00:Patch.esp';
const HITS: Record<string, object> = {
  NPC_: { FormKey: '000900:Patch.esp', EditorID: 'CourierBob', Type: 'NPC_', Plugin: 'Patch.esp' },
  LCTN: { FormKey: '000A00:Patch.esp', EditorID: 'MillSite', Type: 'LCTN', Plugin: 'Patch.esp' },
  MISC: { FormKey: '000B00:Patch.esp', EditorID: 'RustyLocket', Type: 'MISC', Plugin: 'Patch.esp' },
};
const LABELS: Record<string, string> = {
  '000900:Patch.esp': 'Courier Bob',
  '000A00:Patch.esp': 'Abandoned Mill',
  '000B00:Patch.esp': 'Rusty Locket',
};

function wizardBackend(overrides: Record<string, unknown> = {}) {
  return stubBackend({
    GetActivePlugins: vi.fn(async () => JSON.stringify([{ Name: 'Patch.esp', Kind: 'plugin', LoadOrder: 1, Editable: true, Size: 1 }])),
    SearchRecords: vi.fn(async (_query: string, typeFilter: string) => JSON.stringify([HITS[typeFilter]])),
    ResolveFormKeyLabels: vi.fn(async (csv: string) => JSON.stringify(Object.fromEntries(csv.split(',').map(key => [key, LABELS[key]])))),
    CreateRecord: vi.fn(async () => JSON.stringify({ formKey: CREATED })),
    ...overrides,
  });
}

async function pickRecord(row: string, label: string) {
  fireEvent.click(screen.getByRole('button', { name: 'Choose' }));
  fireEvent.click(await screen.findByText(row));
  await screen.findByText(label);
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
}

async function answerEveryStep() {
  fireEvent.click(screen.getByRole('button', { name: /Fetch an item/ }));
  await screen.findByRole('option', { name: 'Patch.esp' });
  fireEvent.change(screen.getByLabelText('Which plugin should the quest go in?'), { target: { value: 'Patch.esp' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  fireEvent.change(screen.getByLabelText('What is the quest called?'), { target: { value: 'The Lost Locket' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  await pickRecord('CourierBob', 'Courier Bob');
  await pickRecord('MillSite', 'Abandoned Mill');
  await pickRecord('RustyLocket', 'Rusty Locket');
  fireEvent.change(screen.getByLabelText('What does the player get as a reward?'), { target: { value: '50 caps' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
}

it('Detects: finishing the wizard sends CreateRecord, then stages, objectives and aliases in that order, then opens the new quest', async () => {
  const createRecord = vi.fn(async () => JSON.stringify({ formKey: CREATED }));
  const backend = wizardBackend({ CreateRecord: createRecord });
  const onCreated = vi.fn();
  render(<NewQuestWizard onBack={() => {}} onCreated={onCreated} />);
  await answerEveryStep();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  await waitFor(() => expect(onCreated).toHaveBeenCalledWith({ formKey: CREATED, plugin: 'Patch.esp' }));
  expect(createRecord).toHaveBeenCalledWith('Patch.esp', 'QUST', { schema: true, editorId: 'The_Lost_Locket' });
  expect(backend.SetQuestStages).toHaveBeenCalledWith('Patch.esp', CREATED, expect.stringContaining('Go to Abandoned Mill and find Rusty Locket.'));
  const [create, stages, objectives, aliases] = [
    createRecord.mock.invocationCallOrder[0],
    backend.SetQuestStages.mock.invocationCallOrder[0],
    backend.SetQuestObjectives.mock.invocationCallOrder[0],
    backend.SetQuestAliases.mock.invocationCallOrder[0],
  ];
  expect(create).toBeLessThan(stages);
  expect(stages).toBeLessThan(objectives);
  expect(objectives).toBeLessThan(aliases);
});

it('Detects: when the objectives call fails, the wizard stops, names the half-made quest, and sends no aliases', async () => {
  const backend = wizardBackend({
    SetQuestObjectives: vi.fn(async () => { throw new Error('duplicate objective index'); }),
  });
  const onCreated = vi.fn();
  render(<NewQuestWizard onBack={() => {}} onCreated={onCreated} />);
  await answerEveryStep();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  const alert = await screen.findByRole('alert');
  expect(alert.textContent).toContain(CREATED);
  expect(backend.SetQuestAliases).not.toHaveBeenCalled();
  expect(onCreated).not.toHaveBeenCalled();
  expect(screen.queryByRole('button', { name: 'Create quest' })).toBeNull();
  expect(screen.getByRole('button', { name: 'Open the quest anyway' })).toBeTruthy();
});

it('Detects: when CreateRecord fails, no stage, objective or alias call is sent and Create quest stays available to retry', async () => {
  const backend = wizardBackend({
    CreateRecord: vi.fn(async () => { throw new Error('Editor ID already exists.'); }),
  });
  render(<NewQuestWizard onBack={() => {}} onCreated={vi.fn()} />);
  await answerEveryStep();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  const alert = await screen.findByRole('alert');
  expect(alert.textContent).toContain('Editor ID already exists.');
  expect(alert.textContent).not.toContain('was made');
  expect(backend.SetQuestStages).not.toHaveBeenCalled();
  expect(screen.getByRole('button', { name: 'Create quest' })).toBeTruthy();
});
