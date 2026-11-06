import type { ConditionDto } from '../../backend';

export interface DialogueResponse {
  text?: string;
  textId?: number;
  scriptNotes?: string;
  emotion: string | null;
  sound: string | null;
  number: number;
  interruptPercent: number;
  cameraTargetAlias: number;
  cameraLocationAlias: number;
}

export interface DialogueFragment {
  index: number;
  script: string;
  function: string;
}

export interface DialogueInfo {
  formKey: string;
  flags: number;
  previous: string | null;
  responses: DialogueResponse[];
  conditions: ConditionDto[];
  fragments: DialogueFragment[];
}

export interface DialogueTopic {
  formKey: string;
  editorId: string;
  priority: number;
  branch: string | null;
  name?: string;
  nameId?: number;
  infos: DialogueInfo[];
}

export interface DialogueBranch {
  formKey: string;
  editorId: string;
  topic: string | null;
}

export interface DialogueData {
  branches: DialogueBranch[];
  topics: DialogueTopic[];
}

export interface LineRef {
  info: string;
  index: number;
}

export interface LineChange {
  text?: string;
  number?: number;
  emotion?: string | null;
}

export interface NewTopic {
  editorId: string;
  name: string;
  priority: number;
  branch: string | null;
}

export const REPLY_FLAGS = [
  { bit: 0x0002, label: 'Random', help: 'dialogue-flag-random' },
  { bit: 0x0008, label: 'Only the player can start the child replies', help: 'dialogue-flag-player-only' },
  { bit: 0x0020, label: 'Random end', help: 'dialogue-flag-random-end' },
  { bit: 0x0100, label: "Child replies don't reuse this reply's reset time", help: 'dialogue-flag-reset' },
  { bit: 0x0200, label: 'Every child reply is random', help: 'dialogue-flag-force-random' },
  { bit: 0x0800, label: "Don't finish all replies before repeating", help: 'dialogue-flag-repeat' },
] as const;

const CP1252_HIGH = new Set([
  0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0x8d, 0x17d,
  0x8f, 0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d,
  0x17e, 0x178,
]);

export function textProblem(text: string): string | null {
  for (const ch of text) {
    const code = ch.codePointAt(0) ?? 0;
    if (code < 0x80 || (code >= 0xa0 && code <= 0xff) || CP1252_HIGH.has(code)) continue;
    const hex = code.toString(16).toUpperCase().padStart(4, '0');
    return `The character "${ch}" (U+${hex}) cannot be stored in game text. Replace it with a standard letter or punctuation mark.`;
  }
  return null;
}

export function parseDialogue(raw: string | object): DialogueData {
  const value: unknown = typeof raw === 'string' ? JSON.parse(raw) : raw;
  const data = value as Partial<DialogueData> | null;
  if (!data || !Array.isArray(data.branches) || !Array.isArray(data.topics)) {
    throw new Error('GetDialogue returned an unexpected shape.');
  }
  return data as DialogueData;
}

export function pluginOf(formKey: string): string {
  return formKey.slice(formKey.indexOf(':') + 1);
}

export const topicTitle = (topic: DialogueTopic): string => topic.name || topic.editorId || topic.formKey;

export const topicLine = (topic: DialogueTopic): string =>
  topic.name || (topic.nameId !== undefined ? `(text ${topic.nameId} in the string table)` : 'No line text yet.');

export const lineText = (response: DialogueResponse): string =>
  response.text ?? (response.textId !== undefined ? `(text ${response.textId} in the string table)` : '');

export const emotionLabel = (response: DialogueResponse, labels: Record<string, string>): string =>
  response.emotion ? (labels[response.emotion] ?? response.emotion) : 'No emotion';

export const isLocalized = (info: DialogueInfo): boolean => info.responses.some(r => r.textId !== undefined);

export function responseSpec(response: DialogueResponse): Record<string, unknown> {
  const spec: Record<string, unknown> = {
    number: response.number,
    interruptPercent: response.interruptPercent,
    cameraTargetAlias: response.cameraTargetAlias,
    cameraLocationAlias: response.cameraLocationAlias,
  };
  if (response.text !== undefined) spec.text = response.text;
  if (response.scriptNotes !== undefined) spec.scriptNotes = response.scriptNotes;
  if (response.emotion) spec.emotion = response.emotion;
  if (response.sound) spec.sound = response.sound;
  return spec;
}
