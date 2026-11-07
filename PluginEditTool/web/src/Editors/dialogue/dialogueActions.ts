import type { Backend } from '../../backend';
import type { UndoEntry } from '../useUndoStack';
import {
  responseSpec,
  type DialogueBranch, type DialogueInfo, type DialogueResponse, type DialogueTopic, type LineChange, type NewTopic,
} from './dialogueModel';

export interface Created {
  ok: boolean;
  formKey: string | null;
}

export interface DialogueActions {
  createBranch(editorId: string): Promise<Created>;
  createTopic(topic: NewTopic): Promise<Created>;
  setBranchStart(branch: DialogueBranch, topic: DialogueTopic): Promise<boolean>;
  addReply(topic: DialogueTopic, text: string): Promise<boolean>;
  saveLine(info: DialogueInfo, index: number, change: LineChange): Promise<boolean>;
  setFlags(info: DialogueInfo, flags: number): Promise<boolean>;
  removeReply(info: DialogueInfo): Promise<boolean>;
}

export type Write = (call: () => Promise<unknown>) => Promise<boolean>;

const lineAfter = (response: DialogueResponse, change: LineChange): DialogueResponse => ({
  ...response,
  text: change.text ?? response.text,
  number: change.number ?? response.number,
  emotion: change.emotion === undefined ? response.emotion : change.emotion,
});

export function createdFormKey(made: unknown): string | null {
  if (typeof made === 'string') {
    try {
      return createdFormKey(JSON.parse(made) as unknown);
    } catch {
      return made === '' ? null : made;
    }
  }
  const key = (made as { formKey?: unknown } | null)?.formKey;
  return typeof key === 'string' ? key : null;
}

export function buildActions(b: Backend, plugin: string, quest: string, write: Write,
  push: (entry: UndoEntry) => void): DialogueActions {
  const setInfo = (info: DialogueInfo, spec: Record<string, unknown>) =>
    write(() => b.SetInfo(plugin, info.formKey, spec));

  const create = async (call: () => Promise<unknown>): Promise<Created> => {
    const made: { formKey: string | null } = { formKey: null };
    const ok = await write(async () => { made.formKey = createdFormKey(await call()); });
    return { ok, formKey: made.formKey };
  };

  const editInfo = async (info: DialogueInfo, change: Record<string, unknown>, before: Record<string, unknown>,
    label: string): Promise<boolean> => {
    const ok = await setInfo(info, change);
    if (ok) push({ label, undo: () => void setInfo(info, before), redo: () => void setInfo(info, change) });
    return ok;
  };

  return {
    createBranch: editorId => create(() => b.CreateDialogueBranch(plugin, quest, { editorId })),

    createTopic: topic => {
      const spec: Record<string, unknown> = { editorId: topic.editorId, name: topic.name, priority: topic.priority };
      if (topic.branch) spec.branch = topic.branch;
      return create(() => b.CreateDialogueTopic(plugin, quest, spec));
    },

    async setBranchStart(branch, topic) {
      const previous = branch.topic;
      const ok = await write(() => b.SetDialogueBranchStart(plugin, branch.formKey, topic.formKey));
      if (ok && previous) {
        push({
          label: 'Change start topic',
          undo: () => void write(() => b.SetDialogueBranchStart(plugin, branch.formKey, previous)),
          redo: () => void write(() => b.SetDialogueBranchStart(plugin, branch.formKey, topic.formKey)),
        });
      }
      return ok;
    },

    addReply: (topic, text) => write(() => b.CreateInfo(plugin, topic.formKey, { responses: [{ text }] })),

    saveLine: (info, index, change) => editInfo(info,
      { responses: info.responses.map((r, i) => responseSpec(i === index ? lineAfter(r, change) : r)) },
      { responses: info.responses.map(r => responseSpec(r)) },
      'Change line'),

    setFlags: (info, flags) => editInfo(info, { flags }, { flags: info.flags }, 'Change reply flags'),

    removeReply: info => write(() => b.RemoveInfo(plugin, info.formKey)),
  };
}
