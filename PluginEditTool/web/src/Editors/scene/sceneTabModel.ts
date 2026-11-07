import { parseAliases } from '../quest/questModel';
import {
  actionColumns, actionTitle, aliasLabel,
  type FlagName, type SceneAction, type SceneDto, type ScenePhase, type Selection,
} from './sceneModel';

export type SceneTab = 'overview' | 'timeline' | 'phases' | 'actors' | 'actions' | 'settings';

export const SCENE_TABS: readonly { id: SceneTab; label: string }[] = [
  { id: 'overview', label: 'Overview' },
  { id: 'timeline', label: 'Timeline' },
  { id: 'phases', label: 'Phases' },
  { id: 'actors', label: 'Actors' },
  { id: 'actions', label: 'Actions' },
  { id: 'settings', label: 'Settings' },
];

export const NO_NAMES: ReadonlyMap<number, string> = new Map();

export const tabLabel = (tab: SceneTab): string => SCENE_TABS.find(entry => entry.id === tab)?.label ?? '';

export const tabForSelection = (selection: NonNullable<Selection>): SceneTab =>
  selection.kind === 'phase' ? 'phases' : selection.kind === 'actor' ? 'actors' : 'actions';

export interface TabItem {
  id: SceneTab;
  label: string;
  count?: number;
  badge?: 'problem';
}

const hasProblem = (scene: SceneDto, tab: SceneTab): boolean => {
  switch (tab) {
    case 'overview': return !!scene.structureError;
    case 'phases': return scene.phases.some(phase => !!phase.startConditionsUnreadable || !!phase.completionConditionsUnreadable);
    case 'settings': return !!scene.conditionsUnreadable;
    default: return false;
  }
};

export function sceneTabItems(scene: SceneDto): TabItem[] {
  return SCENE_TABS.map(({ id, label }) => {
    const badge = hasProblem(scene, id) ? 'problem' as const : undefined;
    if (id === 'phases') return { id, label, count: scene.phases.length, badge };
    if (id === 'actors') return { id, label, count: scene.actors.length, badge };
    if (id === 'actions') return { id, label, count: scene.actions.length, badge };
    return { id, label, badge };
  });
}

export interface ActionFilter {
  phase: number | null;
  actor: number | null;
}

export const NO_FILTER: ActionFilter = { phase: null, actor: null };

export const actorOf = (action: SceneAction): number => action.actorAlias ?? -1;

export function filterActions(scene: SceneDto, filter: ActionFilter): SceneAction[] {
  return scene.actions.filter(action => {
    if (filter.actor !== null && actorOf(action) !== filter.actor) return false;
    if (filter.phase === null) return true;
    const span = actionColumns(action, scene.phases.length);
    return span !== null && span.first <= filter.phase && filter.phase <= span.last;
  });
}

export const actorAliases = (scene: SceneDto): number[] =>
  [...new Set([...scene.actors.map(actor => actor.alias), ...scene.actions.map(actorOf)])].sort((a, b) => a - b);

export const actionsCovering = (scene: SceneDto, phaseIndex: number): number =>
  filterActions(scene, { phase: phaseIndex, actor: null }).length;

export const enabledCount = (value: number | undefined, names: readonly FlagName[]): number =>
  names.filter(flag => ((value ?? 0) & (1 << flag.bit)) !== 0).length;

export const phaseLabel = (phase: ScenePhase): string =>
  phase.name ? `Phase ${phase.index + 1}: ${phase.name}` : `Phase ${phase.index + 1}`;

export const aliasTitle = (alias: number, names: ReadonlyMap<number, string>): string => {
  const name = names.get(alias);
  return name ? `${aliasLabel(alias)} · ${name}` : aliasLabel(alias);
};

export const spanLabel = (action: SceneAction, phaseCount: number): string => {
  const span = actionColumns(action, phaseCount);
  if (!span) return 'No phase';
  return span.first === span.last ? `Phase ${span.first + 1}` : `Phases ${span.first + 1} to ${span.last + 1}`;
};

const DETAIL_KEYS = ['topic', 'startSceneTopic', 'playSound'] as const;

export function actionDetail(action: SceneAction): string {
  for (const key of DETAIL_KEYS) {
    const value = action.payload[key];
    if (typeof value === 'string' && value !== '') return value;
  }
  return '';
}

export function sceneCrumbs(scene: SceneDto, selection: Selection, tab: SceneTab, names: ReadonlyMap<number, string>): string[] {
  if (!selection || tabForSelection(selection) !== tab) return ['Scene', tabLabel(tab)];
  if (selection.kind === 'phase') {
    const phase = scene.phases.find(entry => entry.index === selection.index);
    if (phase) return ['Scene', phaseLabel(phase)];
  }
  if (selection.kind === 'actor') {
    const actor = scene.actors.find(entry => entry.index === selection.index);
    if (actor) return ['Scene', 'Actors', aliasTitle(actor.alias, names)];
  }
  if (selection.kind === 'action') {
    const action = scene.actions.find(entry => entry.index === selection.index);
    if (action) {
      const span = actionColumns(action, scene.phases.length);
      const phase = span ? scene.phases[span.first] : undefined;
      return ['Scene', ...(phase ? [phaseLabel(phase)] : []), `Action ${action.index + 1} (${actionTitle(action)})`];
    }
  }
  return ['Scene', tabLabel(tab)];
}

export const sanitizeFilter = (scene: SceneDto, filter: ActionFilter): ActionFilter => ({
  phase: filter.phase !== null && filter.phase < scene.phases.length ? filter.phase : null,
  actor: filter.actor !== null && actorAliases(scene).includes(filter.actor) ? filter.actor : null,
});

export interface OutlineGroup {
  phase: ScenePhase | null;
  actions: SceneAction[];
}

export function outline(scene: SceneDto): OutlineGroup[] {
  const groups: OutlineGroup[] = scene.phases.map(phase => ({ phase, actions: [] }));
  const loose: SceneAction[] = [];
  for (const action of scene.actions) {
    const span = actionColumns(action, scene.phases.length);
    const group = span ? groups[span.first] : undefined;
    if (group) group.actions.push(action);
    else loose.push(action);
  }
  return loose.length > 0 ? [...groups, { phase: null, actions: loose }] : groups;
}

export function questAliasNames(raw: string | object): ReadonlyMap<number, string> {
  const parsed: unknown = typeof raw === 'string' ? JSON.parse(raw) : raw;
  return new Map(parseAliases(parsed)
    .filter(alias => alias.name !== '')
    .map((alias): [number, string] => [alias.id, alias.name]));
}
