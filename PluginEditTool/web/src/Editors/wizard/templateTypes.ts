import type { AliasKind } from '../quest/questModel';

export interface PluginQuestion { id: 'plugin'; kind: 'plugin'; label: string; help: string; }
export interface TextQuestion { id: string; kind: 'text'; label: string; help: string; placeholder?: string; }
export interface RecordQuestion { id: string; kind: 'record'; label: string; help: string; recordType: string; }
export type TemplateQuestion = TextQuestion | RecordQuestion;
export type WizardStep = PluginQuestion | TemplateQuestion;

export interface TemplateStage { index: number; journal: string; completes?: boolean; fails?: boolean; }
export interface TemplateObjective { index: number; text: string; targets: string[]; }
export interface TemplateAlias { slot: string; name: string; kind: AliasKind; }
export interface TemplateSceneAction { kind: 'dialogue'; phase: number; actor: string; }
export interface TemplateScene { phases: string[]; actors: string[]; actions: TemplateSceneAction[]; }
export interface TemplateDialogue { topic: string; replies: string[]; }
export interface TemplateChoice { prompt: string; reply: string; setsStage: number; }
export interface TemplateChoices { speaker: string; fromStage: number; toStage: number; options: TemplateChoice[]; }

export interface QuestTemplate {
  id: string;
  title: string;
  summary: string;
  questions: TemplateQuestion[];
  stages: TemplateStage[];
  objectives: TemplateObjective[];
  aliases: TemplateAlias[];
  scene?: TemplateScene;
  dialogue?: TemplateDialogue;
  choices?: TemplateChoices;
}
