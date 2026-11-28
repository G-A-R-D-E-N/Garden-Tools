import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import DialogueEditor from '../DialogueEditor';

const QUEST = '000800:Patch.esp';
const OTHER_QUEST = '000900:Patch.esp';
const BRANCH = '000801:Patch.esp';
const TOPIC = '000802:Patch.esp';
const INFO = '000803:Patch.esp';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

function line(number: number, text: string) {
  return { text, number, emotion: null, sound: null, interruptPercent: 0, cameraTargetAlias: -1, cameraLocationAlias: -1 };
}

function extraTopic(i: number) {
  return {
    formKey: `${String(10000 + i).padStart(6, '0')}:Patch.esp`, editorId: `Topic${i}`, priority: 50, branch: BRANCH,
    name: `Line ${i}`, infos: [],
  };
}

const NEW_TOPIC = '000990:Patch.esp';

function setUpBackend(lines: string[], extraTopics = 0, editable = '["Patch.esp"]') {
  const state = { lines, created: [] as ReturnType<typeof extraTopic>[] };
  const dialogue = () => ({
    branches: [{ formKey: BRANCH, editorId: 'MainBranch', topic: null }],
    topics: [{
      formKey: TOPIC, editorId: 'GreetTopic', priority: 50, branch: BRANCH, name: 'Hello there',
      infos: [{
        formKey: INFO, flags: 0, previous: null, conditions: [], fragments: [],
        responses: state.lines.map((text, i) => line(i + 1, text)),
      }],
    }, ...Array.from({ length: extraTopics }, (_, i) => extraTopic(i + 1)), ...state.created],
  });
  const setInfo = vi.fn(async (_plugin: string, _info: string, spec: { responses: { text: string }[] }) => {
    state.lines = spec.responses.map(r => r.text);
    return 'ok';
  });
  const createTopic = vi.fn(async (_plugin: string, _quest: string, spec: { editorId: string; name: string; branch?: string }) => {
    state.created.push({ ...extraTopic(0), formKey: NEW_TOPIC, editorId: spec.editorId, name: spec.name, branch: spec.branch ?? BRANCH });
    return JSON.stringify({ formKey: NEW_TOPIC });
  });
  const backend = {
    SearchRecords: vi.fn(async () => JSON.stringify([
      { FormKey: QUEST, EditorID: 'MQ101', Type: 'QUST', Plugin: 'Patch.esp' },
      { FormKey: OTHER_QUEST, EditorID: 'MQ102', Type: 'QUST', Plugin: 'Patch.esp' },
    ])),
    GetDialogue: vi.fn(async () => JSON.stringify(dialogue())),
    SetInfo: setInfo,
    CreateDialogueTopic: createTopic,
    GetEditablePlugins: vi.fn(async () => editable),
  };
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend } } });
  return { setInfo, backend };
}

async function openQuest() {
  render(<DialogueEditor onBack={() => {}} />);
  fireEvent.click(screen.getByRole('button', { name: 'Pick a quest' }));
  fireEvent.click(await screen.findByText('MQ101'));
  await screen.findByRole('searchbox', { name: 'Search topics' });
}

async function openTopic() {
  await openQuest();
  fireEvent.click(screen.getByRole('button', { name: /Hello there/ }));
}

it('Detects: a game-file dialogue shows the banner without Copy as override and disables the line editor, while topic navigation stays enabled', async () => {
  const { backend } = setUpBackend(['First line', 'Second line', 'Third line'], 0, '["Other.esp"]');
  await openTopic();
  fireEvent.click(screen.getByRole('button', { name: /First line/ }));
  expect(await screen.findByText(/Its dialogue stays readable here/)).toBeTruthy();
  await waitFor(() => expect(backend.GetEditablePlugins).toHaveBeenCalled());
  expect(screen.queryByRole('button', { name: 'Copy as override' })).toBeNull();
  expect((screen.getByLabelText('Line text') as HTMLTextAreaElement).matches(':disabled')).toBe(true);
  expect(screen.getByRole('button', { name: 'Save line' }).matches(':disabled')).toBe(true);
  expect((screen.getByLabelText('New NPC line') as HTMLTextAreaElement).matches(':disabled')).toBe(true);
  fireEvent.click(screen.getByRole('tab', { name: /^Topics/ }));
  expect(screen.getByRole('tab', { name: /^Topics/ }).matches(':disabled')).toBe(false);
  expect(screen.getByRole('button', { name: /Hello there/ }).matches(':disabled')).toBe(false);
  expect(screen.getByRole('button', { name: 'Make the selected topic the start' }).matches(':disabled')).toBe(true);
});

