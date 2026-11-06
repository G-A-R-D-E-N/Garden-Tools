export interface HelpTopic {
  id: string;
  title: string;
  body: string;
  related: readonly string[];
}

export const HELP_TOPICS: readonly HelpTopic[] = [
  {
    id: 'quest-stage',
    title: 'Quest stage',
    body: 'A quest stage is a checkpoint in a quest. Each stage has a number and can show a journal entry for the player. Scripts move the quest from one stage to the next.',
    related: ['objective', 'alias'],
  },
  {
    id: 'objective',
    title: 'Objective',
    body: 'An objective is a goal the player sees in the quest list, such as "Find the courier". It has a number and text, and it can point at aliases so the game can show where to go.',
    related: ['quest-stage', 'alias'],
  },
  {
    id: 'alias',
    title: 'Alias',
    body: 'An alias is a named slot that holds one specific thing for the quest to use, such as an NPC, a door or a location. Objectives and scenes refer to an alias by its alias number.',
    related: ['objective'],
  },
  {
    id: 'dialogue-topic',
    title: 'Topic',
    body: 'A topic is a line the player can say, or a question they can ask an NPC. Each topic holds one or more responses that the NPC can say, and conditions decide which response is used.',
    related: ['quest-stage'],
  },
  {
    id: 'scene-phase',
    title: 'Scene phase',
    body: 'A scene phase is one step of a scripted scene, shown as a column on the timeline. Actions such as dialogue or movement run during a phase, and a phase can run a script when it starts or ends, for example to move the quest to a new stage.',
    related: ['quest-stage'],
  },
];

export const GETTING_STARTED: readonly string[] = [
  'Open the Editors icon in the left bar, then pick Quest, Dialogue or Scene.',
  'Hover over a ? to read what a field means, or press Tab to reach it and read it without a mouse.',
  'Make your changes. While an editor screen has focus, Ctrl+Z undoes the last change and Ctrl+Y redoes it.',
  'Nothing is written to disk until you save the plugin. Right-click a record and choose Save "<plugin name>".',
];

export const SCREEN_TOPICS = {
  quest: ['quest-stage', 'objective', 'alias'],
  dialogue: ['dialogue-topic', 'quest-stage'],
  scene: ['scene-phase', 'alias'],
} as const;

export const getHelpTopic = (id: string): HelpTopic | undefined =>
  HELP_TOPICS.find(topic => topic.id === id);
