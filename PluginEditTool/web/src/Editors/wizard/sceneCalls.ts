import { aliasIndex, editorIdFrom, fill, NEW_RECORD, type Answers, type BackendCall } from './questCalls';
import type { QuestTemplate, TemplateSceneAction } from './templateTypes';

export const NEW_SCENE = '$newScene';
export const NEW_BRANCH = '$newBranch';
export const NEW_TOPIC = '$newTopic';

const SCENE_ACTION_TYPE: Record<TemplateSceneAction['kind'], number> = { dialogue: 0 };
const TOPIC_PRIORITY = 50;

export function buildSceneCalls(template: QuestTemplate, answers: Answers): BackendCall[] {
  const { dialogue, scene } = template;
  const plugin = answers.plugin;
  const base = editorIdFrom(answers.name);
  const calls: BackendCall[] = [];
  if (dialogue) {
    calls.push(
      { method: 'CreateDialogueBranch', args: [plugin, NEW_RECORD, { editorId: `${base}_Branch` }], produces: NEW_BRANCH },
      {
        method: 'CreateDialogueTopic',
        args: [plugin, NEW_RECORD, {
          editorId: `${base}_Topic`, name: fill(dialogue.topic, answers), priority: TOPIC_PRIORITY, branch: NEW_BRANCH,
        }],
        produces: NEW_TOPIC,
      },
      { method: 'CreateInfo', args: [plugin, NEW_TOPIC, { responses: dialogue.replies.map(text => ({ text: fill(text, answers) })) }] },
    );
  }
  if (scene) {
    calls.push({ method: 'CreateScene', args: [plugin, NEW_RECORD, { editorId: `${base}_Scene` }], produces: NEW_SCENE });
    scene.phases.forEach((name, index) => calls.push({ method: 'AddScenePhase', args: [plugin, NEW_SCENE, index, { name }] }));
    scene.actors.forEach((slot, index) => calls.push(
      { method: 'AddSceneActor', args: [plugin, NEW_SCENE, index] },
      { method: 'SetSceneActor', args: [plugin, NEW_SCENE, index, { alias: aliasIndex(template, slot) }] },
    ));
    scene.actions.forEach((action, index) => calls.push(
      { method: 'AddSceneAction', args: [plugin, NEW_SCENE, index, SCENE_ACTION_TYPE[action.kind]] },
      {
        method: 'SetSceneAction',
        args: [plugin, NEW_SCENE, index, {
          actorAlias: aliasIndex(template, action.actor), startPhase: action.phase, endPhase: action.phase, topic: NEW_TOPIC,
        }],
      },
    ));
  }
  return calls;
}

export function describeSceneAndDialogue(template: QuestTemplate, answers: Answers): string[] {
  const { dialogue, scene } = template;
  const lines: string[] = [];
  if (scene) {
    lines.push(`A scene with ${scene.phases.length} parts: ${scene.phases.join(', then ')}.`);
    for (const action of scene.actions) {
      lines.push(`${answers[action.actor]} speaks in the ${scene.phases[action.phase]} part.`);
    }
  }
  if (dialogue) {
    lines.push(`The player says: "${fill(dialogue.topic, answers)}"`);
    lines.push(`The reply: ${dialogue.replies.map(text => `"${fill(text, answers)}"`).join(' ')}`);
  }
  return lines;
}