it('Detects: a writable dialogue shows no read-only banner and keeps the line editor enabled', async () => {
  const { backend } = setUpBackend(['First line', 'Second line', 'Third line']);
  await openTopic();
  fireEvent.click(screen.getByRole('button', { name: /First line/ }));
  await waitFor(() => expect(backend.GetEditablePlugins).toHaveBeenCalled());
  await act(async () => { await new Promise(resolve => setTimeout(resolve, 0)); });
  expect(screen.queryByText(/is a game file and cannot be changed/)).toBeNull();
  expect((screen.getByLabelText('Line text') as HTMLTextAreaElement).matches(':disabled')).toBe(false);
});

it('Detects: creating a topic selects it and jumps the list to the page that holds it', async () => {
  setUpBackend(['First line'], 120);
  await openQuest();
  fireEvent.click(screen.getByRole('button', { name: 'New topic' }));
  fireEvent.change(screen.getByLabelText('Player line'), { target: { value: 'Brand new line' } });
  fireEvent.change(screen.getByLabelText('Editor ID'), { target: { value: 'BrandNew' } });
  fireEvent.change(screen.getByLabelText('Branch'), { target: { value: BRANCH } });
  fireEvent.click(screen.getByRole('button', { name: 'Create topic' }));
  expect(await screen.findByText('Page 3 of 3')).toBeTruthy();
  expect(screen.getByRole('button', { name: /Brand new line/ }).getAttribute('aria-pressed')).toBe('true');
});

it('Detects: collapsing a branch removes its topics from the page count', async () => {
  setUpBackend(['First line'], 120);
  await openQuest();
  expect(screen.getByRole('button', { name: 'Next' })).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: /MainBranch/ }));
  expect(screen.queryByRole('button', { name: 'Next' })).toBeNull();
  expect(screen.getByRole('button', { name: /MainBranch/ }).getAttribute('aria-expanded')).toBe('false');
});

it('Detects: leaving the Details tab disarms the remove-reply confirmation', async () => {
  setUpBackend(['First line', 'Second line', 'Third line']);
  await openTopic();
  fireEvent.click(screen.getByRole('button', { name: /First line/ }));
  fireEvent.click(screen.getByRole('button', { name: 'Remove reply' }));
  expect(screen.getByRole('button', { name: 'Click again to remove this reply' })).toBeTruthy();
  fireEvent.click(screen.getByRole('tab', { name: /^Topics/ }));
  fireEvent.click(screen.getByRole('tab', { name: /^Details/ }));
  expect(screen.getByRole('button', { name: 'Remove reply' })).toBeTruthy();
});

it('shows the responses of the selected topic in order. Detects: chat lines shown out of order or dropped', async () => {
  setUpBackend(['First line', 'Second line', 'Third line']);
  await openTopic();
  const chat = screen.getByRole('region', { name: 'Conversation' });
  const spoken = within(chat).getAllByRole('listitem')
    .map(item => item.textContent?.match(/(First|Second|Third) line/)?.[0]);
  expect(spoken).toEqual(['First line', 'Second line', 'Third line']);
});

