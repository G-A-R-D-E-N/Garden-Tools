import { expect, it } from 'vitest';
import { parseStages, stagesJson, type QuestStage } from './questModel';

const condition = (value: number) => ({
  function: 'GetIsID',
  operator: '==',
  param1: '000800:Patch.esp',
  value,
  _native: { ctda: Array.from({ length: 32 }, (_, byte) => byte), cis1: null, cis2: null },
});

it('Detects: stagesJson sends the loaded conditions unchanged, including each _native object', () => {
  const conditions = [
    condition(1),
    { ...condition(0), _native: { ctda: Array.from({ length: 32 }, () => 0), cis1: 'KeywordOne', cis2: null } },
  ];
  const stages = parseStages({ stages: [{ index: 10, flags: 0, logEntries: [
    { logText: 'Start', note: 'n1', flags: 0, conditions },
  ] }] });
  const sent = JSON.parse(stagesJson(stages));
  expect(sent[0].logEntries[0].conditions).toEqual(conditions);
});

it('Detects: a log entry added in the editor sends conditions as an empty array', () => {
  const stages: QuestStage[] = [{ index: 10, flags: 0, logEntries: [
    { logText: 'Old', note: '', flags: 0, conditions: [condition(1)] },
    { logText: 'New', note: '', flags: 0 },
  ] }];
  const sent = JSON.parse(stagesJson(stages));
  expect(sent[0].logEntries[0].conditions).toEqual([condition(1)]);
  expect(sent[0].logEntries[1].conditions).toEqual([]);
});

it('Detects: deleting a log entry drops only its conditions, and the entries that stay keep their own', () => {
  const [loaded] = parseStages({ stages: [{ index: 10, flags: 0, logEntries: [
    { logText: 'A', note: '', flags: 0, conditions: [condition(1)] },
    { logText: 'B', note: '', flags: 0, conditions: [condition(2)] },
    { logText: 'C', note: '', flags: 0, conditions: [condition(3)] },
  ] }] });
  const afterDelete = [{ ...loaded, logEntries: loaded.logEntries.filter((_, at) => at !== 1) }];
  const sent = JSON.parse(stagesJson(afterDelete));
  expect(sent[0].logEntries.map((entry: { logText: string }) => entry.logText)).toEqual(['A', 'C']);
  expect(sent[0].logEntries[0].conditions).toEqual([condition(1)]);
  expect(sent[0].logEntries[1].conditions).toEqual([condition(3)]);
});

it('Detects: a log entry flagged conditionsUnreadable omits the conditions key, so native keeps its conditions by position', () => {
  const stages = parseStages({ stages: [{ index: 10, flags: 0, logEntries: [
    { logText: 'Hard', note: '', flags: 0, conditionsUnreadable: true },
  ] }] });
  const [entry] = JSON.parse(stagesJson(stages))[0].logEntries;
  expect(entry).toEqual({ logText: 'Hard', flags: 0 });
  expect('conditions' in entry).toBe(false);
});

it('Detects: parseStages ignores unknown fields without crashing', () => {
  const stages = parseStages({ stages: [{
    index: 10,
    flags: 2,
    startUp: true,
    flagNames: ['Run when the stage starts'],
    futureField: 1,
    logEntries: [{ logText: 'Go', flags: 1, futureEntryField: { nested: true }, conditions: [] }],
  }] });
  expect(stages).toEqual([{ index: 10, flags: 2, logEntries: [{ logText: 'Go', note: '', flags: 1, conditions: [] }] }]);
  expect(() => stagesJson(stages)).not.toThrow();
});
