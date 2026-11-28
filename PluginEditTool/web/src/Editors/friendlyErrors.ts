interface FriendlyError {
  match: string;
  message: string;
  fix: string;
}

const FRIENDLY_ERRORS: readonly FriendlyError[] = [
  {
    match: 'references the actor alias this edit removes',
    message: 'A scene action still uses this actor.',
    fix: 'Clear the actor on those actions first, then remove the actor.',
  },
  {
    match: 'references the phase this edit removes',
    message: 'A scene action still uses this phase.',
    fix: 'Move or remove those actions first, then remove the phase.',
  },
  {
    match: 'binds the phase this edit removes',
    message: 'A script fragment is still attached to this phase.',
    fix: 'Remove the phase script fragment first, then remove the phase.',
  },
  {
    match: 'duplicate objective index',
    message: 'Another objective in this quest already uses this number.',
    fix: 'Pick a number that no other objective uses.',
  },
  {
    match: 'already has an objective with index',
    message: 'Another objective in this quest already uses this number.',
    fix: 'Pick a number that no other objective uses.',
  },
  {
    match: 'Dialogue structure does not allow this edit',
    message: 'The dialogue links do not fit together. A common cause is a topic and a branch from different quests.',
    fix: 'Check that the topic and the branch belong to the same quest, then try again.',
  },
  {
    match: 'Windows-1252',
    message: 'This text has characters the game cannot store.',
    fix: 'Use standard Western letters and punctuation. Remove emoji and text from other alphabets.',
  },
];

export interface FriendlyMessage {
  text: string;
  details?: string;
}

export const readOnlySentence = (plugin: string) =>
  `${plugin} is a game file and cannot be changed. Copy the record into your own plugin first (the Copy as override button above, or the record menu), then edit the copy.`;

export const readOnlyNoCopySentence = (plugin: string) =>
  `${plugin} is a game file and cannot be changed. Its dialogue stays readable here. Copying the quest does not copy its dialogue, so this screen cannot copy it.`;

export function friendlyError(message: string): FriendlyMessage {
  if (message.includes('PluginReadOnly')) {
    const plugin = /plugin:\s*(.+?)\s*$/.exec(message)?.[1] ?? 'This plugin';
    return { text: readOnlySentence(plugin), details: message };
  }
  const known = FRIENDLY_ERRORS.find(entry => message.includes(entry.match));
  return known ? { text: `${known.message} ${known.fix}`, details: message } : { text: message };
}
