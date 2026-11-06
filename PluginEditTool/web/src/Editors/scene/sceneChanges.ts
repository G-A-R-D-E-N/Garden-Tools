import type { Backend, ConditionDto } from '../../backend';
import { PAYLOAD_KEYS, type SceneAction, type SceneActor, type ScenePhase, type Selection, type Spec } from './sceneModel';

export interface Change {
  label: string;
  apply: () => Promise<void>;
  revert: () => Promise<void>;
}

export type Run = (change: Change, after?: Selection) => void;

type Step = () => unknown;

const steps = (...list: Step[]) => async (): Promise<void> => {
  for (const step of list) await step();
};

const LINK_REVERT_MESSAGE =
  'This change cannot be undone: the link was empty before, and this tool cannot clear a link yet.';

const revertable = (spec: Spec, step: Step): Step => () => {
  if (Object.values(spec).includes(null)) throw new Error(LINK_REVERT_MESSAGE);
  return step();
};

const withoutUndefined = (spec: Spec): Spec =>
  Object.fromEntries(Object.entries(spec).filter(([, value]) => value !== undefined));

export const phaseSpec = (phase: ScenePhase): Spec => withoutUndefined({
  name: phase.name ?? '',
  editorWidth: phase.editorWidth,
  flags: phase.flags,
  questStageOnStart: phase.questStageOnStart,
  questStageOnCompletion: phase.questStageOnCompletion,
});

function phaseRestoreSpec(phase: ScenePhase): Spec {
  const spec = phaseSpec(phase);
  if (!phase.startConditionsUnreadable) spec.startConditions = phase.startConditions ?? [];
  if (!phase.completionConditionsUnreadable) spec.completionConditions = phase.completionConditions ?? [];
  return spec;
}

export function actionRestoreSpec(action: SceneAction): Spec {
  const payload = Object.fromEntries(
    Object.entries(action.payload).filter(([key, value]) => value !== null && value !== undefined && PAYLOAD_KEYS.has(key)));
  return {
    ...payload,
    name: action.name ?? '',
    actorAlias: action.actorAlias ?? -1,
    flags: action.flags ?? 0,
    startPhase: action.startPhase ?? 0,
    endPhase: action.endPhase ?? 0,
  };
}

export interface ActionEdit {
  index: number;
  next: Spec;
  prev: Spec;
}

export function repointActorEdits(actions: SceneAction[], fromAlias: number, toAlias: number): ActionEdit[] {
  return actions
    .filter(action => action.actorAlias === fromAlias)
    .map(action => ({ index: action.index, next: { actorAlias: toAlias }, prev: { actorAlias: fromAlias } }));
}

export function repointPhaseEdits(actions: SceneAction[], fromPhase: number, toPhase: number): ActionEdit[] {
  return actions.flatMap(action => {
    const next: Spec = {};
    const prev: Spec = {};
    if (action.startPhase === fromPhase) {
      next.startPhase = toPhase;
      prev.startPhase = fromPhase;
    }
    if (action.endPhase === fromPhase) {
      next.endPhase = toPhase;
      prev.endPhase = fromPhase;
    }
    return Object.keys(next).length > 0 ? [{ index: action.index, next, prev }] : [];
  });
}

export function diffDraft(draft: Spec, saved: Spec, linkKeys: ReadonlySet<string> = new Set<string>()) {
  const next: Spec = {};
  const prev: Spec = {};
  const cleared: string[] = [];
  for (const key of Object.keys(draft)) {
    if (draft[key] === saved[key]) continue;
    if (linkKeys.has(key) && draft[key] === '') {
      cleared.push(key);
      continue;
    }
    next[key] = draft[key];
    prev[key] = linkKeys.has(key) && saved[key] === '' ? null : saved[key];
  }
  return { next, prev, cleared };
}

