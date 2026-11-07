import { buildQuestCalls, startGameCall, type Answers, type BackendCall, type FormKeys } from './questCalls';
import { buildSceneCalls } from './sceneCalls';
import type { QuestTemplate } from './templateTypes';

export function buildCreateCalls(template: QuestTemplate, answers: Answers, formKeys: FormKeys): BackendCall[] {
  return [
    ...buildQuestCalls(template, answers, formKeys),
    ...buildSceneCalls(template, answers, formKeys),
    startGameCall(answers.plugin),
  ];
}
