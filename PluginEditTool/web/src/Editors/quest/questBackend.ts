import type { Backend } from '../../backend';
import {
  aliasesJson, normalizeFormKey, objectivesJson, parseAliases, parseObjectives, parseStages, stagesJson,
  type QuestAlias, type QuestObjective, type QuestStage, type StageJump,
} from './questModel';

export interface FragmentScript {
  name: string;
  parses: boolean | null;
}

export interface FragmentRead {
  jumps: StageJump[];
  scripts: FragmentScript[];
}

export type ScopeKind = 'quest' | 'dialogue' | 'scene';
export type QuestScope = Map<string, ScopeKind>;

export interface QuestSnapshot {
  stages: QuestStage[];
  objectives: QuestObjective[];
  aliases: QuestAlias[];
  jumps: StageJump[];
  fragmentScripts: FragmentScript[] | null;
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

interface ScriptRead {
  source: string;
  parses: boolean | null;
}

async function readScript(b: Backend, scriptName: string, importsDirectory?: string): Promise<ScriptRead> {
  try {
    const script = unwrap(await b.GetQuestScript(scriptName, importsDirectory)) as { source?: unknown; parses?: unknown } | null;
    const source = script?.source;
    const parses = script?.parses;
    return {
      source: typeof source === 'string' ? source : '',
      parses: typeof parses === 'boolean' ? parses : null,
    };
  } catch {
    return { source: '', parses: null };
  }
}

export async function readFragments(b: Backend, plugin: string, formKey: string,
                                    importsDirectory?: string): Promise<FragmentRead> {
  const bindings = unwrap(await b.GetQuestFragments(plugin, formKey)) as FragmentBinding[];
  const names = [...new Set(bindings.map(binding => binding.fragmentScript))];
  const reads = new Map(await Promise.all(
    names.map(async name => [name, await readScript(b, name, importsDirectory)] as const)));
  return {
    jumps: parseStageJumps(bindings, new Map([...reads].map(([name, read]) => [name, read.source] as const))),
    scripts: names.map(name => ({ name, parses: reads.get(name)?.parses ?? null })),
  };
}

interface DialogueRows {
  branches?: { formKey: string }[];
  topics?: { formKey: string; infos?: { formKey: string }[] }[];
}

interface ReferenceRow {
  Plugin: string;
  FormKey: string;
  Type: string;
}

export async function readQuestScope(b: Backend, plugin: string, formKey: string): Promise<QuestScope> {
  const scope: QuestScope = new Map<string, ScopeKind>([[normalizeFormKey(formKey), 'quest']]);
  const dialogue = unwrap(await b.GetDialogue(plugin, formKey)) as DialogueRows;
  const topics = dialogue.topics ?? [];
  for (const row of [...(dialogue.branches ?? []), ...topics, ...topics.flatMap(topic => topic.infos ?? [])]) {
    scope.set(normalizeFormKey(row.formKey), 'dialogue');
  }
  const references = unwrap(await b.GetReferencedBy(formKey)) as ReferenceRow[];
  const scenes = references.filter(row => row.Type === 'SCEN' && normalizeFormKey(row.Plugin) === normalizeFormKey(plugin));
  const parents = await Promise.all(scenes.map(async row => ({
    sceneKey: row.FormKey,
    parent: await b.GetScene(plugin, row.FormKey).then(
      scene => (unwrap(scene) as { parentQuest?: string | null } | null)?.parentQuest, () => null),
  })));
  for (const { sceneKey, parent } of parents) {
    if (parent && normalizeFormKey(parent) === normalizeFormKey(formKey)) scope.set(normalizeFormKey(sceneKey), 'scene');
  }
  return scope;
}

export async function readQuest(b: Backend, plugin: string, formKey: string): Promise<QuestSnapshot> {
  const context = { language: 'en' };
  const [stages, objectives, aliases, fragments] = await Promise.all([
    b.GetQuestStages(plugin, formKey, context),
    b.GetQuestObjectives(plugin, formKey, context),
    b.GetQuestAliases(plugin, formKey),
    readFragments(b, plugin, formKey).catch((): FragmentRead | null => null),
  ]);
  return {
    stages: parseStages(unwrap(stages)),
    objectives: parseObjectives(unwrap(objectives)),
    aliases: parseAliases(unwrap(aliases)),
    jumps: fragments?.jumps ?? [],
    fragmentScripts: fragments?.scripts ?? null,
  };
}

export const writeStages = (b: Backend, plugin: string, formKey: string, stages: QuestStage[]) =>
  b.SetQuestStages(plugin, formKey, stagesJson(stages));

export const writeObjectives = (b: Backend, plugin: string, formKey: string, objectives: QuestObjective[]) =>
  b.SetQuestObjectives(plugin, formKey, objectivesJson(objectives));

export const writeAliases = (b: Backend, plugin: string, formKey: string, aliases: QuestAlias[]) =>
  b.SetQuestAliases(plugin, formKey, aliasesJson(aliases));
