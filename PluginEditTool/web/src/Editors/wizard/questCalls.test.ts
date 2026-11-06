import { expect, it } from 'vitest';
import { buildQuestCalls, isLinked, NEW_RECORD, type Answers, type BackendCall } from './questCalls';
import type { QuestTemplate } from './templateTypes';
import { WIZARD_HELP_TOPICS } from './wizardHelpTopics';
import { CHOICE_TWO_ENDINGS } from './templates/choiceTwoEndings';
import { ESCORT } from './templates/escort';
import { FETCH_AN_ITEM } from './templates/fetchAnItem';
import { QUEST_TEMPLATES } from './templates/registry';

interface ParsedStage { index: number; logEntries: { flags: number; logText: string }[]; }
interface ParsedObjective { displayText: string; targets: { alias: number }[]; }

const FORM_KEYS = {
  questGiver: '000900:Patch.esp',
  location: '000A00:Patch.esp',
  item: '000B00:Patch.esp',
  escortee: '000C00:Patch.esp',
  destination: '000D00:Patch.esp',
  target: '000E00:Patch.esp',
  person: '000F00:Patch.esp',
};

const sampleAnswers = (template: QuestTemplate): Answers => ({
  plugin: 'Patch.esp',
  ...Object.fromEntries(template.questions.map(question => [question.id, `Sample ${question.id}`])),
});

const jsonArg = <T>(call: BackendCall): T => JSON.parse(call.args[2] as string) as T;

const ESCORT_ANSWERS = {
  plugin: 'Patch.esp', name: 'Safe Passage', questGiver: 'Courier Bob', escortee: 'Old Hannah', destination: 'Sanctuary', reward: '50 caps',
};

const CHOICE_ANSWERS = {
  plugin: 'Patch.esp', name: 'The Raider Deal', questGiver: 'Courier Bob', optionA: 'Spare the raiders', optionB: 'Hand the raiders over', reward: '50 caps', rewardB: '20 caps',
};

const ANSWERS = {
  plugin: 'Patch.esp',
  name: 'The Lost Locket',
  questGiver: 'Courier Bob',
  location: 'Abandoned Mill',
  item: 'Rusty Locket',
  reward: '50 caps',
};

it('Detects: a template edit silently changing the quest it creates, because fetch-an-item must keep its exact calls and JSON', () => {
  expect(buildQuestCalls(FETCH_AN_ITEM, ANSWERS, FORM_KEYS)).toEqual([
    { method: 'CreateRecord', args: ['Patch.esp', 'QUST', { schema: true, editorId: 'The_Lost_Locket' }] },
    { method: 'SetField', args: ['Patch.esp', NEW_RECORD, 'QUST\\Flags', '1'] },
    {
      method: 'SetQuestStages',
      args: ['Patch.esp', NEW_RECORD, '[{"index":10,"flags":2,"logEntries":[{"flags":0,"logText":"Go to Abandoned Mill and find Rusty Locket."}]},{"index":20,"flags":0,"logEntries":[{"flags":0,"logText":"You found Rusty Locket. Take it back to Courier Bob."}]},{"index":30,"flags":0,"logEntries":[{"flags":1,"logText":"You gave Rusty Locket to Courier Bob. Reward: 50 caps."}]}]'],
    },
    {
      method: 'SetQuestAliases',
      args: ['Patch.esp', NEW_RECORD, '[{"kind":"reference","id":0,"name":"QuestGiver","uniqueActor":"000900:Patch.esp"},{"kind":"reference","id":1,"name":"Item","createReference":{"object":"000B00:Patch.esp","alias":2,"create":"At"}},{"kind":"location","id":2,"name":"Location","specificLocation":"000A00:Patch.esp"}]'],
    },
    {
      method: 'SetQuestObjectives',
      args: ['Patch.esp', NEW_RECORD, '[{"index":10,"flags":0,"displayText":"Find Rusty Locket at Abandoned Mill","targets":[{"alias":2,"flags":0}]},{"index":20,"flags":0,"displayText":"Return Rusty Locket to Courier Bob","targets":[{"alias":0,"flags":0}]}]'],
    },
  ]);
});

it('Detects: a quest name with no letters or digits sending an empty editor ID to CreateRecord', () => {
  expect(() => buildQuestCalls(FETCH_AN_ITEM, { ...ANSWERS, name: '!!! ' }, FORM_KEYS)).toThrow('at least one letter or number');
});

it('Detects: an unanswered {blank} making the quest say "undefined" in its journal, because the build must stop instead', () => {
  expect(() => buildQuestCalls(FETCH_AN_ITEM, { plugin: 'Patch.esp', name: 'The Lost Locket' }, FORM_KEYS)).toThrow('{location}');
});

