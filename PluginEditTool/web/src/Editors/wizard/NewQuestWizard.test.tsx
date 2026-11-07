import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { stubBackend } from '../quest/questTestHarness';
import EditorsHome from '../EditorsHome';
import { TOUR_STORAGE_KEY } from '../help/useTour';
import NewQuestWizard from './NewQuestWizard';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); localStorage.clear(); });

const CREATED = '000D00:Patch.esp';
const NAME_LABEL = 'What is the quest\'s internal name?';
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
const PLUGINS = [
  { Name: 'Patch.esp', Kind: 'plugin', LoadOrder: 1, Editable: true, Size: 1 },
];

const BRANCH = '000E00:Patch.esp';
const TOPIC = '000F00:Patch.esp';
const INFO = '001000:Patch.esp';
const SCENE = '001100:Patch.esp';

function sceneStubs() {
  return {
    CreateDialogueBranch: vi.fn(async () => JSON.stringify({ formKey: BRANCH })),
    CreateDialogueTopic: vi.fn(async () => JSON.stringify({ formKey: TOPIC })),
    CreateInfo: vi.fn(async () => JSON.stringify({ formKey: INFO })),
    CreateScene: vi.fn(async () => JSON.stringify({ formKey: SCENE })),
    AddScenePhase: vi.fn(async () => 'ok'),
    AddSceneActor: vi.fn(async () => 'ok'),
    SetSceneActor: vi.fn(async () => 'ok'),
    AddSceneAction: vi.fn(async () => 'ok'),
    SetSceneAction: vi.fn(async () => 'ok'),
    SetDialogueBranchStart: vi.fn(async () => 'ok'),
  };
}

type WizardBackend = ReturnType<typeof stubBackend> & {
  CreateRecord: ReturnType<typeof vi.fn>;
  SetField: ReturnType<typeof vi.fn>;
  CreateSeqFile: ReturnType<typeof vi.fn>;
  SetDialogueBranchStart: ReturnType<typeof vi.fn>;
};

function wizardBackend(overrides: Record<string, unknown> = {}): WizardBackend {
  return stubBackend({
    GetActivePlugins: vi.fn(async () => JSON.stringify(PLUGINS)),
    SearchRecords: vi.fn(async (_query: string, typeFilter: string) => JSON.stringify([HITS[typeFilter]])),
    ResolveFormKeyLabels: vi.fn(async (csv: string) => JSON.stringify(Object.fromEntries(csv.split(',').map(key => [key, LABELS[key]])))),
    CreateRecord: vi.fn(async () => JSON.stringify({ formKey: CREATED })),
    SetField: vi.fn(async () => 'ok'),
    CreateSeqFile: vi.fn(async () => 'Wrote Patch.esp.seq.'),
    ...sceneStubs(),
    ...overrides,
  }) as WizardBackend;
}

async function pickRecord(row: string, label: string) {
  fireEvent.click(screen.getByRole('button', { name: 'Choose' }));
  fireEvent.click(await screen.findByText(row));
  await screen.findByText(label);
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
}

