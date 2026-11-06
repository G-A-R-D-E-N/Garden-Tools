import type { QuestTemplate } from '../templateTypes';

export const ESCORT: QuestTemplate = {
  id: 'escort',
  title: 'Escort',
  summary: 'Walk a named character to a place and keep them alive on the way. Report back for the reward.',
  questions: [
    { id: 'name', kind: 'text', label: 'What is the quest\'s internal name?', help: 'wizard-name', placeholder: 'Safe Passage' },
    { id: 'questGiver', kind: 'record', label: 'Who gives the quest?', help: 'wizard-quest-giver', recordType: 'NPC_' },
    { id: 'escortee', kind: 'record', label: 'Who does the player escort?', help: 'wizard-escortee', recordType: 'NPC_' },
    { id: 'destination', kind: 'record', label: 'Where does the escortee go?', help: 'wizard-destination', recordType: 'LCTN' },
    { id: 'reward', kind: 'text', label: 'What does the player get as a reward?', help: 'wizard-reward', placeholder: '50 caps' },
  ],
  stages: [
    { index: 10, journal: 'Find {escortee} and agree to take them to {destination}.' },
    { index: 20, journal: 'Take {escortee} to {destination}.' },
    { index: 30, journal: '{escortee} reached {destination}. Report to {questGiver} for your reward: {reward}.', completes: true },
    { index: 90, journal: '{escortee} died. The quest failed.', fails: true },
  ],
  objectives: [
    { index: 10, text: 'Find {escortee}', targets: ['escortee'] },
    { index: 20, text: 'Take {escortee} to {destination}', targets: ['destination'] },
    { index: 30, text: 'Report to {questGiver}', targets: ['questGiver'] },
  ],
  aliases: [
    { slot: 'questGiver', name: 'QuestGiver', kind: 'reference' },
    { slot: 'escortee', name: 'Escortee', kind: 'reference' },
    { slot: 'destination', name: 'Destination', kind: 'location' },
  ],
};
