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
  {
    id: 'wizard-target',
    title: 'Target',
    body: 'The named enemy the player must find and defeat. Pick an NPC from the list. The quest does not put the enemy in the world, so place them at the location in the Creation Kit.',
    related: ['alias'],
  },
  {
    id: 'wizard-target-location',
    title: 'Target location',
    body: 'The place where the player finds the target. Pick a location from the list.',
    related: ['alias'],
  },
  {
    id: 'wizard-person',
    title: 'Person',
    body: 'The named character the player must find and talk to. Pick an NPC from the list.',
    related: ['alias'],
  },
  {
    id: 'wizard-person-location',
    title: 'Where to find them',
    body: 'The place where the player finds this person. Pick a location from the list.',
    related: ['alias'],
  },
  {
    id: 'wizard-escortee',
    title: 'Escortee',
    body: 'The character who walks with the player to the destination. If this character dies, the quest fails. Pick an NPC from the list.',
    related: ['alias'],
  },
  {
    id: 'wizard-destination',
    title: 'Destination',
    body: 'The place the escortee must be taken to. Pick a location from the list.',
    related: ['alias'],
  },
  {
    id: 'wizard-option-a',
    title: 'First choice',
    body: 'The first line the player can pick when talking to the quest giver, worded as the player says it, such as "Spare the raiders". It also appears in the journal, which reads "You chose: Spare the raiders."',
    related: ['quest-stage', 'dialogue-topic'],
  },
  {
    id: 'wizard-option-b',
    title: 'Second choice',
    body: 'The second line the player can pick when talking to the quest giver, worded the same way as the first choice, such as "Hand the raiders over". It also appears in the journal.',
    related: ['quest-stage', 'dialogue-topic'],
  },
  {
    id: 'wizard-choices',
    title: 'Dialogue choices',
    body: 'The two lines the player can pick when talking to the quest giver. The quest giver offers them only while the quest is at stage 10 or 20. Picking one sets the quest to stage 30 or 40, which finishes the quest.',
    related: ['quest-stage', 'dialogue-topic'],
  },
  {
    id: 'wizard-reward-b',
    title: 'Reward for the second choice',
    body: 'The reward the final journal line names when the player makes the second choice, such as "20 caps". Writing it here does not give the player anything by itself.',
    related: ['quest-stage'],
  },
  {
    id: 'wizard-scene',
    title: 'Scene',
    body: 'A scene is a short piece of staged action: parts that play in order, and a character who takes part. The wizard makes a starter scene with the parts listed in the review. Nothing starts the scene when the quest starts, so set that in the scene editor.',
    related: ['scene-timeline'],
  },
  {
    id: 'wizard-dialogue',
    title: 'Starter dialogue',
    body: 'The line the player says and the reply the character gives. The lines use your answers, such as the character\'s name. The wizard does not attach this dialogue to a character, so set that in the dialogue editor.',
    related: ['dialogue-branch'],
  },
];
