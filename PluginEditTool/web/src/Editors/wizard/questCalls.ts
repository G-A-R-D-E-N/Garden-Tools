import type { Backend } from '../../backend';
import {
  aliasesJson, objectivesJson, stagesJson, type AliasKind, type QuestAlias, type QuestObjective, type QuestStage,
} from '../quest/questModel';
import type { QuestTemplate } from './templateTypes';

export const NEW_RECORD = '$newQuest';

export type Answers = Record<string, string>;
export type FormKeys = Record<string, string>;
export type BackendMethod = 'CreateRecord' | 'SetField' | 'SetQuestStages' | 'SetQuestObjectives' | 'SetQuestAliases';
export interface BackendCall { method: BackendMethod; args: unknown[]; }
export interface QuestRun { formKey: string | null; }

const RUN_WHEN_STAGE_STARTS = 0x02;
const START_GAME_ENABLED = '1';
const FILL_BY_RECORD_TYPE: Record<string, { alias: AliasKind; key: string }> = {
  NPC_: { alias: 'reference', key: 'uniqueActor' },
  LCTN: { alias: 'location', key: 'specificLocation' },
  MISC: { alias: 'reference', key: 'createReference' },
};

const fill = (text: string, answers: Answers): string => text.replace(/\{(\w+)\}/g, (_, key: string) => {
  const value = answers[key];
  if (value === undefined) throw new Error(`No answer for {${key}}.`);
  return value;
});

const editorIdFrom = (name: string): string => name.trim().replace(/[^A-Za-z0-9]+/g, '_').replace(/^_|_$/g, '');

const aliasIndex = (template: QuestTemplate, slot: string): number => {
  const index = template.aliases.findIndex(alias => alias.slot === slot);
  if (index < 0) throw new Error(`Template ${template.id} has no alias slot ${slot}.`);
  return index;
};

const locationAliasId = (template: QuestTemplate): number | undefined => {
  const index = template.aliases.findIndex(alias => alias.kind === 'location');
  return index < 0 ? undefined : index;
};

const fillRule = (template: QuestTemplate, slot: string) => {
  const question = template.questions.find(item => item.id === slot);
  const alias = template.aliases.find(item => item.slot === slot);
  if (question?.kind !== 'record' || alias === undefined) return undefined;
  const rule = FILL_BY_RECORD_TYPE[question.recordType];
  if (!rule || rule.alias !== alias.kind) return undefined;
  if (rule.key === 'createReference' && locationAliasId(template) === undefined) return undefined;
  return rule;
};

export const isLinked = (template: QuestTemplate, slot: string): boolean => fillRule(template, slot) !== undefined;

const linkFill = (template: QuestTemplate, slot: string, formKeys: FormKeys): Record<string, unknown> | undefined => {
  const rule = fillRule(template, slot);
  if (!rule) return undefined;
  const formKey = formKeys[slot];
  if (!formKey) throw new Error(`No record was picked for {${slot}}.`);
  if (rule.key === 'createReference') {
    return { createReference: { object: formKey, alias: locationAliasId(template), create: 'At' } };
  }
  return { [rule.key]: formKey };
};

export function buildQuestCalls(template: QuestTemplate, answers: Answers, formKeys: FormKeys): BackendCall[] {
  const plugin = answers.plugin;
  const stages: QuestStage[] = template.stages.map((stage, position) => ({
    index: stage.index,
    flags: position === 0 ? RUN_WHEN_STAGE_STARTS : 0,
    logEntries: [{ logText: fill(stage.journal, answers), note: '', flags: stage.completes ? 1 : stage.fails ? 2 : 0 }],
  }));
  const objectives: QuestObjective[] = template.objectives.map(objective => ({
    index: objective.index,
    flags: 0,
    displayText: fill(objective.text, answers),
    targets: objective.targets.map(slot => {
      const alias = aliasIndex(template, slot);
      return { alias, aliasName: template.aliases[alias].name, flags: 0 };
    }),
  }));
  const aliases: QuestAlias[] = template.aliases.map((alias, id) => ({
    id, kind: alias.kind, name: alias.name, flags: null, collectionEntries: [],
    fill: linkFill(template, alias.slot, formKeys),
  }));
  const editorId = editorIdFrom(answers.name);
  if (!editorId) throw new Error('The quest name needs at least one letter or number.');
  return [
    { method: 'CreateRecord', args: [plugin, 'QUST', { schema: true, editorId }] },
    { method: 'SetField', args: [plugin, NEW_RECORD, 'QUST\\Flags', START_GAME_ENABLED] },
    { method: 'SetQuestStages', args: [plugin, NEW_RECORD, stagesJson(stages)] },
    { method: 'SetQuestAliases', args: [plugin, NEW_RECORD, aliasesJson(aliases)] },
    { method: 'SetQuestObjectives', args: [plugin, NEW_RECORD, objectivesJson(objectives)] },
  ];
}

export async function runQuestCalls(b: Backend, calls: BackendCall[], run: QuestRun): Promise<void> {
  for (const call of calls) {
    const args = call.args.map(arg => (arg === NEW_RECORD ? run.formKey : arg));
    const send = b[call.method] as unknown as (...params: unknown[]) => Promise<unknown>;
    const raw = await send.apply(b, args);
    if (call.method === 'CreateRecord') {
      const created = (typeof raw === 'string' ? JSON.parse(raw) : raw) as { formKey?: string } | null;
      if (!created?.formKey) throw new Error('The game did not confirm that the quest was made.');
      run.formKey = created.formKey;
    }
  }
}
