import type { QuestTemplate } from '../templateTypes';

export const KILL_A_TARGET: QuestTemplate = {
  id: 'kill-a-target',
  title: 'Kill a target',
  summary: 'Go to a place, find a named enemy, defeat them, and report back to the quest giver.',
  questions: [
    { id: 'name', kind: 'text', label: 'What is the quest\'s internal name?', help: 'wizard-name', placeholder: 'Mill Bandit' },
    { id: 'questGiver', kind: 'record', label: 'Who gives the quest?', help: 'wizard-quest-giver', recordType: 'NPC_' },
    { id: 'target', kind: 'record', label: 'Which enemy does the player defeat?', help: 'wizard-target', recordType: 'NPC_' },
    { id: 'location', kind: 'record', label: 'Where is the enemy?', help: 'wizard-target-location', recordType: 'LCTN' },
    { id: 'reward', kind: 'text', label: 'What does the player get as a reward?', help: 'wizard-reward', placeholder: '50 caps' },
  ],
  stages: [
    { index: 10, journal: 'Go to {location} and find {target}.' },
    { index: 20, journal: 'You defeated {target}. Report back to {questGiver}.' },
    { index: 30, journal: 'You reported to {questGiver}. Reward: {reward}.', completes: true },
  ],
  objectives: [
    { index: 10, text: 'Find {target} at {location}', targets: ['location', 'target'] },
    { index: 20, text: 'Report back to {questGiver}', targets: ['questGiver'] },
  ],
  aliases: [
    { slot: 'questGiver', name: 'QuestGiver', kind: 'reference' },
    { slot: 'target', name: 'Target', kind: 'reference' },
    { slot: 'location', name: 'Location', kind: 'location' },
  ],
};
