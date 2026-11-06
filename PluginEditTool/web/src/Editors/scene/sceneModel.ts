import type { ConditionDto } from '../../backend';

export interface ScenePhase {
  index: number;
  name?: string;
  editorWidth?: number;
  flags?: number;
  questStageOnStart?: number;
  questStageOnCompletion?: number;
  startConditions?: ConditionDto[];
  startConditionsUnreadable?: boolean;
  completionConditions?: ConditionDto[];
  completionConditionsUnreadable?: boolean;
  startFragment?: { script: string; function: string };
  completionFragment?: { script: string; function: string };
}

export interface SceneActor {
  index: number;
  alias: number;
  flags: number;
  behaviorFlags: number;
}

export interface SceneAction {
  index: number;
  type: number;
  typeName?: string;
  name?: string;
  actorAlias?: number;
  flags?: number;
  startPhase?: number;
  endPhase?: number;
  payload: Record<string, unknown>;
}

export interface SceneDto {
  formKey: string;
  editorId?: string;
  structureError?: string;
  flags?: number;
  parentQuest?: string | null;
  phases: ScenePhase[];
  actors: SceneActor[];
  actions: SceneAction[];
  conditions?: ConditionDto[];
  conditionsUnreadable?: boolean;
}

export interface ConditionsTarget {
  path: string;
  label: string;
  rows: ConditionDto[];
}

export type Selection = { kind: 'phase' | 'actor' | 'action'; index: number } | null;

export type Spec = Record<string, unknown>;

export function parseScene(raw: string | object): SceneDto {
  const parsed = (typeof raw === 'string' ? JSON.parse(raw) : raw) as Partial<SceneDto>;
  return { formKey: '', phases: [], actors: [], actions: [], ...parsed } as SceneDto;
}

export const ACTION_TYPES = [
  { type: 0, label: 'Dialogue' },
  { type: 1, label: 'Package' },
  { type: 2, label: 'Timer' },
  { type: 3, label: 'Player Dialogue' },
  { type: 4, label: 'Start Scene' },
  { type: 5, label: 'NPC Response Dialogue' },
  { type: 6, label: 'Radio' },
] as const;

export const typeLabel = (type: number): string =>
  ACTION_TYPES.find(entry => entry.type === type)?.label ?? `Type ${type}`;

export interface PayloadField {
  key: string;
  label: string;
  kind: 'link' | 'number';
  topic: string;
}

const link = (key: string, label: string, topic: string): PayloadField => ({ key, label, kind: 'link', topic });
const numeric = (key: string, label: string, topic: string): PayloadField => ({ key, label, kind: 'number', topic });

const SIDES = ['Positive', 'Negative', 'Neutral', 'Question'] as const;
const replyFields = (who: 'player' | 'npc', owner: string): PayloadField[] =>
  SIDES.flatMap(side => [
    link(`${who}${side}Response`, `${owner} ${side.toLowerCase()} reply`, 'scene-reply'),
    link(`${who}${side}DialogueSubtype`, `${owner} ${side.toLowerCase()} subtype`, 'scene-subtype'),
  ]);

const TOPIC = link('topic', 'Topic', 'scene-topic');
const SUBTYPE = link('dialogueSubtype', 'Dialogue subtype', 'scene-subtype');
const AUDIO = link('audioOutputOverride', 'Audio output override', 'scene-audio');
const LOOPING_MIN = numeric('loopingMin', 'Looping minimum (seconds)', 'scene-looping');
const LOOPING_MAX = numeric('loopingMax', 'Looping maximum (seconds)', 'scene-looping');
const TARGET = numeric('dialogueTargetActor', 'Target actor alias', 'scene-target');

const PAYLOAD_BY_TYPE: Record<number, PayloadField[]> = {
  0: [
    TOPIC, SUBTYPE, link('animArchType', 'Animation archetype', 'scene-anim'), AUDIO, LOOPING_MIN, LOOPING_MAX,
    numeric('emotionType', 'Emotion type', 'scene-emotion'), numeric('emotionValue', 'Emotion value', 'scene-emotion'),
    numeric('cameraFovOnPlayerCamera', 'Camera field of view', 'scene-camera'),
    numeric('cameraRateOfChange', 'Camera rate of change', 'scene-camera'),
  ],
  1: [],
  2: [numeric('timerMin', 'Timer minimum (seconds)', 'scene-timer'), numeric('timerMax', 'Timer maximum (seconds)', 'scene-timer')],
  3: [AUDIO, ...replyFields('player', 'Player'), ...replyFields('npc', 'NPC'), TARGET],
  4: [link('startSceneTopic', 'Start topic', 'scene-start-topic')],
  5: [AUDIO, ...replyFields('player', 'Player')],
  6: [TOPIC, link('playSound', 'Sound', 'scene-sound'), LOOPING_MAX, SUBTYPE],
};

export const payloadFieldsFor = (type: number): PayloadField[] => PAYLOAD_BY_TYPE[type] ?? [];

export const PAYLOAD_KEYS: ReadonlySet<string> = new Set(Object.values(PAYLOAD_BY_TYPE).flat().map(field => field.key));

