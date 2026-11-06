import type { Backend } from '../../backend';
import {
  aliasesJson, objectivesJson, parseAliases, parseObjectives, parseStages, stagesJson,
  type QuestAlias, type QuestObjective, type QuestStage, type StageJump,
} from './questModel';

export interface QuestSnapshot {
  stages: QuestStage[];
  objectives: QuestObjective[];
  aliases: QuestAlias[];
  jumps: StageJump[];
}

export interface FragmentBinding {
  stage: number;
  function: string;
  fragmentScript: string;
}

const unwrap = (raw: string | object): unknown => (typeof raw === 'string' ? JSON.parse(raw) : raw);

const BLOCK_COMMENT = /;\/[\s\S]*?\/;/g;
const LINE_COMMENT = /;.*$/gm;
const FUNCTION_BODY = /\bFunction\s+(\w+)\s*\([^)]*\)(?![^\n]*\bnative\b)([\s\S]*?)\bEndFunction\b/gi;
const SET_STAGE = /\b(?:SetStage|SetCurrentStageID)\s*\(\s*(\d+)\s*\)/gi;

function functionBodies(source: string): Map<string, string> {
  const code = source.replace(BLOCK_COMMENT, '').replace(LINE_COMMENT, '');
  const bodies = new Map<string, string>();
  for (const match of code.matchAll(FUNCTION_BODY)) bodies.set(match[1].toLowerCase(), match[2]);
  return bodies;
}

export function parseStageJumps(bindings: FragmentBinding[], sourceByScript: Map<string, string>): StageJump[] {
  const jumps: StageJump[] = [];
  for (const binding of bindings) {
    const source = sourceByScript.get(binding.fragmentScript);
    const body = source === undefined ? undefined : functionBodies(source).get(binding.function.toLowerCase());
    if (body === undefined) continue;
    for (const match of body.matchAll(SET_STAGE)) {
      jumps.push({ from: binding.stage, to: Number(match[1]), function: binding.function });
    }
  }
  return jumps;
}

async function fragmentSource(b: Backend, scriptName: string, importsDirectory?: string): Promise<string> {
  try {
    const source = (unwrap(await b.GetQuestScript(scriptName, importsDirectory)) as { source?: unknown } | null)?.source;
    return typeof source === 'string' ? source : '';
  } catch {
    return '';
  }
}

export async function readStageJumps(b: Backend, plugin: string, formKey: string,
                                     importsDirectory?: string): Promise<StageJump[]> {
  const bindings = unwrap(await b.GetQuestFragments(plugin, formKey)) as FragmentBinding[];
  const scripts = [...new Set(bindings.map(binding => binding.fragmentScript))];
  const sourceByScript = new Map(await Promise.all(
    scripts.map(async script => [script, await fragmentSource(b, script, importsDirectory)] as const)));
  return parseStageJumps(bindings, sourceByScript);
}

export async function readQuest(b: Backend, plugin: string, formKey: string): Promise<QuestSnapshot> {
  const context = { language: 'en' };
  const [stages, objectives, aliases, jumps] = await Promise.all([
    b.GetQuestStages(plugin, formKey, context),
    b.GetQuestObjectives(plugin, formKey, context),
    b.GetQuestAliases(plugin, formKey),
    readStageJumps(b, plugin, formKey).catch((): StageJump[] => []),
  ]);
  return {
    stages: parseStages(unwrap(stages)),
    objectives: parseObjectives(unwrap(objectives)),
    aliases: parseAliases(unwrap(aliases)),
    jumps,
  };
}

export const writeStages = (b: Backend, plugin: string, formKey: string, stages: QuestStage[]) =>
  b.SetQuestStages(plugin, formKey, stagesJson(stages));

export const writeObjectives = (b: Backend, plugin: string, formKey: string, objectives: QuestObjective[]) =>
  b.SetQuestObjectives(plugin, formKey, objectivesJson(objectives));

export const writeAliases = (b: Backend, plugin: string, formKey: string, aliases: QuestAlias[]) =>
  b.SetQuestAliases(plugin, formKey, aliasesJson(aliases));
