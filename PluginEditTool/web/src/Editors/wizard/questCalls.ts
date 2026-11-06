import type { Backend } from '../../backend';
import {
  aliasesJson, objectivesJson, stagesJson, type QuestAlias, type QuestObjective, type QuestStage,
} from '../quest/questModel';
import type { QuestTemplate } from './templateTypes';

export const NEW_RECORD = '$newQuest';

export type Answers = Record<string, string>;
export type BackendMethod = 'CreateRecord' | 'SetQuestStages' | 'SetQuestObjectives' | 'SetQuestAliases';
export interface BackendCall { method: BackendMethod; args: unknown[]; }
export interface QuestRun { formKey: string | null; }

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

export function buildQuestCalls(template: QuestTemplate, answers: Answers): BackendCall[] {
  const plugin = answers.plugin;
  const stages: QuestStage[] = template.stages.map(stage => ({
    index: stage.index,
    flags: 0,
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
  }));
  const editorId = editorIdFrom(answers.name);
  if (!editorId) throw new Error('The quest name needs at least one letter or number.');
  return [
    { method: 'CreateRecord', args: [plugin, 'QUST', { schema: true, editorId }] },
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
