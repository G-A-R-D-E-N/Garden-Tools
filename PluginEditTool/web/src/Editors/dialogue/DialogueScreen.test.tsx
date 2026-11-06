import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
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

function setUpBackend(lines: string[]) {
  const state = { lines };
  const dialogue = () => ({
    branches: [{ formKey: BRANCH, editorId: 'MainBranch', topic: null }],
    topics: [{
      formKey: TOPIC, editorId: 'GreetTopic', priority: 50, branch: BRANCH, name: 'Hello there',
      infos: [{
        formKey: INFO, flags: 0, previous: null, conditions: [], fragments: [],
        responses: state.lines.map((text, i) => line(i + 1, text)),
      }],
    }],
  });
  const setInfo = vi.fn(async (_plugin: string, _info: string, spec: { responses: { text: string }[] }) => {
    state.lines = spec.responses.map(r => r.text);
    return 'ok';
  });
  const backend = {
    SearchRecords: vi.fn(async () => JSON.stringify([
      { FormKey: QUEST, EditorID: 'MQ101', Type: 'QUST', Plugin: 'Patch.esp' },
      { FormKey: OTHER_QUEST, EditorID: 'MQ102', Type: 'QUST', Plugin: 'Patch.esp' },
    ])),
    GetDialogue: vi.fn(async () => JSON.stringify(dialogue())),
    SetInfo: setInfo,
  };
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend } } });
  return { setInfo };
}

async function openTopic() {
  render(<DialogueEditor onBack={() => {}} />);
  fireEvent.click(screen.getByRole('button', { name: 'Pick a quest' }));
  fireEvent.click(await screen.findByText('MQ101'));
  fireEvent.click(await screen.findByRole('button', { name: /Hello there/ }));
}

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
  await screen.findByRole('button', { name: /First line/ });
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
