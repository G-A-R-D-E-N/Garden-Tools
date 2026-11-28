import { expect, it } from 'vitest';
import { parseScene } from './sceneModel';
import {
  NO_FILTER, filterActions, outline, questAliasNames, sanitizeFilter, sceneCrumbs, sceneTabItems, tabForSelection,
} from './sceneTabModel';

const scene = parseScene({
  formKey: '000800:Patch.esp',
  phases: [{ index: 0, name: 'Walk' }, { index: 1, name: 'Talk' }, { index: 2, name: 'Leave' }],
  actors: [{ index: 0, alias: 3, flags: 0, behaviorFlags: 0 }],
  actions: [
    { index: 0, type: 0, name: 'Greeting', actorAlias: 3, startPhase: 1, endPhase: 2, payload: { topic: '000900:Patch.esp' } },
    { index: 1, type: 2, actorAlias: 9, startPhase: 0, endPhase: 9, payload: {} },
    { index: 2, type: 0, name: 'Lost', actorAlias: 3, startPhase: 7, endPhase: 8, payload: {} },
  ],
});

it('Detects: an action passes the phase filter when its column span covers the phase, and the actor filter matches its alias', () => {
  expect(filterActions(scene, { phase: 0, actor: null }).map(action => action.index)).toEqual([1]);
  expect(filterActions(scene, { phase: 2, actor: null }).map(action => action.index)).toEqual([0, 1, 2]);
  expect(filterActions(scene, { phase: null, actor: 3 }).map(action => action.index)).toEqual([0, 2]);
  expect(filterActions(scene, { phase: 2, actor: 9 }).map(action => action.index)).toEqual([1]);
  expect(filterActions(scene, NO_FILTER)).toHaveLength(3);
});

it('Detects: the outline puts each action under its first phase, clamped to the last phase, and adds no loose group when every action has a phase', () => {
  const groups = outline(scene);
  expect(groups.map(group => group.phase?.index ?? 'loose')).toEqual([0, 1, 2]);
  expect(groups[0].actions.map(action => action.index)).toEqual([1]);
  expect(groups[1].actions.map(action => action.index)).toEqual([0]);
  expect(groups[2].actions.map(action => action.index)).toEqual([2]);
});

it('Detects: the outline keeps actions in a loose group only when the scene has no phases', () => {
  const phaseless = parseScene({ formKey: '000800:Patch.esp', phases: [], actions: [
    { index: 0, type: 0, name: 'Lone', actorAlias: 3, startPhase: 0, endPhase: 0, payload: {} },
  ] });
  const groups = outline(phaseless);
  expect(groups.map(group => group.phase)).toEqual([null]);
  expect(groups[0].actions.map(action => action.index)).toEqual([0]);
});

it('Detects: the tab items carry counts and a problem badge only where the scene has a problem', () => {
  const items = sceneTabItems(scene);
  expect(items.map(item => item.id)).toEqual(['overview', 'timeline', 'phases', 'actors', 'actions', 'settings']);
  expect(items.find(item => item.id === 'phases')).toMatchObject({ count: 3 });
  expect(items.find(item => item.id === 'actions')).toMatchObject({ count: 3 });
  expect(items.some(item => item.badge === 'problem')).toBe(false);
  const unreadable = parseScene({ ...scene, phases: [{ index: 0, name: 'Walk', startConditionsUnreadable: true }] });
  expect(sceneTabItems(unreadable).find(item => item.id === 'phases')).toMatchObject({ badge: 'problem' });
});

it('Detects: the breadcrumb names the selected phase, actor or action, and falls back to the tab name', () => {
  const names = new Map<number, string>([[3, 'Courier']]);
  expect(sceneCrumbs(scene, null, 'actions', names)).toEqual(['Scene', 'Actions']);
  expect(sceneCrumbs(scene, { kind: 'phase', index: 1 }, 'phases', names)).toEqual(['Scene', 'Phase 2: Talk']);
  expect(sceneCrumbs(scene, { kind: 'actor', index: 0 }, 'actors', names)).toEqual(['Scene', 'Actors', 'Alias 3 · Courier']);
  expect(sceneCrumbs(scene, { kind: 'action', index: 0 }, 'actions', names))
    .toEqual(['Scene', 'Phase 2: Talk', 'Action 1 (Greeting)']);
  expect(sceneCrumbs(scene, { kind: 'action', index: 9 }, 'actions', names)).toEqual(['Scene', 'Actions']);
});

it('Detects: the breadcrumb shows the selection only on the tab that edits it, and names the clamped first phase of an action', () => {
  const names = new Map<number, string>();
  expect(sceneCrumbs(scene, { kind: 'phase', index: 1 }, 'overview', names)).toEqual(['Scene', 'Overview']);
  expect(sceneCrumbs(scene, { kind: 'action', index: 0 }, 'timeline', names)).toEqual(['Scene', 'Timeline']);
  expect(sceneCrumbs(scene, { kind: 'action', index: 2 }, 'actions', names))
    .toEqual(['Scene', 'Phase 3: Leave', 'Action 3 (Lost)']);
});

it('Detects: a selection opens the tab that edits that kind of item', () => {
  expect(tabForSelection({ kind: 'phase', index: 0 })).toBe('phases');
  expect(tabForSelection({ kind: 'actor', index: 0 })).toBe('actors');
  expect(tabForSelection({ kind: 'action', index: 0 })).toBe('actions');
});

it('Detects: a filter on a removed phase or a missing actor is cleared, and a live one is kept', () => {
  expect(sanitizeFilter(scene, { phase: 7, actor: 4 })).toEqual({ phase: null, actor: null });
  expect(sanitizeFilter(scene, { phase: 2, actor: 3 })).toEqual({ phase: 2, actor: 3 });
  expect(sanitizeFilter(scene, NO_FILTER)).toEqual(NO_FILTER);
});

it('Detects: quest alias names are read by alias id and an unnamed alias is left out', () => {
  const names = questAliasNames({
    aliases: [
      { id: 3, kind: 'reference', name: 'Courier' },
      { id: 4, kind: 'reference', name: '' },
    ],
  });
  expect(names.get(3)).toBe('Courier');
  expect(names.has(4)).toBe(false);
  expect(questAliasNames('{"aliases":[]}').size).toBe(0);
});
