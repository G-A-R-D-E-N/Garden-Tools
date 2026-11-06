import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import SceneWorkspace from './SceneWorkspace';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

type Row = Record<string, unknown>;
interface FakeScene { formKey: string; editorId: string; flags: number; parentQuest: null; phases: Row[]; actors: Row[]; actions: Row[] }

const phase = (index: number, name: string, extra: Row = {}): Row => ({
  index, name, editorWidth: 0, flags: 0, questStageOnStart: 0, questStageOnCompletion: 0,
  startConditions: [], completionConditions: [], ...extra,
});

const sceneFixture = (): FakeScene => ({
  formKey: '000800:Patch.esp',
  editorId: 'TestScene',
  flags: 0,
  parentQuest: null,
  phases: [phase(0, 'Walk'), phase(1, 'Talk')],
  actors: [
    { index: 0, alias: 3, flags: 0, behaviorFlags: 0 },
    { index: 1, alias: 5, flags: 0, behaviorFlags: 0 },
  ],
  actions: [
    { index: 0, type: 0, name: 'Greeting', actorAlias: 3, startPhase: 0, endPhase: 1, flags: 0, payload: {} },
    { index: 1, type: 0, name: 'Reply', actorAlias: 3, startPhase: 1, endPhase: 1, flags: 0, payload: {} },
  ],
});

function fakeBackend(initial: FakeScene) {
  const scene = structuredClone(initial);
  const backend = {
    GetScene: vi.fn(async () => JSON.stringify(scene)),
    SetScenePhase: vi.fn(async (_plugin: string, _scene: string, index: number, spec: Row) => {
      Object.assign(scene.phases[index], spec);
      return 'ok';
    }),
    SetSceneAction: vi.fn(async (_plugin: string, _scene: string, index: number, spec: Row) => {
      Object.assign(scene.actions[index], spec);
      return 'ok';
    }),
  };
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend } } });
  return backend;
}

it('Detects: a used actor cannot be removed until its actions are re-pointed', async () => {
  const backend = fakeBackend(sceneFixture());
  render(<SceneWorkspace sceneKey="000800:Patch.esp" plugin="Patch.esp" onPickScene={() => {}} />);
  fireEvent.click(await screen.findByRole('button', { name: 'Alias 3' }));
  expect(screen.getByText(/2 actions use this actor/)).toBeTruthy();
  expect((screen.getByRole('button', { name: 'Remove actor' }) as HTMLButtonElement).disabled).toBe(true);

  fireEvent.click(screen.getByRole('button', { name: 'Re-point 2 actions' }));
  await waitFor(() => expect(backend.SetSceneAction).toHaveBeenCalledTimes(2));
  expect(backend.SetSceneAction).toHaveBeenCalledWith('Patch.esp', '000800:Patch.esp', 0, { actorAlias: 5 });
  await waitFor(() => expect((screen.getByRole('button', { name: 'Remove actor' }) as HTMLButtonElement).disabled).toBe(false));
});

it('Detects: unreadable conditions show a warning and no condition count', async () => {
  fakeBackend({ ...sceneFixture(), phases: [phase(0, 'Walk', { startConditionsUnreadable: true, startConditions: undefined }), phase(1, 'Talk')] });
  render(<SceneWorkspace sceneKey="000800:Patch.esp" plugin="Patch.esp" onPickScene={() => {}} />);
  fireEvent.click(await screen.findByRole('button', { name: 'Walk' }));
  expect(screen.getByText('These conditions could not be read; saving here will not change them')).toBeTruthy();
  expect((screen.getByRole('button', { name: 'Edit start conditions' }) as HTMLButtonElement).disabled).toBe(true);
  expect(screen.getAllByText('0 conditions')).toHaveLength(1);
});

it('Detects: an applied phase rename is restored by Undo', async () => {
  const backend = fakeBackend(sceneFixture());
  render(<SceneWorkspace sceneKey="000800:Patch.esp" plugin="Patch.esp" onPickScene={() => {}} />);
  fireEvent.click(await screen.findByRole('button', { name: 'Walk' }));
  fireEvent.change(screen.getByLabelText('Name'), { target: { value: 'Walk again' } });
  fireEvent.click(screen.getByRole('button', { name: 'Apply phase changes' }));
  await screen.findByRole('button', { name: 'Walk again' });
  expect(backend.SetScenePhase).toHaveBeenLastCalledWith('Patch.esp', '000800:Patch.esp', 0, { name: 'Walk again' });

  fireEvent.click(screen.getByRole('button', { name: 'Undo' }));
  await screen.findByRole('button', { name: 'Walk' });
  expect(backend.SetScenePhase).toHaveBeenLastCalledWith('Patch.esp', '000800:Patch.esp', 0, { name: 'Walk' });
});
