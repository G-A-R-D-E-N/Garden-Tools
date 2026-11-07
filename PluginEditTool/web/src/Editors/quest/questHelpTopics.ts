import type { HelpTopic } from '../help/helpContent';

export const QUEST_HELP_TOPICS: readonly HelpTopic[] = [
  {
    id: 'quest-stage-flags',
    title: 'Stage options',
    body: 'Run when the stage starts runs the stage\'s scripts as soon as the quest reaches it. Run when the stage stops does the same when the quest leaves it. Keep instance data from here on keeps the data that scripts store for this quest. If you are not sure, leave them off.',
    related: ['quest-stage'],
  },
  {
    id: 'quest-stage-log',
    title: 'Log entry',
    body: 'A log entry is the journal line the player reads when the quest reaches this stage. Write it as a short sentence, such as "Find the courier at the docks". Leave it empty if the stage should not show anything. Saving the stages rewrites every log entry. Log-entry conditions are kept when you save, but this editor cannot show or edit them.',
    related: ['quest-stage'],
  },
  {
    id: 'quest-stage-entry-flags',
    title: 'Log entry outcome',
    body: 'Completes the quest marks the quest as finished when this log entry is reached. Fails the quest marks it as failed. Leave both off for a normal step.',
    related: ['quest-stage-log'],
  },
  {
    id: 'quest-objective-index',
    title: 'Objective number',
    body: 'Every objective needs its own number. Scripts use this number to show, complete or track the objective, so two objectives in one quest can never share it.',
    related: ['objective'],
  },
  {
    id: 'quest-objective-text',
    title: 'Objective text',
    body: 'This is the goal the player sees in the quest list, such as "Find the courier". Keep it short and say what to do next.',
    related: ['objective'],
  },
  {
    id: 'quest-objective-flags',
    title: 'Objective options',
    body: 'Shown together with the previous objective makes this objective count as part of the one before it, so both show as one goal. Do not count in the player statistics keeps this goal out of the game\'s statistics.',
    related: ['objective'],
  },
  {
    id: 'quest-objective-targets',
    title: 'Objective targets',
    body: 'A target is an alias that this objective points at. The game uses it to show where the player should go or who to talk to. Add an alias on the Aliases pane first.',
    related: ['objective', 'alias'],
  },
  {
    id: 'quest-alias-kind',
    title: 'Alias kind',
    body: 'Reference is one specific actor or object, such as a named NPC. Location is a place, such as a town or a building. Collection is a group of references that the game fills in order.',
    related: ['alias'],
  },
  {
    id: 'quest-alias-name',
    title: 'Alias name',
    body: 'The name scripts and the Creation Kit use to refer to this alias. Use a clear name such as "Courier" so you can find it later.',
    related: ['alias'],
  },
  {
    id: 'quest-alias-flags',
    title: 'Alias flags',
    body: 'A number that stores this alias\'s options. The editor keeps the number as it is. The names of the individual options are not shown yet, so change this only if you know the value you need.',
    related: ['alias'],
  },
  {
    id: 'quest-alias-fill',
    title: 'Fill rule',
    body: 'A fill rule tells the game which actor, object or place to put in an alias when the quest starts. This editor cannot show or edit fill rules, keywords, items or conditions. Saving keeps them as they are.',
    related: ['alias'],
  },
  {
    id: 'quest-fragment',
    title: 'Script fragment',
    body: 'A fragment is a short script function that runs when the quest reaches a log entry. This editor writes the function to a .psc source file and a .pex compiled file, in the output folder you choose.',
    related: ['quest-stage'],
  },
  {
    id: 'quest-fragment-action',
    title: 'Fragment action',
    body: 'Show objective number shows the objective with that number. Complete objective number marks it as done. Set quest to stage moves the quest to that stage number. Each action runs in the order listed.',
    related: ['objective', 'quest-stage'],
  },
  {
    id: 'quest-fragment-output',
    title: 'Script folders',
    body: 'The output folder receives the .psc and .pex files for this script. The imports folder is where the script looks for game scripts it uses, usually the game\'s Data\\Scripts folder.',
    related: ['quest-fragment'],
  },
  {
    id: 'quest-check',
    title: 'Check this quest',
    body: 'Check this quest looks for problems that stop this quest from working. It checks that a stage completes or fails the quest, that each stage has journal text or an objective with its number, that each objective has a stage with its number and points only at aliases that exist, that no two stages share a number, that script jumps go to stages that exist, and that fragment scripts compile. It also lists the game record checker results for this quest, its dialogue and its scenes. Errors are listed before warnings, and info lines mean something could not be read so it was not checked. The list is out of date after you save a stage, objective, alias or fragment, so check again. If the record checker hits its row limit, a warning says the list may be missing problems.',
    related: ['quest-stage', 'quest-fragment'],
  },
];
