import { expect, it } from 'vitest';
import { buildQuestCalls, NEW_RECORD } from './questCalls';
import { FETCH_AN_ITEM } from './templates/fetchAnItem';

const ANSWERS = {
  plugin: 'Patch.esp',
  name: 'The Lost Locket',
  questGiver: 'Courier Bob',
  location: 'Abandoned Mill',
  item: 'Rusty Locket',
  reward: '50 caps',
};

it('Detects: a template edit silently changing the quest it creates, because fetch-an-item must keep its exact calls and JSON', () => {
  expect(buildQuestCalls(FETCH_AN_ITEM, ANSWERS)).toEqual([
    { method: 'CreateRecord', args: ['Patch.esp', 'QUST', { schema: true, editorId: 'The_Lost_Locket' }] },
    {
      method: 'SetQuestStages',
      args: ['Patch.esp', NEW_RECORD, '[{"index":10,"flags":0,"logEntries":[{"flags":0,"logText":"Go to Abandoned Mill and find Rusty Locket."}]},{"index":20,"flags":0,"logEntries":[{"flags":0,"logText":"You found Rusty Locket. Take it back to Courier Bob."}]},{"index":30,"flags":0,"logEntries":[{"flags":1,"logText":"You gave Rusty Locket to Courier Bob. Reward: 50 caps."}]}]'],
    },
    {
      method: 'SetQuestAliases',
      args: ['Patch.esp', NEW_RECORD, '[{"kind":"reference","id":0,"name":"QuestGiver"},{"kind":"reference","id":1,"name":"Item"},{"kind":"location","id":2,"name":"Location"}]'],
    },
    {
      method: 'SetQuestObjectives',
      args: ['Patch.esp', NEW_RECORD, '[{"index":10,"flags":0,"displayText":"Find Rusty Locket at Abandoned Mill","targets":[{"alias":2,"flags":0}]},{"index":20,"flags":0,"displayText":"Return Rusty Locket to Courier Bob","targets":[{"alias":0,"flags":0}]}]'],
    },
  ]);
});

it('Detects: a quest name with no letters or digits sending an empty editor ID to CreateRecord', () => {
  expect(() => buildQuestCalls(FETCH_AN_ITEM, { ...ANSWERS, name: '!!! ' })).toThrow('at least one letter or number');
});

it('Detects: an unanswered {blank} making the quest say "undefined" in its journal, because the build must stop instead', () => {
  expect(() => buildQuestCalls(FETCH_AN_ITEM, { plugin: 'Patch.esp', name: 'The Lost Locket' })).toThrow('{location}');
});
