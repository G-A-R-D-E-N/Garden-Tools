import type { HelpTopic } from '../help/helpContent';

export const WIZARD_HELP_TOPICS: readonly HelpTopic[] = [
  {
    id: 'wizard-plugin',
    title: 'Plugin',
    body: 'The plugin is the file the new quest is saved in. Pick a plugin that belongs to your mod, not a base game file. Nothing is written to disk until you save the plugin.',
    related: [],
  },
  {
    id: 'wizard-name',
    title: 'Quest name',
    body: 'The name becomes the quest\'s editor ID. Spaces and punctuation turn into underscores, so "The Lost Locket" becomes The_Lost_Locket.',
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