it('Detects: a picked NPC, location or item whose form key is missing, so the alias would ship unlinked while the review says it is linked', () => {
  expect(() => buildQuestCalls(FETCH_AN_ITEM, ANSWERS, { questGiver: FORM_KEYS.questGiver, item: FORM_KEYS.item })).toThrow('No record was picked for {location}.');
  expect(() => buildQuestCalls(FETCH_AN_ITEM, ANSWERS, { questGiver: FORM_KEYS.questGiver, location: FORM_KEYS.location })).toThrow('No record was picked for {item}.');
});

it('Detects: an item with no location alias to place it at, so the item stays unlinked and the review keeps its not-linked note', () => {
  const noLocation = { ...FETCH_AN_ITEM, objectives: [], aliases: FETCH_AN_ITEM.aliases.filter(alias => alias.kind !== 'location') };
  expect(isLinked(noLocation, 'item')).toBe(false);
  expect(isLinked(FETCH_AN_ITEM, 'item')).toBe(true);
  const aliases = jsonArg<{ name: string; createReference?: unknown }[]>(buildQuestCalls(noLocation, ANSWERS, FORM_KEYS)[3]);
  expect(aliases.find(alias => alias.name === 'Item')?.createReference).toBeUndefined();
});

it('Detects: a template whose stages, objectives, aliases or question help ids do not line up, so the quest it builds is broken or the wizard shows a help topic that does not exist', () => {
  const helpIds = WIZARD_HELP_TOPICS.map(topic => topic.id);
  for (const template of QUEST_TEMPLATES) {
    const calls = buildQuestCalls(template, sampleAnswers(template), FORM_KEYS);
    expect(calls.map(call => call.method)).toEqual(['CreateRecord', 'SetField', 'SetQuestStages', 'SetQuestAliases', 'SetQuestObjectives']);
    const stages = jsonArg<ParsedStage[]>(calls[2]);
    const aliases = jsonArg<{ id: number }[]>(calls[3]);
    const objectives = jsonArg<ParsedObjective[]>(calls[4]);
    const indexes = stages.map(stage => stage.index);
    expect(indexes.filter((index, i) => i > 0 && index <= indexes[i - 1])).toEqual([]);
    expect(stages.some(stage => stage.logEntries.some(entry => (entry.flags & 1) === 1))).toBe(true);
    const texts = [
      ...stages.flatMap(stage => stage.logEntries.map(entry => entry.logText)),
      ...objectives.map(objective => objective.displayText),
    ];
    expect(texts.filter(text => text.includes('{'))).toEqual([]);
    const questionHelp = template.questions.map(question => question.help);
    expect(questionHelp.filter(id => !helpIds.includes(id))).toEqual([]);
    expect(new Set(questionHelp).size).toBe(questionHelp.length);
    expect(objectives.flatMap(objective => objective.targets.map(target => target.alias)).filter(alias => aliases[alias] === undefined)).toEqual([]);
  }
});

it('Detects: the escort failure stage marked as completing the quest (flag 1) instead of failing it (flag 2), so a dead escortee would count as a success', () => {
  expect(buildQuestCalls(ESCORT, ESCORT_ANSWERS, FORM_KEYS)[2]).toEqual({
    method: 'SetQuestStages',
    args: ['Patch.esp', NEW_RECORD, '[{"index":10,"flags":2,"logEntries":[{"flags":0,"logText":"Find Old Hannah and agree to take them to Sanctuary."}]},{"index":20,"flags":0,"logEntries":[{"flags":0,"logText":"Take Old Hannah to Sanctuary."}]},{"index":30,"flags":0,"logEntries":[{"flags":1,"logText":"Old Hannah reached Sanctuary. Report to Courier Bob for your reward: 50 caps."}]},{"index":90,"flags":0,"logEntries":[{"flags":2,"logText":"Old Hannah died. The quest failed."}]}]'],
  });
});

it('Detects: the two-ending template leaving one ending open (stage 40 not completing), so the player who makes the second choice would never finish the quest', () => {
  expect(buildQuestCalls(CHOICE_TWO_ENDINGS, CHOICE_ANSWERS, FORM_KEYS)[2]).toEqual({
    method: 'SetQuestStages',
    args: ['Patch.esp', NEW_RECORD, '[{"index":10,"flags":2,"logEntries":[{"flags":0,"logText":"Talk to Courier Bob to hear your choice."}]},{"index":20,"flags":0,"logEntries":[{"flags":0,"logText":"Decide between two options: Spare the raiders or Hand the raiders over."}]},{"index":30,"flags":0,"logEntries":[{"flags":1,"logText":"You chose: Spare the raiders. Reward: 50 caps."}]},{"index":40,"flags":0,"logEntries":[{"flags":1,"logText":"You chose: Hand the raiders over. Reward: 20 caps."}]}]'],
  });
});
