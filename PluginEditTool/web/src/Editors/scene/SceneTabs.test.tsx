import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import SceneWorkspace from './SceneWorkspace';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

type Row = Record<string, unknown>;
interface FakeScene { formKey: string; editorId: string; flags: number; parentQuest: null; phases: Row[]; actors: Row[]; actions: Row[] }

const phase = (index: number, name: string): Row => ({
  index, name, editorWidth: 0, flags: 0, questStageOnStart: 0, questStageOnCompletion: 0,
  startConditions: [], completionConditions: [],
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
    { index: 1, type: 0, name: 'Reply', actorAlias: 5, startPhase: 1, endPhase: 1, flags: 0, payload: { topic: '000900:Patch.esp' } },
  ],
});

function fakeBackend(initial: FakeScene) {
  const scene = structuredClone(initial);
  const reindex = (rows: Row[]) => rows.forEach((row, index) => { row.index = index; });
  const backend = {
    GetScene: vi.fn(async () => JSON.stringify(scene)),
    AddScenePhase: vi.fn(async (_plugin: string, _scene: string, index: number) => {
      scene.phases.splice(index, 0, phase(index, ''));
      reindex(scene.phases);
      return 'ok';
    }),
    AddSceneActor: vi.fn(async (_plugin: string, _scene: string, index: number) => {
      scene.actors.splice(index, 0, { index, alias: -1, flags: 0, behaviorFlags: 0 });
      reindex(scene.actors);
      return 'ok';
    }),
    AddSceneAction: vi.fn(async (_plugin: string, _scene: string, index: number, type: number) => {
      scene.actions.splice(index, 0, {
        index, type, name: '', actorAlias: -1, flags: 0, startPhase: 0, endPhase: 0, payload: {},
      });
      reindex(scene.actions);
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

const renderScene = () => render(<SceneWorkspace sceneKey="000800:Patch.esp" plugin="Patch.esp" onPickScene={() => {}} />);

it('Detects: only the active tab panel shows its content', async () => {
  fakeBackend(sceneFixture());
  renderScene();
  expect(await screen.findByRole('region', { name: 'Overview' })).toBeTruthy();
  fireEvent.click(screen.getByRole('tab', { name: /Settings/ }));
  expect(await screen.findByRole('region', { name: 'Scene settings' })).toBeTruthy();
  expect(screen.queryByRole('region', { name: 'Overview' })).toBeNull();
  expect(screen.queryByRole('button', { name: 'Walk' })).toBeNull();
  expect(screen.getByRole('button', { name: 'Add' })).toBeTruthy();
});

it('Detects: the Settings tab groups scene flags under headings', async () => {
  fakeBackend(sceneFixture());
  renderScene();
  fireEvent.click(await screen.findByRole('tab', { name: /Settings/ }));
  for (const heading of ['Quest', 'Timing', 'Behaviour', 'Flags']) {
    expect(await screen.findByRole('heading', { name: heading, level: 5 })).toBeTruthy();
  }
});

it('Detects: an overview outline row opens the Actions tab with that action selected', async () => {
  fakeBackend(sceneFixture());
  renderScene();
  fireEvent.click(await screen.findByRole('button', { name: /Reply/ }));
  expect(await screen.findByRole('region', { name: 'Reply' })).toBeTruthy();
  expect(screen.getByRole('button', { name: 'Greeting' })).toBeTruthy();
  expect(screen.getByRole('navigation', { name: 'Breadcrumb' }).textContent).toBe('Scene › Phase 2: Talk › Action 2 (Reply)');
});

it('Detects: an overview phase row opens the Phases tab with that phase selected', async () => {
  fakeBackend(sceneFixture());
  renderScene();
  fireEvent.click(await screen.findByRole('button', { name: /Talk/ }));
  expect(await screen.findByRole('region', { name: 'Phase 2' })).toBeTruthy();
  expect(screen.getByRole('navigation', { name: 'Breadcrumb' }).textContent).toBe('Scene › Phase 2: Talk');
});

it('Detects: the Add menu adds a phase through the same backend call and selects it', async () => {
  const backend = fakeBackend(sceneFixture());
  renderScene();
  fireEvent.click(await screen.findByRole('button', { name: 'Add' }));
  fireEvent.click(screen.getByRole('menuitem', { name: 'Add phase' }));
  await waitFor(() => expect(backend.AddScenePhase).toHaveBeenCalledWith('Patch.esp', '000800:Patch.esp', 2));
  expect(await screen.findByRole('region', { name: 'Phase 3' })).toBeTruthy();
  expect(screen.queryByRole('menu')).toBeNull();
});

it('Detects: the Add menu adds an action of the chosen type through the same backend calls', async () => {
  const backend = fakeBackend(sceneFixture());
  renderScene();
  fireEvent.click(await screen.findByRole('button', { name: 'Add' }));
  fireEvent.click(screen.getByRole('menuitem', { name: 'Add action' }));
  fireEvent.click(screen.getByRole('menuitem', { name: 'Timer' }));
  await waitFor(() => expect(backend.AddSceneAction).toHaveBeenCalledWith('Patch.esp', '000800:Patch.esp', 2, 2));
  expect(backend.SetSceneAction).toHaveBeenCalledWith('Patch.esp', '000800:Patch.esp', 2, {
    name: 'Timer', actorAlias: 3, startPhase: 0, endPhase: 0,
  });
  expect(await screen.findByRole('region', { name: 'Timer' })).toBeTruthy();
});

it('Detects: the Add menu adds an actor and can be dismissed with Escape', async () => {
  const backend = fakeBackend(sceneFixture());
  renderScene();
  fireEvent.click(await screen.findByRole('button', { name: 'Add' }));
  expect(screen.getByRole('menu')).toBeTruthy();
  fireEvent.keyDown(document, { key: 'Escape' });
  expect(screen.queryByRole('menu')).toBeNull();
  fireEvent.click(screen.getByRole('button', { name: 'Add' }));
  fireEvent.click(screen.getByRole('menuitem', { name: 'Add actor' }));
  await waitFor(() => expect(backend.AddSceneActor).toHaveBeenCalledWith('Patch.esp', '000800:Patch.esp', 2));
  expect(await screen.findByRole('region', { name: 'No alias set' })).toBeTruthy();
});

it('Detects: an outline row moves focus onto the tab it opens, so Ctrl+Z still reaches the section', async () => {
  fakeBackend(sceneFixture());
  renderScene();
  fireEvent.click(await screen.findByRole('button', { name: /Reply/ }));
  await screen.findByRole('region', { name: 'Reply' });
  expect(document.activeElement).toBe(screen.getByRole('tab', { name: /Actions/ }));
});

it('Detects: the Actions filters narrow the list by phase and by actor', async () => {
  fakeBackend(sceneFixture());
  renderScene();
  fireEvent.click(await screen.findByRole('tab', { name: /Actions/ }));
  expect(await screen.findByRole('button', { name: 'Greeting' })).toBeTruthy();
  expect(screen.getByRole('button', { name: 'Reply' })).toBeTruthy();

  fireEvent.change(screen.getByLabelText('Phase'), { target: { value: '0' } });
  expect(screen.queryByRole('button', { name: 'Reply' })).toBeNull();
  expect(screen.getByRole('button', { name: 'Greeting' })).toBeTruthy();

  fireEvent.change(screen.getByLabelText('Phase'), { target: { value: '' } });
  fireEvent.change(screen.getByLabelText('Actor'), { target: { value: '5' } });
  expect(screen.queryByRole('button', { name: 'Greeting' })).toBeNull();
  expect(screen.getByRole('button', { name: 'Reply' })).toBeTruthy();
});

it('Detects: a newly added action hidden by the actor filter is reported as hidden', async () => {
  fakeBackend(sceneFixture());
  renderScene();
  fireEvent.click(await screen.findByRole('tab', { name: /Actions/ }));
  fireEvent.change(await screen.findByLabelText('Actor'), { target: { value: '5' } });
  fireEvent.click(screen.getByRole('button', { name: 'Add' }));
  fireEvent.click(screen.getByRole('menuitem', { name: 'Add action' }));
  fireEvent.click(screen.getByRole('menuitem', { name: 'Timer' }));
  expect(await screen.findByText('2 hidden by the filter')).toBeTruthy();
});
