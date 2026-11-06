import type { QuestTemplate } from '../templateTypes';

export const TALK_TO_SOMEONE: QuestTemplate = {
  id: 'talk-to-someone',
  title: 'Talk to someone',
  summary: 'Find a named person, speak with them, and get your reward.',
  questions: [
    { id: 'name', kind: 'text', label: 'What is the quest\'s internal name?', help: 'wizard-name', placeholder: 'The Old Friend' },
    { id: 'person', kind: 'record', label: 'Who does the player talk to?', help: 'wizard-person', recordType: 'NPC_' },
    { id: 'location', kind: 'record', label: 'Where does the player find them?', help: 'wizard-person-location', recordType: 'LCTN' },
    { id: 'reward', kind: 'text', label: 'What does the player get as a reward?', help: 'wizard-reward', placeholder: '50 caps' },
  ],
  stages: [
    { index: 10, journal: 'Find {person} at {location} and talk to them.' },
    { index: 20, journal: 'You talked to {person}. Reward: {reward}.', completes: true },
  ],
  objectives: [
    { index: 10, text: 'Talk to {person} at {location}', targets: ['person', 'location'] },
  ],
  aliases: [
    { slot: 'person', name: 'Person', kind: 'reference' },
    { slot: 'location', name: 'Location', kind: 'location' },
  ],
};