export function sceneChanges(b: Backend, plugin: string, scene: string) {
  const addPhase = (index: number): Change => ({
    label: 'Add phase',
    apply: steps(() => b.AddScenePhase(plugin, scene, index)),
    revert: steps(() => b.RemoveScenePhase(plugin, scene, index)),
  });

  const movePhase = (from: number, to: number): Change => ({
    label: 'Move phase',
    apply: steps(() => b.MoveScenePhase(plugin, scene, from, to)),
    revert: steps(() => b.MoveScenePhase(plugin, scene, to, from)),
  });

  const removePhase = (phase: ScenePhase): Change => ({
    label: 'Remove phase',
    apply: steps(() => b.RemoveScenePhase(plugin, scene, phase.index)),
    revert: steps(
      () => b.AddScenePhase(plugin, scene, phase.index),
      () => b.SetScenePhase(plugin, scene, phase.index, phaseRestoreSpec(phase)),
    ),
  });

  const editPhase = (index: number, next: Spec, prev: Spec): Change => ({
    label: 'Change phase',
    apply: steps(() => b.SetScenePhase(plugin, scene, index, next)),
    revert: steps(() => b.SetScenePhase(plugin, scene, index, prev)),
  });

  const addActor = (index: number): Change => ({
    label: 'Add actor',
    apply: steps(() => b.AddSceneActor(plugin, scene, index)),
    revert: steps(() => b.RemoveSceneActor(plugin, scene, index)),
  });

  const removeActor = (actor: SceneActor): Change => ({
    label: 'Remove actor',
    apply: steps(() => b.RemoveSceneActor(plugin, scene, actor.index)),
    revert: steps(
      () => b.AddSceneActor(plugin, scene, actor.index),
      () => b.SetSceneActor(plugin, scene, actor.index, {
        alias: actor.alias,
        flags: actor.flags,
        behaviorFlags: actor.behaviorFlags,
      }),
    ),
  });

  const editActor = (index: number, next: Spec, prev: Spec): Change => ({
    label: 'Change actor',
    apply: steps(() => b.SetSceneActor(plugin, scene, index, next)),
    revert: steps(() => b.SetSceneActor(plugin, scene, index, prev)),
  });

  const editActions = (label: string, edits: ActionEdit[]): Change => ({
    label,
    apply: steps(...edits.map(edit => () => b.SetSceneAction(plugin, scene, edit.index, edit.next))),
    revert: steps(...edits.map(edit => revertable(edit.prev, () => b.SetSceneAction(plugin, scene, edit.index, edit.prev)))),
  });

  const addAction = (index: number, type: number, spec: Spec): Change => ({
    label: 'Add action',
    apply: steps(
      () => b.AddSceneAction(plugin, scene, index, type),
      () => b.SetSceneAction(plugin, scene, index, spec),
    ),
    revert: steps(() => b.RemoveSceneAction(plugin, scene, index)),
  });

  const removeAction = (action: SceneAction): Change => ({
    label: 'Remove action',
    apply: steps(() => b.RemoveSceneAction(plugin, scene, action.index)),
    revert: steps(
      () => b.AddSceneAction(plugin, scene, action.index, action.type),
      () => b.SetSceneAction(plugin, scene, action.index, actionRestoreSpec(action)),
    ),
  });

  const moveAction = (from: number, to: number): Change => ({
    label: 'Move action',
    apply: steps(() => b.MoveSceneAction(plugin, scene, from, to)),
    revert: steps(() => b.MoveSceneAction(plugin, scene, to, from)),
  });

  const editScene = (next: Spec, prev: Spec): Change => ({
    label: 'Change scene',
    apply: steps(() => b.SetScene(plugin, scene, next)),
    revert: steps(() => b.SetScene(plugin, scene, prev)),
  });

  return {
    addPhase, movePhase, removePhase, editPhase, addActor, removeActor, editActor,
    editActions, addAction, removeAction, moveAction, editScene,
  };
}

export type SceneChanges = ReturnType<typeof sceneChanges>;

export const readConditions = async (b: Backend, plugin: string, scene: string, path: string): Promise<ConditionDto[]> =>
  JSON.parse(await b.GetConditionsAt(plugin, scene, path));

export const writeConditions = async (
  b: Backend, plugin: string, scene: string, path: string, rows: ConditionDto[],
): Promise<void> => {
  await b.SetConditionsAt(plugin, scene, path, JSON.stringify(rows));
};
