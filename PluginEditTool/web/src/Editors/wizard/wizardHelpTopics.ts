import type { HelpTopic } from '../help/helpContent';

export const WIZARD_HELP_TOPICS: readonly HelpTopic[] = [
  {
    id: 'wizard-plugin',
    title: 'Plugin',
    body: 'The plugin is the file the new quest is saved in. Pick a plugin that is open for editing. Nothing is written to disk until you save the plugin.',
    related: [],
  },
  {
    id: 'wizard-name',
    title: 'Internal name',
    body: 'The internal name is how the Creation Kit and scripts find this quest. It cannot have spaces, so spaces and punctuation turn into underscores: "The Lost Locket" becomes The_Lost_Locket. This is not the title the player sees.',
    related: [],
  },
  {
    id: 'wizard-quest-giver',
    title: 'Quest giver',
    body: 'The character who gives the player the quest and who the item goes back to. Pick an NPC from the list.',
    related: ['alias'],
  },
  {
    id: 'wizard-location',
    title: 'Location',
    body: 'The place where the player finds the item. Pick a location from the list.',
    related: ['alias'],
  },
  {
    id: 'wizard-item',
    title: 'Item',
    body: 'The item the player must bring back. Pick an item from the list.',
    related: ['alias'],
  },
  {
    id: 'wizard-reward',
    title: 'Reward',
    body: 'The reward the final journal line names, such as "50 caps". Writing it here does not give the player anything by itself.',
    related: ['quest-stage'],
  },
];
