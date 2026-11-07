import { expect, it, vi } from 'vitest';
import type { Backend } from '../../backend';
import { buildCreateCalls } from './createCalls';
import { buildQuestCalls, fill, isLinked, NEW_RECORD, questNameError, runQuestCalls, type Answers, type BackendCall, type QuestRun } from './questCalls';
import { buildSceneCalls, describeSceneAndDialogue, NEW_BRANCH, NEW_SCENE, NEW_TOPIC } from './sceneCalls';
import type { QuestTemplate } from './templateTypes';
import { WIZARD_HELP_TOPICS } from './wizardHelpTopics';
import { CHOICE_TWO_ENDINGS } from './templates/choiceTwoEndings';
import { ESCORT } from './templates/escort';
import { FETCH_AN_ITEM } from './templates/fetchAnItem';
import { KILL_A_TARGET } from './templates/killATarget';
import { QUEST_TEMPLATES } from './templates/registry';
import { TALK_TO_SOMEONE } from './templates/talkToSomeone';

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
    {
      method: 'SetQuestStages',
      args: ['Patch.esp', NEW_RECORD, '[{"index":10,"flags":2,"logEntries":[{"flags":0,"logText":"Go to Abandoned Mill and find Rusty Locket.","conditions":[]}]},{"index":20,"flags":0,"logEntries":[{"flags":0,"logText":"You found Rusty Locket. Take it back to Courier Bob.","conditions":[]}]},{"index":30,"flags":0,"logEntries":[{"flags":1,"logText":"You gave Rusty Locket to Courier Bob. Reward: 50 caps.","conditions":[]}]}]'],
    },
    {
      method: 'SetQuestAliases',
      args: ['Patch.esp', NEW_RECORD, '[{"kind":"reference","id":0,"flags":null,"name":"QuestGiver","uniqueActor":"000900:Patch.esp"},{"kind":"reference","id":1,"flags":null,"name":"Item","createReference":{"object":"000B00:Patch.esp","alias":2,"create":"At"}},{"kind":"location","id":2,"flags":null,"name":"Location","specificLocation":"000A00:Patch.esp"}]'],
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

it('Detects: a quest name starting with a digit reaching CreateRecord, because the editor ID would be rejected by the game', () => {
  expect(questNameError('!!! ')).toBe('The quest name needs at least one letter or number.');
  expect(questNameError('1st Locket')).toBe('The quest name cannot start with a number.');
  expect(questNameError('The Lost Locket')).toBeNull();
  expect(() => buildQuestCalls(FETCH_AN_ITEM, { ...ANSWERS, name: '2 Locket' }, FORM_KEYS)).toThrow('cannot start with a number');
});

it('Detects: stray spaces in an answer reaching a journal line or objective, because the text would show a gap before the punctuation', () => {
  expect(fill('Find {item} at {location}', { item: '  Rusty Locket ', location: 'Abandoned Mill' })).toBe('Find Rusty Locket at Abandoned Mill');
  expect(() => fill('Go to {location}.', { location: '   ' })).toThrow('No answer for {location}.');
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
  const aliases = jsonArg<{ name: string; createReference?: unknown }[]>(buildQuestCalls(noLocation, ANSWERS, FORM_KEYS)[2]);
  expect(aliases.find(alias => alias.name === 'Item')?.createReference).toBeUndefined();
});

it('Detects: a template whose stages, objectives, aliases or question help ids do not line up, so the quest it builds is broken or the wizard shows a help topic that does not exist', () => {
  const helpIds = WIZARD_HELP_TOPICS.map(topic => topic.id);
  for (const template of QUEST_TEMPLATES) {
    const calls = buildQuestCalls(template, sampleAnswers(template), FORM_KEYS);
    expect(calls.map(call => call.method)).toEqual(['CreateRecord', 'SetQuestStages', 'SetQuestAliases', 'SetQuestObjectives']);
    const stages = jsonArg<ParsedStage[]>(calls[1]);
    const aliases = jsonArg<{ id: number }[]>(calls[2]);
    const objectives = jsonArg<ParsedObjective[]>(calls[3]);
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

it('Detects: the escort failure stage marked as completing the quest (flag 1) instead of failing it (flag 2), or the report stage completing before the player reports, so a dead escortee would count as a success', () => {
  expect(buildQuestCalls(ESCORT, ESCORT_ANSWERS, FORM_KEYS)[1]).toEqual({
    method: 'SetQuestStages',
    args: ['Patch.esp', NEW_RECORD, '[{"index":10,"flags":2,"logEntries":[{"flags":0,"logText":"Find Old Hannah and agree to take them to Sanctuary.","conditions":[]}]},{"index":20,"flags":0,"logEntries":[{"flags":0,"logText":"Take Old Hannah to Sanctuary.","conditions":[]}]},{"index":30,"flags":0,"logEntries":[{"flags":0,"logText":"Old Hannah reached Sanctuary. Report to Courier Bob for your reward: 50 caps.","conditions":[]}]},{"index":40,"flags":0,"logEntries":[{"flags":1,"logText":"You reported to Courier Bob. Reward: 50 caps.","conditions":[]}]},{"index":90,"flags":0,"logEntries":[{"flags":2,"logText":"Old Hannah died. The quest failed.","conditions":[]}]}]'],
  });
});

it('Detects: the two-ending template leaving one ending open (stage 40 not completing), so the player who makes the second choice would never finish the quest', () => {
  expect(buildQuestCalls(CHOICE_TWO_ENDINGS, CHOICE_ANSWERS, FORM_KEYS)[1]).toEqual({
    method: 'SetQuestStages',
    args: ['Patch.esp', NEW_RECORD, '[{"index":10,"flags":2,"logEntries":[{"flags":0,"logText":"Talk to Courier Bob to hear your choice.","conditions":[]}]},{"index":20,"flags":0,"logEntries":[{"flags":0,"logText":"Decide between two options: Spare the raiders or Hand the raiders over.","conditions":[]}]},{"index":30,"flags":0,"logEntries":[{"flags":1,"logText":"You chose: Spare the raiders. Reward: 50 caps.","conditions":[]}]},{"index":40,"flags":0,"logEntries":[{"flags":1,"logText":"You chose: Hand the raiders over. Reward: 20 caps.","conditions":[]}]}]'],
  });
});

const TALK_ANSWERS = {
  plugin: 'Patch.esp', name: 'The Old Friend', person: 'Courier Bob', location: 'Abandoned Mill', reward: '50 caps',
};

const KILL_ANSWERS = {
  plugin: 'Patch.esp', name: 'Mill Bandit', questGiver: 'Courier Bob', target: 'Raider Boss', location: 'Abandoned Mill', reward: '50 caps',
};

it('Detects: the kill-a-target quest calls changing from what the wizard made before, because that template has no scene and must keep its exact quest calls', () => {
  expect(buildQuestCalls(KILL_A_TARGET, KILL_ANSWERS, FORM_KEYS)).toEqual([
    { method: 'CreateRecord', args: ['Patch.esp', 'QUST', { schema: true, editorId: 'Mill_Bandit' }] },
    {
      method: 'SetQuestStages',
      args: ['Patch.esp', NEW_RECORD, '[{"index":10,"flags":2,"logEntries":[{"flags":0,"logText":"Go to Abandoned Mill and find Raider Boss.","conditions":[]}]},{"index":20,"flags":0,"logEntries":[{"flags":0,"logText":"You defeated Raider Boss. Report back to Courier Bob.","conditions":[]}]},{"index":30,"flags":0,"logEntries":[{"flags":1,"logText":"You reported to Courier Bob. Reward: 50 caps.","conditions":[]}]}]'],
    },
    {
      method: 'SetQuestAliases',
      args: ['Patch.esp', NEW_RECORD, '[{"kind":"reference","id":0,"flags":null,"name":"QuestGiver","uniqueActor":"000900:Patch.esp"},{"kind":"reference","id":1,"flags":null,"name":"Target","uniqueActor":"000E00:Patch.esp"},{"kind":"location","id":2,"flags":null,"name":"Location","specificLocation":"000A00:Patch.esp"}]'],
    },
    {
      method: 'SetQuestObjectives',
      args: ['Patch.esp', NEW_RECORD, '[{"index":10,"flags":0,"displayText":"Find Raider Boss at Abandoned Mill","targets":[{"alias":2,"flags":0},{"alias":1,"flags":0}]},{"index":20,"flags":0,"displayText":"Report back to Courier Bob","targets":[{"alias":0,"flags":0}]}]'],
    },
  ]);
});

it('Detects: a fetch, kill or choice template gaining a scene or starter dialogue, which would make the quest contain content its template never asked for', () => {
  for (const template of [FETCH_AN_ITEM, KILL_A_TARGET, CHOICE_TWO_ENDINGS]) {
    expect(buildSceneCalls(template, sampleAnswers(template))).toEqual([]);
  }
});

it('Detects: the talk-to-someone scene and dialogue calls changing fields, order or the topic link, so the scene is written with fields the game does not read or a link to a topic not yet made', () => {
  expect(buildSceneCalls(TALK_TO_SOMEONE, TALK_ANSWERS)).toEqual([
    { method: 'CreateDialogueBranch', args: ['Patch.esp', NEW_RECORD, { editorId: 'The_Old_Friend_Branch' }], produces: NEW_BRANCH },
    {
      method: 'CreateDialogueTopic',
      args: ['Patch.esp', NEW_RECORD, { editorId: 'The_Old_Friend_Topic', name: 'Are you Courier Bob? I was sent to speak with you.', priority: 50, branch: NEW_BRANCH }],
      produces: NEW_TOPIC,
    },
    {
      method: 'CreateInfo',
      args: ['Patch.esp', NEW_TOPIC, { responses: [{ text: 'I have been waiting for you at Abandoned Mill.' }, { text: 'Thanks for coming. Your reward is 50 caps.' }] }],
    },
    { method: 'CreateScene', args: ['Patch.esp', NEW_RECORD, { editorId: 'The_Old_Friend_Scene' }], produces: NEW_SCENE },
    { method: 'AddScenePhase', args: ['Patch.esp', NEW_SCENE, 0, { name: 'Meet' }] },
    { method: 'AddScenePhase', args: ['Patch.esp', NEW_SCENE, 1, { name: 'Talk' }] },
    { method: 'AddSceneActor', args: ['Patch.esp', NEW_SCENE, 0] },
    { method: 'SetSceneActor', args: ['Patch.esp', NEW_SCENE, 0, { alias: 0 }] },
    { method: 'AddSceneAction', args: ['Patch.esp', NEW_SCENE, 0, 0] },
    { method: 'SetSceneAction', args: ['Patch.esp', NEW_SCENE, 0, { actorAlias: 0, startPhase: 1, endPhase: 1, topic: NEW_TOPIC }] },
  ]);
});

it('Detects: the escort scene and dialogue calls pointing the escortee action at the wrong alias or phase, so the scene would make the wrong character speak', () => {
  expect(buildSceneCalls(ESCORT, ESCORT_ANSWERS)).toEqual([
    { method: 'CreateDialogueBranch', args: ['Patch.esp', NEW_RECORD, { editorId: 'Safe_Passage_Branch' }], produces: NEW_BRANCH },
    {
      method: 'CreateDialogueTopic',
      args: ['Patch.esp', NEW_RECORD, { editorId: 'Safe_Passage_Topic', name: 'Do you need an escort to Sanctuary?', priority: 50, branch: NEW_BRANCH }],
      produces: NEW_TOPIC,
    },
    {
      method: 'CreateInfo',
      args: ['Patch.esp', NEW_TOPIC, { responses: [{ text: 'Yes. I will walk with you to Sanctuary.' }, { text: 'Stay close, and do not let me get hurt.' }] }],
    },
    { method: 'CreateScene', args: ['Patch.esp', NEW_RECORD, { editorId: 'Safe_Passage_Scene' }], produces: NEW_SCENE },
    { method: 'AddScenePhase', args: ['Patch.esp', NEW_SCENE, 0, { name: 'Meet' }] },
    { method: 'AddScenePhase', args: ['Patch.esp', NEW_SCENE, 1, { name: 'Walk' }] },
    { method: 'AddScenePhase', args: ['Patch.esp', NEW_SCENE, 2, { name: 'Arrive' }] },
    { method: 'AddSceneActor', args: ['Patch.esp', NEW_SCENE, 0] },
    { method: 'SetSceneActor', args: ['Patch.esp', NEW_SCENE, 0, { alias: 1 }] },
    { method: 'AddSceneAction', args: ['Patch.esp', NEW_SCENE, 0, 0] },
    { method: 'SetSceneAction', args: ['Patch.esp', NEW_SCENE, 0, { actorAlias: 1, startPhase: 0, endPhase: 0, topic: NEW_TOPIC }] },
  ]);
});

it('Detects: a scene skeleton whose actor slot is not a quest alias, whose action phase is past the last part, or whose line has an unfilled {blank}, so the scene is written broken', () => {
  for (const template of QUEST_TEMPLATES) {
    const scene = template.scene;
    if (!scene) continue;
    const slots = template.aliases.map(alias => alias.slot);
    expect(scene.actors.filter(slot => !slots.includes(slot))).toEqual([]);
    expect(scene.actions.filter(action => !scene.actors.includes(action.actor))).toEqual([]);
    expect(scene.actions.filter(action => action.phase < 0 || action.phase >= scene.phases.length)).toEqual([]);
    expect(scene.actions.some(action => action.kind === 'dialogue') && !template.dialogue).toBe(false);
    const calls = buildSceneCalls(template, sampleAnswers(template));
    expect(calls.map(call => call.method)).toContain('CreateScene');
    expect(JSON.stringify(calls)).not.toMatch(/\{\w+\}/);
  }
});

it('Detects: the runner sending a scene or topic placeholder to the game as text instead of the form key the game returned, so the scene and its dialogue link would point at nothing', async () => {
  const keys: Record<string, string> = {
    CreateRecord: '000D00:Patch.esp', CreateDialogueBranch: '000E00:Patch.esp', CreateDialogueTopic: '000F00:Patch.esp',
    CreateInfo: '001000:Patch.esp', CreateScene: '001100:Patch.esp',
  };
  const methods = [
    'CreateRecord', 'SetField', 'SetQuestStages', 'SetQuestObjectives', 'SetQuestAliases', 'CreateScene', 'AddScenePhase', 'AddSceneActor',
    'SetSceneActor', 'AddSceneAction', 'SetSceneAction', 'CreateDialogueBranch', 'CreateDialogueTopic', 'CreateInfo',
  ];
  const backend = Object.fromEntries(methods.map(name => [name, vi.fn(async () => (
    name in keys ? JSON.stringify({ formKey: keys[name] }) : 'ok'
  ))]));
  const run: QuestRun = { formKey: null, made: new Map() };
  await runQuestCalls(backend as unknown as Backend, [
    ...buildQuestCalls(TALK_TO_SOMEONE, TALK_ANSWERS, FORM_KEYS), ...buildSceneCalls(TALK_TO_SOMEONE, TALK_ANSWERS),
  ], run);
  expect(backend.CreateDialogueTopic).toHaveBeenCalledWith('Patch.esp', '000D00:Patch.esp', expect.objectContaining({ branch: '000E00:Patch.esp' }));
  expect(backend.CreateInfo).toHaveBeenCalledWith('Patch.esp', '000F00:Patch.esp', expect.anything());
  expect(backend.SetSceneAction).toHaveBeenCalledWith('Patch.esp', '001100:Patch.esp', 0, { actorAlias: 0, startPhase: 1, endPhase: 1, topic: '000F00:Patch.esp' });
});

it('Detects: the review sentences for Talk to someone dropping the scene parts or the reply lines, so the user is not shown what the wizard will create', () => {
  expect(describeSceneAndDialogue(TALK_TO_SOMEONE, TALK_ANSWERS)).toEqual([
    'A scene with 2 parts: Meet, then Talk.',
    'Courier Bob speaks in the Talk part.',
    'The player says: "Are you Courier Bob? I was sent to speak with you."',
    'The reply: "I have been waiting for you at Abandoned Mill." "Thanks for coming. Your reward is 50 caps."',
  ]);
});

it('Detects: the start-game flag written before the objective, alias or scene calls, so a failure later leaves a start-enabled half-built quest', () => {
  for (const template of QUEST_TEMPLATES) {
    const calls = buildCreateCalls(template, sampleAnswers(template), FORM_KEYS);
    expect(calls[calls.length - 1]).toEqual({ method: 'SetField', args: ['Patch.esp', NEW_RECORD, 'QUST\\Flags', '1'] });
    expect(calls.filter(call => call.method === 'SetField')).toHaveLength(1);
  }
});

const CREATE_KEYS: Record<string, string> = {
  CreateRecord: '000D00:Patch.esp', CreateDialogueBranch: '000E00:Patch.esp', CreateDialogueTopic: '000F00:Patch.esp',
  CreateInfo: '001000:Patch.esp', CreateScene: '001100:Patch.esp',
};
const CREATE_METHODS = [
  'CreateRecord', 'SetField', 'SetQuestStages', 'SetQuestObjectives', 'SetQuestAliases', 'CreateScene', 'AddScenePhase', 'AddSceneActor',
  'SetSceneActor', 'AddSceneAction', 'SetSceneAction', 'CreateDialogueBranch', 'CreateDialogueTopic', 'CreateInfo',
];

function createBackend(overrides: Record<string, unknown> = {}) {
  return {
    ...Object.fromEntries(CREATE_METHODS.map(name => [name, vi.fn(async () => (
      name in CREATE_KEYS ? JSON.stringify({ formKey: CREATE_KEYS[name] }) : 'ok'
    ))])),
    ...overrides,
  } as Record<string, ReturnType<typeof vi.fn>>;
}

it('Detects: the start-game flag is written only after the scene and dialogue calls all succeed, so a late failure leaves the quest not started by the game', async () => {
  const backend = createBackend();
  const run: QuestRun = { formKey: null, made: new Map() };
  await runQuestCalls(backend as unknown as Backend, buildCreateCalls(TALK_TO_SOMEONE, TALK_ANSWERS, FORM_KEYS), run);
  expect(backend.SetField.mock.invocationCallOrder[0]).toBeGreaterThan(backend.SetSceneAction.mock.invocationCallOrder[0]);
  expect(backend.SetField).toHaveBeenCalledWith('Patch.esp', CREATE_KEYS.CreateRecord, 'QUST\\Flags', '1');
});

it('Detects: a dialogue topic failure after the quest was staged never reaches the start-game flag, and the run keeps the made quest key', async () => {
  const backend = createBackend({
    CreateDialogueTopic: vi.fn(async () => { throw new Error('Localized plugin refuses dialogue topics.'); }),
  });
  const run: QuestRun = { formKey: null, made: new Map() };
  await expect(runQuestCalls(backend as unknown as Backend, buildCreateCalls(TALK_TO_SOMEONE, TALK_ANSWERS, FORM_KEYS), run))
    .rejects.toThrow('Localized plugin refuses dialogue topics.');
  expect(backend.SetQuestObjectives).toHaveBeenCalled();
  expect(backend.SetField).not.toHaveBeenCalled();
  expect(run.formKey).toBe(CREATE_KEYS.CreateRecord);
});
