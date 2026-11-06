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

const unwrap = (raw: string | object): unknown => (typeof raw === 'string' ? JSON.parse(raw) : raw);

export async function readQuest(b: Backend, plugin: string, formKey: string): Promise<QuestSnapshot> {
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

export const writeStages = (b: Backend, plugin: string, formKey: string, stages: QuestStage[]) =>
  b.SetQuestStages(plugin, formKey, stagesJson(stages));

export const writeObjectives = (b: Backend, plugin: string, formKey: string, objectives: QuestObjective[]) =>
  b.SetQuestObjectives(plugin, formKey, objectivesJson(objectives));

export const writeAliases = (b: Backend, plugin: string, formKey: string, aliases: QuestAlias[]) =>
  b.SetQuestAliases(plugin, formKey, aliasesJson(aliases));
