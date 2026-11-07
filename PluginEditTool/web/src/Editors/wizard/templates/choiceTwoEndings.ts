import type { QuestTemplate } from '../templateTypes';

export const CHOICE_TWO_ENDINGS: QuestTemplate = {
  id: 'choice-two-endings',
  title: 'Choice with two endings',
  summary: 'Hear a choice from the quest giver, make it, and see one of two endings.',
  questions: [
    { id: 'name', kind: 'text', label: 'What is the quest\'s internal name?', help: 'wizard-name', placeholder: 'The Raider Deal' },
    { id: 'questGiver', kind: 'record', label: 'Who gives the quest?', help: 'wizard-quest-giver', recordType: 'NPC_' },
    { id: 'optionA', kind: 'text', label: 'What is the first choice?', help: 'wizard-option-a', placeholder: 'Spare the raiders' },
    { id: 'optionB', kind: 'text', label: 'What is the second choice?', help: 'wizard-option-b', placeholder: 'Hand the raiders over' },
    { id: 'reward', kind: 'text', label: 'What does the player get for the first choice?', help: 'wizard-reward', placeholder: '50 caps' },
    { id: 'rewardB', kind: 'text', label: 'What does the player get for the second choice?', help: 'wizard-reward-b', placeholder: '20 caps' },
  ],
  stages: [
    { index: 10, journal: 'Talk to {questGiver} to hear your choice.' },
    { index: 20, journal: 'Decide between two options: {optionA} or {optionB}.' },
    { index: 30, journal: 'You chose: {optionA}. Reward: {reward}.', completes: true },
    { index: 40, journal: 'You chose: {optionB}. Reward: {rewardB}.', completes: true },
  ],
  objectives: [
    { index: 10, text: 'Talk to {questGiver}', targets: ['questGiver'] },
    { index: 20, text: 'Choose: {optionA} or {optionB}', targets: ['questGiver'] },
  ],
  aliases: [
    { slot: 'questGiver', name: 'QuestGiver', kind: 'reference' },
  ],
  choices: {
    speaker: 'questGiver',
    fromStage: 10,
    toStage: 20,
    options: [
      { prompt: '{optionA}', reply: 'Then it is done. Your reward is {reward}.', setsStage: 30 },
      { prompt: '{optionB}', reply: 'Then it is done. Your reward is {rewardB}.', setsStage: 40 },
    ],
  },
};
