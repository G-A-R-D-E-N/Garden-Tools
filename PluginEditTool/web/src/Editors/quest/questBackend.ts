import type { Backend } from '../../backend';
import {
  aliasesJson, objectivesJson, parseAliases, parseObjectives, parseStages, stagesJson,
  type QuestAlias, type QuestObjective, type QuestStage,
} from './questModel';

export interface QuestSnapshot {
  stages: QuestStage[];
  objectives: QuestObjective[];
  aliases: QuestAlias[];
}

export const questPlugin = (formKey: string): string => formKey.slice(formKey.indexOf(':') + 1);

const unwrap = (raw: string | object): unknown => (typeof raw === 'string' ? JSON.parse(raw) : raw);

export async function readQuest(b: Backend, formKey: string): Promise<QuestSnapshot> {
  const plugin = questPlugin(formKey);
  const context = { language: 'en' };
  const [stages, objectives, aliases] = await Promise.all([
    b.GetQuestStages(plugin, formKey, context),
    b.GetQuestObjectives(plugin, formKey, context),
    b.GetQuestAliases(plugin, formKey),
  ]);
  return {
    stages: parseStages(unwrap(stages)),
    objectives: parseObjectives(unwrap(objectives)),
    aliases: parseAliases(unwrap(aliases)),
  };
}

export const writeStages = (b: Backend, formKey: string, stages: QuestStage[]) =>
  b.SetQuestStages(questPlugin(formKey), formKey, stagesJson(stages));

export const writeObjectives = (b: Backend, formKey: string, objectives: QuestObjective[]) =>
  b.SetQuestObjectives(questPlugin(formKey), formKey, objectivesJson(objectives));

export const writeAliases = (b: Backend, formKey: string, aliases: QuestAlias[]) =>
  b.SetQuestAliases(questPlugin(formKey), formKey, aliasesJson(aliases));
