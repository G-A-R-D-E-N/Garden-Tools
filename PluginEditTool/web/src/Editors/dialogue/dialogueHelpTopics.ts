import type { HelpTopic } from '../help/helpContent';

export const DIALOGUE_HELP_TOPICS: readonly HelpTopic[] = [
  {
    id: 'dialogue-quest',
    title: 'Quest',
    body: 'Dialogue belongs to one quest. The branches, topics and replies you see are the ones this quest owns. New ones are added to it.',
    related: ['dialogue-topic', 'dialogue-branch'],
  },
  {
    id: 'dialogue-branch',
    title: 'Branch',
    body: 'A branch is a group of topics for one quest. Each branch has a start topic, the first thing the player can say in that group.',
    related: ['dialogue-start-topic'],
  },
  {
    id: 'dialogue-start-topic',
    title: 'Start topic',
    body: 'The start topic is the topic a branch opens with. Pick a topic on the left, then press Make the selected topic the start.',
    related: ['dialogue-branch'],
  },
  {
    id: 'dialogue-editor-id',
    title: 'Editor ID',
    body: 'The internal name of a record. Players never see it. Use letters, numbers and underscores, such as GreetPlayer.',
    related: [],
  },
  {
    id: 'dialogue-priority',
    title: 'Priority',
    body: 'Priority sets how the game weighs topics against each other. Leave it at 50 unless you have a reason to change it.',
    related: ['dialogue-topic'],
  },
  {
    id: 'dialogue-reply',
    title: 'Reply',
    body: 'A reply is one answer block under a topic. It holds the NPC lines, and its conditions decide when the game can use it.',
    related: ['dialogue-topic', 'dialogue-conditions'],
  },
  {
    id: 'dialogue-line-text',
    title: 'Line text',
    body: 'The words the NPC says. They also show as subtitles. Lines in a plugin that keeps text in the string table cannot be edited here yet.',
    related: ['dialogue-line-number'],
  },
  {
    id: 'dialogue-line-number',
    title: 'Line number',
    body: 'The number of this line inside its reply, from 1 to 255. Keep the numbers in the order you want the lines used.',
    related: ['dialogue-line-text'],
  },
  {
    id: 'dialogue-emotion',
    title: 'Emotion',
    body: 'The facial expression the NPC uses while saying this line. Pick an emotion keyword from the list, or clear it for none.',
    related: [],
  },
  {
    id: 'dialogue-conditions',
    title: 'Conditions',
    body: 'Rules that must pass before the game can use this reply. Each rule checks one thing, such as an item count or a quest stage.',
    related: ['dialogue-reply'],
  },
  {
    id: 'dialogue-fragment',
    title: 'Script fragment',
    body: 'A small Papyrus script attached to a reply that runs when the reply plays. This editor shows the attached scripts but does not change them yet.',
    related: ['dialogue-reply'],
  },
  {
    id: 'dialogue-flag-random',
    title: 'Random flag',
    body: 'The game picks one of the matching replies at random instead of always using the first one.',
    related: ['dialogue-reply'],
  },
  {
    id: 'dialogue-flag-player-only',
    title: 'Player-only flag',
    body: 'Only the player can start the replies under this one. The NPC will not start them.',
    related: ['dialogue-reply'],
  },
  {
    id: 'dialogue-flag-random-end',
    title: 'Random end flag',
    body: 'The conversation can end at random at this point.',
    related: ['dialogue-reply'],
  },
  {
    id: 'dialogue-flag-reset',
    title: 'Reset time flag',
    body: 'The replies under this one do not reuse this reply\'s reset time. Each of them uses its own reset time.',
    related: ['dialogue-reply'],
  },
  {
    id: 'dialogue-flag-force-random',
    title: 'Every child random flag',
    body: 'Every reply under this one is picked at random.',
    related: ['dialogue-reply'],
  },
  {
    id: 'dialogue-flag-repeat',
    title: 'Repeat flag',
    body: 'The replies under this one can repeat before every one of them has been used once.',
    related: ['dialogue-reply'],
  },
];