it('edits a line, then undo restores the old text through SetInfo. Detects: undo that keeps the edited text or sends back the wrong text', async () => {
  const { setInfo } = setUpBackend(['First line', 'Second line', 'Third line']);
  await openTopic();
  fireEvent.click(screen.getByRole('button', { name: /First line/ }));
  fireEvent.change(screen.getByLabelText('Line text'), { target: { value: 'Changed line' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save line' }));
  await waitFor(() => expect(setInfo).toHaveBeenCalledTimes(1));
  expect(setInfo.mock.calls[0][2].responses[0].text).toBe('Changed line');
  const screenRegion = screen.getByRole('region', { name: 'Dialogue' });
  await waitFor(() => {
    fireEvent.keyDown(screenRegion, { key: 'z', ctrlKey: true });
    expect(setInfo).toHaveBeenCalledTimes(2);
  });
  expect(setInfo.mock.calls[1][2].responses[0].text).toBe('First line');
  await waitFor(() => expect((screen.getByLabelText('Line text') as HTMLTextAreaElement).value).toBe('First line'));
});

it('Detects: Ctrl+Z after picking another quest does not undo an edit made on the previous quest', async () => {
  const { setInfo } = setUpBackend(['First line', 'Second line', 'Third line']);
  await openTopic();
  fireEvent.click(screen.getByRole('button', { name: /First line/ }));
  fireEvent.change(screen.getByLabelText('Line text'), { target: { value: 'Changed line' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save line' }));
  await waitFor(() => expect(setInfo).toHaveBeenCalledTimes(1));
  fireEvent.click(screen.getByRole('button', { name: 'Change quest' }));
  fireEvent.click(await screen.findByText('MQ102'));
  await screen.findByRole('button', { name: /Hello there/ });
  fireEvent.keyDown(screen.getByRole('region', { name: 'Dialogue' }), { key: 'z', ctrlKey: true });
  expect(setInfo).toHaveBeenCalledTimes(1);
});

it('flags a character Windows-1252 cannot hold inline and does not call SetInfo. Detects: unsupported text reaching SetInfo', async () => {
  const { setInfo } = setUpBackend(['First line', 'Second line', 'Third line']);
  await openTopic();
  fireEvent.click(screen.getByRole('button', { name: /First line/ }));
  fireEvent.change(screen.getByLabelText('Line text'), { target: { value: 'Hello 中' } });
  const message = screen.getByText(/cannot be stored in game text/);
  expect(message.textContent).toContain('"中" (U+4E2D)');
  fireEvent.click(screen.getByRole('button', { name: 'Save line' }));
  expect(setInfo).not.toHaveBeenCalled();
});

it('Detects: tab switching shows only the active panel', async () => {
  setUpBackend(['First line', 'Second line', 'Third line']);
  await openTopic();
  expect(screen.getByRole('region', { name: 'Conversation' })).toBeTruthy();
  expect(screen.queryByRole('searchbox', { name: 'Search topics' })).toBeNull();
  fireEvent.click(screen.getByRole('tab', { name: /^Topics/ }));
  expect(screen.getByRole('searchbox', { name: 'Search topics' })).toBeTruthy();
  expect(screen.queryByRole('region', { name: 'Conversation' })).toBeNull();
  fireEvent.click(screen.getByRole('tab', { name: /^Details/ }));
  expect(screen.queryByRole('searchbox', { name: 'Search topics' })).toBeNull();
  expect(screen.getByText(/Pick a reply line in the Conversation tab/)).toBeTruthy();
});

it('Detects: selecting a topic in Topics opens the Conversation tab', async () => {
  setUpBackend(['First line', 'Second line', 'Third line']);
  await openTopic();
  expect(screen.getByRole('region', { name: 'Conversation' }).textContent).toContain('Hello there');
});

it('Detects: filtering topics narrows the list and pagination caps the rendered topics', async () => {
  setUpBackend(['First line'], 120);
  await openQuest();
  const topicItems = () => within(screen.getByRole('navigation', { name: 'Branches and topics' })).queryAllByRole('listitem');
  expect(topicItems()).toHaveLength(50);
  fireEvent.click(screen.getByRole('button', { name: 'Next' }));
  expect(screen.getByText('Page 2 of 3')).toBeTruthy();
  expect(topicItems()).toHaveLength(50);
  fireEvent.change(screen.getByRole('searchbox', { name: 'Search topics' }), { target: { value: 'hello' } });
  expect(topicItems()).toHaveLength(1);
  expect(screen.queryByRole('button', { name: 'Next' })).toBeNull();
});

it('Detects: selecting a reply opens Details with that line number', async () => {
  setUpBackend(['First line', 'Second line', 'Third line']);
  await openTopic();
  fireEvent.click(screen.getByRole('button', { name: /Second line/ }));
  expect(screen.queryByRole('region', { name: 'Conversation' })).toBeNull();
  expect((screen.getByLabelText('Line number') as HTMLInputElement).value).toBe('2');
});

it('Detects: the breadcrumb updates as the user moves from quest to topic to reply', async () => {
  setUpBackend(['First line', 'Second line', 'Third line']);
  await openQuest();
  const crumbs = () => screen.getByRole('navigation', { name: 'Location' }).textContent;
  expect(crumbs()).toMatch(/^000800:Patch\.esp$/);
  fireEvent.click(screen.getByRole('button', { name: /Hello there/ }));
  expect(crumbs()).toMatch(/^000800:Patch\.esp › MainBranch › Hello there$/);
  fireEvent.click(screen.getByRole('button', { name: /Second line/ }));
  expect(crumbs()).toMatch(/^000800:Patch\.esp › MainBranch › Hello there › Reply 1$/);
});