async function answerChoiceSteps() {
  fireEvent.click(screen.getByRole('button', { name: /Choice with two endings/ }));
  await screen.findByRole('option', { name: 'Patch.esp' });
  fireEvent.change(screen.getByLabelText('Which plugin should the quest go in?'), { target: { value: 'Patch.esp' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  fireEvent.change(screen.getByLabelText(NAME_LABEL), { target: { value: 'The Raider Deal' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  await pickRecord('CourierBob', 'Courier Bob');
  fireEvent.change(screen.getByLabelText('What is the first choice?'), { target: { value: 'Spare the raiders' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  fireEvent.change(screen.getByLabelText('What is the second choice?'), { target: { value: 'Hand the raiders over' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  fireEvent.change(screen.getByLabelText('What does the player get for the first choice?'), { target: { value: '50 caps' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  fireEvent.change(screen.getByLabelText('What does the player get for the second choice?'), { target: { value: '20 caps' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
}

async function answerEveryStep() {
  fireEvent.click(screen.getByRole('button', { name: /Fetch an item/ }));
  await screen.findByRole('option', { name: 'Patch.esp' });
  fireEvent.change(screen.getByLabelText('Which plugin should the quest go in?'), { target: { value: 'Patch.esp' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  fireEvent.change(screen.getByLabelText(NAME_LABEL), { target: { value: 'The Lost Locket' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  await pickRecord('CourierBob', 'Courier Bob');
  await pickRecord('MillSite', 'Abandoned Mill');
  await pickRecord('RustyLocket', 'Rusty Locket');
  fireEvent.change(screen.getByLabelText('What does the player get as a reward?'), { target: { value: '50 caps' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
}

async function answerTalkSteps() {
  fireEvent.click(screen.getByRole('button', { name: /Talk to someone/ }));
  await screen.findByRole('option', { name: 'Patch.esp' });
  fireEvent.change(screen.getByLabelText('Which plugin should the quest go in?'), { target: { value: 'Patch.esp' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  fireEvent.change(screen.getByLabelText(NAME_LABEL), { target: { value: 'The Old Friend' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  await pickRecord('CourierBob', 'Courier Bob');
  await pickRecord('MillSite', 'Abandoned Mill');
  fireEvent.change(screen.getByLabelText('What does the player get as a reward?'), { target: { value: '50 caps' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
}

it('Detects: finishing the wizard sends CreateRecord, stages, aliases and objectives, then the start-game flag, then the SEQ file, then opens the new quest', async () => {
  const createRecord = vi.fn(async () => JSON.stringify({ formKey: CREATED }));
  const setField = vi.fn(async () => 'ok');
  const backend = wizardBackend({ CreateRecord: createRecord, SetField: setField });
  const onCreated = vi.fn();
  render(<NewQuestWizard onBack={() => {}} onCreated={onCreated} />);
  await answerEveryStep();
  expect(screen.getByText(/sets the quest to start with the game/)).toBeTruthy();
  expect(screen.getByText(/writes the plugin's SEQ file/)).toBeTruthy();
  expect(screen.getByText(/will be duplicated/)).toBeTruthy();
  expect(screen.getByText(/Nothing moves the player past the first stage yet/)).toBeTruthy();
  expect(screen.queryByText(/\(not linked:/)).toBeNull();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  await waitFor(() => expect(onCreated).toHaveBeenCalledWith({ formKey: CREATED, plugin: 'Patch.esp' }));
  expect(createRecord).toHaveBeenCalledWith('Patch.esp', 'QUST', { schema: true, editorId: 'The_Lost_Locket' });
  expect(setField).toHaveBeenCalledWith('Patch.esp', CREATED, 'QUST\\Flags', '1');
  expect(backend.CreateSeqFile).toHaveBeenCalledWith('Patch.esp', '');
  expect(backend.SetQuestStages).toHaveBeenCalledWith('Patch.esp', CREATED, expect.stringContaining('Go to Abandoned Mill and find Rusty Locket.'));
  expect(backend.SetQuestAliases).toHaveBeenCalledWith('Patch.esp', CREATED, expect.stringContaining('"uniqueActor":"000900:Patch.esp"'));
  expect(backend.SetQuestAliases).toHaveBeenCalledWith('Patch.esp', CREATED, expect.stringContaining('"specificLocation":"000A00:Patch.esp"'));
  expect(backend.SetQuestAliases).toHaveBeenCalledWith('Patch.esp', CREATED, expect.stringContaining('"createReference":{"object":"000B00:Patch.esp","alias":2,"create":"At"}'));
  const [create, stages, aliases, objectives, flags, seq] = [
    createRecord.mock.invocationCallOrder[0],
    backend.SetQuestStages.mock.invocationCallOrder[0],
    backend.SetQuestAliases.mock.invocationCallOrder[0],
    backend.SetQuestObjectives.mock.invocationCallOrder[0],
    setField.mock.invocationCallOrder[0],
    backend.CreateSeqFile.mock.invocationCallOrder[0],
  ];
  expect(create).toBeLessThan(stages);
  expect(stages).toBeLessThan(aliases);
  expect(aliases).toBeLessThan(objectives);
  expect(objectives).toBeLessThan(flags);
  expect(flags).toBeLessThan(seq);
});

it('Detects: when the SEQ file cannot be written after the quest is made, the wizard says so and keeps the made quest, instead of opening it silently', async () => {
  const backend = wizardBackend({
    CreateSeqFile: vi.fn(async () => { throw new Error('Plugin folder is read-only.'); }),
  });
  const onCreated = vi.fn();
  render(<NewQuestWizard onBack={() => {}} onCreated={onCreated} />);
  await answerEveryStep();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  const alert = await screen.findByRole('alert');
  expect(alert.textContent).toContain('The quest was made, but its SEQ file was not written.');
  expect(alert.textContent).toContain('Create SEQ file');
  expect(onCreated).not.toHaveBeenCalled();
  expect(backend.SetField).toHaveBeenCalledWith('Patch.esp', CREATED, 'QUST\\Flags', '1');
  fireEvent.click(screen.getByRole('button', { name: 'Open the quest anyway' }));
  expect(onCreated).toHaveBeenCalledWith({ formKey: CREATED, plugin: 'Patch.esp' });
});

it('Detects: a quest name that would give an editor ID starting with a digit cannot pass the name step, so the error shows before any call is made', async () => {
  const backend = wizardBackend();
  render(<NewQuestWizard onBack={() => {}} onCreated={vi.fn()} />);
  fireEvent.click(screen.getByRole('button', { name: /Fetch an item/ }));
  await screen.findByRole('option', { name: 'Patch.esp' });
  fireEvent.change(screen.getByLabelText('Which plugin should the quest go in?'), { target: { value: 'Patch.esp' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  fireEvent.change(screen.getByLabelText(NAME_LABEL), { target: { value: '2 Lost Locket' } });
  expect(screen.getByText('The quest name cannot start with a number.')).toBeTruthy();
  expect((screen.getByRole('button', { name: 'Next' }) as HTMLButtonElement).disabled).toBe(true);
  fireEvent.change(screen.getByLabelText(NAME_LABEL), { target: { value: '!!!' } });
  expect(screen.getByText('The quest name needs at least one letter or number.')).toBeTruthy();
  expect(backend.CreateRecord).not.toHaveBeenCalled();
});

it.each(['CreateDialogueTopic', 'CreateInfo', 'CreateScene'] as const)(
  'Detects: when %s fails after the quest was made, the wizard shows the game message, says part of the quest was made, keeps the made key out of the text, and never sets the start-game flag',
  async (method) => {
    const backend = wizardBackend({
      [method]: vi.fn(async () => { throw new Error('Localized plugin refuses this record.'); }),
    });
    const onCreated = vi.fn();
    render(<NewQuestWizard onBack={() => {}} onCreated={onCreated} />);
    await answerTalkSteps();
    fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
    const alert = await screen.findByRole('alert');
    expect(alert.textContent).toContain('Localized plugin refuses this record.');
    expect(alert.textContent).toContain('Part of the quest was made');
    expect(alert.textContent).not.toContain(CREATED);
    expect(backend.SetQuestObjectives).toHaveBeenCalled();
    expect(backend.SetField).not.toHaveBeenCalled();
    expect(backend.CreateSeqFile).not.toHaveBeenCalled();
    expect(onCreated).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Open the quest anyway' }));
    expect(onCreated).toHaveBeenCalledWith({ formKey: CREATED, plugin: 'Patch.esp' });
  },
);

it('Detects: when the objectives call fails, the wizard stops, says part of the quest was made, and does not show the form key', async () => {
  wizardBackend({
    SetQuestObjectives: vi.fn(async () => { throw new Error('duplicate objective index'); }),
  });
  const onCreated = vi.fn();
  render(<NewQuestWizard onBack={() => {}} onCreated={onCreated} />);
  await answerEveryStep();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  const alert = await screen.findByRole('alert');
  expect(alert.textContent).toContain('Part of the quest was made');
  expect(alert.textContent).not.toContain(CREATED);
  expect(onCreated).not.toHaveBeenCalled();
  expect(screen.queryByRole('button', { name: 'Create quest' })).toBeNull();
  expect(screen.getByRole('button', { name: 'Open the quest anyway' })).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Previous' }));
  fireEvent.change(screen.getByLabelText('What does the player get as a reward?'), { target: { value: '100 caps' } });
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  expect(screen.queryByRole('button', { name: 'Create quest' })).toBeNull();
  expect(screen.getByRole('button', { name: 'Open the quest anyway' })).toBeTruthy();
});

it('Detects: when the aliases call fails, no objectives call is sent, because the game rejects objective targets that are not aliases yet', async () => {
  const backend = wizardBackend({
    SetQuestAliases: vi.fn(async () => { throw new Error('alias write failed'); }),
  });
  render(<NewQuestWizard onBack={() => {}} onCreated={vi.fn()} />);
  await answerEveryStep();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  expect((await screen.findByRole('alert')).textContent).toContain('Part of the quest was made');
  expect(backend.SetQuestStages).toHaveBeenCalledTimes(1);
  expect(backend.SetQuestObjectives).not.toHaveBeenCalled();
});

it('Detects: when CreateRecord fails, no stage, alias or objective call is sent and Create quest stays available to retry', async () => {
  const backend = wizardBackend({
    CreateRecord: vi.fn(async () => { throw new Error('Editor ID already exists.'); }),
  });
  render(<NewQuestWizard onBack={() => {}} onCreated={vi.fn()} />);
  await answerEveryStep();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  const alert = await screen.findByRole('alert');
  expect(alert.textContent).toContain('Editor ID already exists.');
  expect(alert.textContent).not.toContain('Part of the quest was made');
  expect(backend.SetQuestStages).not.toHaveBeenCalled();
  expect(screen.getByRole('button', { name: 'Create quest' })).toBeTruthy();
});

it('Detects: when CreateRecord returns no form key, the wizard stops before any setter and says the game did not confirm the quest', async () => {
  const backend = wizardBackend({
    CreateRecord: vi.fn(async () => JSON.stringify({})),
  });
  render(<NewQuestWizard onBack={() => {}} onCreated={vi.fn()} />);
  await answerEveryStep();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  const alert = await screen.findByRole('alert');
  expect(alert.textContent).toContain('The game did not confirm that the quest was made.');
  expect(alert.textContent).not.toContain('Part of the quest was made');
  expect(backend.SetQuestStages).not.toHaveBeenCalled();
});

it('Detects: a plugin that is not open for editing is not offered as the place for the new quest', async () => {
  wizardBackend({
    GetActivePlugins: vi.fn(async () => JSON.stringify([
      ...PLUGINS,
      { Name: 'Base.esp', Kind: 'plugin', LoadOrder: 2, Editable: false, Size: 1 },
    ])),
  });
  render(<NewQuestWizard onBack={() => {}} onCreated={vi.fn()} />);
  fireEvent.click(screen.getByRole('button', { name: /Fetch an item/ }));
  await screen.findByRole('option', { name: 'Patch.esp' });
  expect(screen.queryByRole('option', { name: 'Base.esp' })).toBeNull();
});

it('Detects: going Back from the quest the wizard just made, then choosing the Quest card, opens an empty editor instead of that quest', async () => {
  localStorage.setItem(TOUR_STORAGE_KEY, 'done');
  wizardBackend();
  render(<EditorsHome onClose={() => {}} />);
  fireEvent.click(screen.getByRole('button', { name: /^New quest from template/ }));
  await answerEveryStep();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  await screen.findByText(`Editing ${CREATED}`);
  fireEvent.click(screen.getByRole('button', { name: 'Back' }));
  fireEvent.click(screen.getByRole('button', { name: /Stages, objectives and aliases for one quest/ }));
  expect(await screen.findByText('Choose a quest to start editing it. Nothing changes until you edit a field.')).toBeTruthy();
  expect(screen.queryByText(`Editing ${CREATED}`)).toBeNull();
});

it('Detects: the template chooser dropping, renaming or reordering one of the five quest templates, so a beginner cannot start that kind of quest', () => {
  wizardBackend();
  const { container } = render(<NewQuestWizard onBack={() => {}} onCreated={vi.fn()} />);
  const titles = Array.from(container.querySelectorAll('.editors-card-title'), node => node.textContent);
  expect(titles).toEqual(['Fetch an item', 'Kill a target', 'Talk to someone', 'Escort', 'Choice with two endings']);
});

it('Detects: finishing Talk to someone making the dialogue and scene in an order where the scene action links to a topic that is not made yet', async () => {
  const stubs = sceneStubs();
  const createRecord = vi.fn(async () => JSON.stringify({ formKey: CREATED }));
  const backend = wizardBackend({ CreateRecord: createRecord, ...stubs });
  const onCreated = vi.fn();
  render(<NewQuestWizard onBack={() => {}} onCreated={onCreated} />);
  await answerTalkSteps();
  expect(screen.getByText('A scene with 2 parts: Meet, then Talk.')).toBeTruthy();
  expect(screen.getByText('Courier Bob speaks in the Talk part.')).toBeTruthy();
  expect(screen.queryByText(/will be duplicated/)).toBeNull();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  await waitFor(() => expect(onCreated).toHaveBeenCalledWith({ formKey: CREATED, plugin: 'Patch.esp' }));
  const order = (mock: { mock: { invocationCallOrder: number[] } }) => mock.mock.invocationCallOrder[0];
  expect(order(createRecord)).toBeLessThan(order(stubs.CreateDialogueBranch));
  expect(order(stubs.CreateDialogueBranch)).toBeLessThan(order(stubs.CreateDialogueTopic));
  expect(order(stubs.CreateDialogueTopic)).toBeLessThan(order(stubs.CreateInfo));
  expect(order(stubs.CreateInfo)).toBeLessThan(order(stubs.CreateScene));
  expect(order(stubs.CreateScene)).toBeLessThan(order(stubs.AddScenePhase));
  expect(order(stubs.CreateDialogueTopic)).toBeLessThan(order(stubs.SetSceneAction));
  expect(stubs.CreateDialogueTopic).toHaveBeenCalledWith('Patch.esp', CREATED, expect.objectContaining({ branch: BRANCH, name: 'Are you Courier Bob? I was sent to speak with you.' }));
  expect(stubs.SetSceneAction).toHaveBeenCalledWith('Patch.esp', SCENE, 0, { actorAlias: 0, startPhase: 1, endPhase: 1, topic: TOPIC });
  expect(backend.SetQuestObjectives).toHaveBeenCalledWith('Patch.esp', CREATED, expect.stringContaining('"displayText":"Talk to Courier Bob at Abandoned Mill"'));
});

it('Detects: finishing Fetch an item sending any scene or dialogue call, because that template has no skeleton and must make only the quest', async () => {
  const stubs = sceneStubs();
  wizardBackend(stubs);
  const onCreated = vi.fn();
  render(<NewQuestWizard onBack={() => {}} onCreated={onCreated} />);
  await answerEveryStep();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  await waitFor(() => expect(onCreated).toHaveBeenCalledWith({ formKey: CREATED, plugin: 'Patch.esp' }));
  for (const mock of Object.values(stubs)) expect(mock).not.toHaveBeenCalled();
});

it('Detects: finishing Choice with two endings with the review still saying the dialogue is not linked, or with the choice calls out of order, so the player could never reach stage 30 or 40', async () => {
  const stubs = sceneStubs();
  const createRecord = vi.fn(async () => JSON.stringify({ formKey: CREATED }));
  const backend = wizardBackend({ CreateRecord: createRecord, ...stubs });
  const onCreated = vi.fn();
  render(<NewQuestWizard onBack={() => {}} onCreated={onCreated} />);
  await answerChoiceSteps();
  expect(screen.getByText('The player can say: "Spare the raiders" or "Hand the raiders over".')).toBeTruthy();
  expect(screen.getByText('Courier Bob answers, and each choice ends the quest at stage 30 or 40.')).toBeTruthy();
  expect(screen.getByText(/Each choice is offered only by Courier Bob, and only while the quest is at a stage from 10 to 20/)).toBeTruthy();
  expect(screen.getByText(/The choices set stage 30 or 40, which finishes the quest\. They are offered from stage 10, so the quest works as soon as it starts\. Nothing in the wizard moves the quest through any other stage\./)).toBeTruthy();
  expect(screen.queryByText(/Nothing moves the player past the first stage yet/)).toBeNull();
  expect(screen.queryByText(/will be duplicated/)).toBeNull();
  expect(screen.queryByText(/the first stage runs when the quest starts/)).toBeNull();
  expect(screen.queryByText(/dialogue is not linked to a character/)).toBeNull();
  fireEvent.click(screen.getByRole('button', { name: 'Create quest' }));
  await waitFor(() => expect(onCreated).toHaveBeenCalledWith({ formKey: CREATED, plugin: 'Patch.esp' }));
  const order = (mock: { mock: { invocationCallOrder: number[] } }) => mock.mock.invocationCallOrder[0];
  expect(order(createRecord)).toBeLessThan(order(stubs.CreateDialogueBranch));
  expect(order(stubs.CreateDialogueBranch)).toBeLessThan(order(stubs.CreateDialogueTopic));
  expect(order(stubs.CreateDialogueTopic)).toBeLessThan(order(stubs.CreateInfo));
  expect(order(stubs.CreateInfo)).toBeLessThan(order(stubs.SetDialogueBranchStart));
  expect(backend.SetField.mock.invocationCallOrder[0]).toBeGreaterThan(stubs.SetDialogueBranchStart.mock.invocationCallOrder[1]);
  expect(backend.SetField).toHaveBeenCalledWith('Patch.esp', CREATED, 'QUST\\Flags', '1');
  expect(backend.CreateSeqFile.mock.invocationCallOrder[0]).toBeGreaterThan(backend.SetField.mock.invocationCallOrder[0]);
  expect(stubs.CreateDialogueBranch).toHaveBeenCalledTimes(2);
  expect(stubs.CreateDialogueTopic).toHaveBeenNthCalledWith(1, 'Patch.esp', CREATED, expect.objectContaining({ name: 'Spare the raiders', branch: BRANCH }));
  expect(stubs.CreateDialogueTopic).toHaveBeenNthCalledWith(2, 'Patch.esp', CREATED, expect.objectContaining({ name: 'Hand the raiders over', branch: BRANCH }));
  expect(stubs.CreateInfo).toHaveBeenNthCalledWith(1, 'Patch.esp', TOPIC, expect.objectContaining({ responses: [{ text: 'Then it is done. Your reward is 50 caps.' }], setStageOnEnd: 30 }));
  expect(stubs.CreateInfo).toHaveBeenNthCalledWith(2, 'Patch.esp', TOPIC, expect.objectContaining({ responses: [{ text: 'Then it is done. Your reward is 20 caps.' }], setStageOnEnd: 40 }));
  expect(stubs.SetDialogueBranchStart).toHaveBeenNthCalledWith(1, 'Patch.esp', BRANCH, TOPIC);
  expect(stubs.SetDialogueBranchStart).toHaveBeenNthCalledWith(2, 'Patch.esp', BRANCH, TOPIC);
  expect(stubs.CreateScene).not.toHaveBeenCalled();
  expect(backend.SetQuestObjectives).toHaveBeenCalledWith('Patch.esp', CREATED, expect.stringContaining('"displayText":"Choose: Spare the raiders or Hand the raiders over"'));
});