export function unrestorableReason(action: SceneAction): string | null {
  if (action.type === 1) return 'A Package action holds a package list that this editor cannot put back on undo.';
  const keys = Object.keys(action.payload).filter(key => action.payload[key] !== null && !PAYLOAD_KEYS.has(key));
  return keys.length > 0
    ? `This action has settings that this editor cannot put back on undo (${keys.join(', ')}).`
    : null;
}

export interface FlagName {
  bit: number;
  label: string;
}

export const ACTION_FLAGS: readonly FlagName[] = [
  { bit: 7, label: 'Player Positive Use Dialogue Subtype / Hold Into Next Scene' },
  { bit: 8, label: 'Player Negative Use Dialogue Subtype' },
  { bit: 9, label: 'Player Neutral Use Dialogue Subtype' },
  { bit: 10, label: 'Use Dialogue Subtype' },
  { bit: 11, label: 'Player Question Use Dialogue Subtype' },
  { bit: 12, label: 'Keep/Clear Target on Action End' },
  { bit: 14, label: 'Run on End of Phase' },
  { bit: 15, label: 'Face Target' },
  { bit: 16, label: 'Looping' },
  { bit: 17, label: 'Headtrack Player' },
  { bit: 19, label: 'Ignore For Completion' },
  { bit: 21, label: 'Camera Speaker Target' },
  { bit: 22, label: 'Complete Face Target' },
  { bit: 27, label: 'NPC Positive Use Dialogue Subtype' },
  { bit: 28, label: 'NPC Negative Use Dialogue Subtype' },
  { bit: 29, label: 'NPC Neutral Use Dialogue Subtype' },
  { bit: 30, label: 'NPC Question Use Dialogue Subtype' },
];

export const PHASE_FLAGS: readonly FlagName[] = [
  { bit: 0, label: 'Start - WalkAway Phase' },
  { bit: 1, label: "Don't Run End Scripts on Scene Jump" },
  { bit: 2, label: 'Start - Inherit In Templated Scenes' },
];

export const ACTOR_FLAGS: readonly FlagName[] = [
  { bit: 0, label: 'No Player Activation' },
  { bit: 1, label: 'Optional' },
  { bit: 2, label: 'Run Only Scene Packages' },
  { bit: 3, label: 'No Command State' },
];

export const ACTOR_BEHAVIOR_FLAGS: readonly FlagName[] = [
  { bit: 0, label: 'Death Pause' },
  { bit: 1, label: 'Death End' },
  { bit: 2, label: 'Combat Pause' },
  { bit: 3, label: 'Combat End' },
  { bit: 4, label: 'Dialogue Pause' },
  { bit: 5, label: 'Dialogue End' },
  { bit: 6, label: 'OBS_COM Pause' },
  { bit: 7, label: 'OBS_COM End' },
];

export const SCENE_FLAGS: readonly FlagName[] = [
  { bit: 0, label: 'Begin on Quest Start' },
  { bit: 1, label: 'Stop on Quest End' },
  { bit: 2, label: 'Show All Text' },
  { bit: 3, label: 'Repeat Conditions While True' },
  { bit: 4, label: 'Interruptible' },
  { bit: 5, label: 'Has Player Dialogue' },
  { bit: 6, label: 'Prevent Player Exit Dialogue' },
  { bit: 9, label: 'Pause Actors Current Scenes' },
  { bit: 11, label: 'Disable Dialogue Camera' },
  { bit: 12, label: 'No Follower Idle Chatter' },
];

export const hasBit = (value: number | undefined, bit: number): boolean => ((value ?? 0) & (1 << bit)) !== 0;

export const withBit = (value: number, bit: number, on: boolean): number =>
  (on ? value | (1 << bit) : value & ~(1 << bit)) >>> 0;

export const actionsUsingActor = (scene: SceneDto, alias: number): SceneAction[] =>
  scene.actions.filter(action => action.actorAlias === alias);

export const actionsUsingPhase = (scene: SceneDto, index: number): SceneAction[] =>
  scene.actions.filter(action => action.startPhase === index || action.endPhase === index);

export const phaseName = (phase: ScenePhase): string => phase.name || `Phase ${phase.index + 1}`;

export const aliasLabel = (alias: number): string => (alias >= 0 ? `Alias ${alias}` : 'No alias set');

export const actorName = (actor: SceneActor): string => aliasLabel(actor.alias);

export const actionTitle = (action: SceneAction): string => action.name || typeLabel(action.type);

export const plural = (count: number, one: string, many: string): string => `${count} ${count === 1 ? one : many}`;

export function actionColumns(action: SceneAction, phaseCount: number): { first: number; last: number } | null {
  if (phaseCount === 0) return null;
  const start = action.startPhase ?? 0;
  const end = action.endPhase ?? start;
  const clamp = (value: number) => Math.min(Math.max(value, 0), phaseCount - 1);
  return { first: clamp(Math.min(start, end)), last: clamp(Math.max(start, end)) };
}

export const toNumber = (text: string): number => {
  const value = Number(text);
  return Number.isFinite(value) ? value : 0;
};
