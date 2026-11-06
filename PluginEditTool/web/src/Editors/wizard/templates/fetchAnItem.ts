import type { QuestTemplate } from '../templateTypes';

export const FETCH_AN_ITEM: QuestTemplate = {
  id: 'fetch-an-item',
  title: 'Fetch an item',
  summary: 'Go to a place, pick up an item, and bring it back to the quest giver.',
  questions: [
    { id: 'name', kind: 'text', label: 'What is the quest\'s internal name?', help: 'wizard-name', placeholder: 'The Lost Locket' },
    { id: 'questGiver', kind: 'record', label: 'Who gives the quest?', help: 'wizard-quest-giver', recordType: 'NPC_' },
    { id: 'location', kind: 'record', label: 'Where is the item?', help: 'wizard-location', recordType: 'LCTN' },
    { id: 'item', kind: 'record', label: 'Which item does the player fetch?', help: 'wizard-item', recordType: 'MISC' },
    { id: 'reward', kind: 'text', label: 'What does the player get as a reward?', help: 'wizard-reward', placeholder: '50 caps' },
  ],
  stages: [
    { index: 10, journal: 'Go to {location} and find {item}.' },
    { index: 20, journal: 'You found {item}. Take it back to {questGiver}.' },
    { index: 30, journal: 'You gave {item} to {questGiver}. Reward: {reward}.', completes: true },
  ],
  objectives: [
    { index: 10, text: 'Find {item} at {location}', targets: ['location'] },
    { index: 20, text: 'Return {item} to {questGiver}', targets: ['questGiver'] },
  ],
  aliases: [
    { slot: 'questGiver', name: 'QuestGiver', kind: 'reference' },
    { slot: 'item', name: 'Item', kind: 'reference' },
    { slot: 'location', name: 'Location', kind: 'location' },
  ],
};
