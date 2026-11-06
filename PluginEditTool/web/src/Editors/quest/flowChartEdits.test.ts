import { expect, it } from 'vitest';
import { addStageAfter, nextStageNumber, removeStage, setOutcome, setStageText } from './flowChartEdits';
import type { QuestStage } from './questModel';

const stage = (index: number, logEntries: QuestStage['logEntries'] = []): QuestStage => ({ index, flags: 0, logEntries });
const numbers = (stages: QuestStage[]) => stages.map(item => item.index);

it('Detects: adding a step after the last stage never reuses an existing stage number', () => {
  expect(nextStageNumber([stage(10), stage(20)], 10)).toBe(30);
  expect(nextStageNumber([stage(10), stage(30)], 10)).toBe(20);
  expect(numbers(addStageAfter([stage(10), stage(20)], 20))).toEqual([10, 20, 30]);
});

it('Detects: removing a stage leaves the other stages exactly as they were', () => {
  const kept = stage(10, [{ logText: 'Keep me', note: 'For the CK', flags: 1 }]);
  const last = stage(30);
  const result = removeStage([kept, stage(20), last], 20);
  expect(result).toEqual([kept, last]);
  expect(result[0]).toBe(kept);
});

it('Detects: setting an outcome changes only bit 1 or 2 on the first log entry and keeps other flag bits', () => {
  const second = { logText: 'Later', note: '', flags: 0 };
  const stages = [stage(10, [{ logText: 'Find it', note: '', flags: 6 }, second])];
  const completed = setOutcome(stages, 10, 1, true);
  expect(completed[0].logEntries[0].flags).toBe(7);
  expect(completed[0].logEntries[1]).toBe(second);
  expect(setOutcome(completed, 10, 2, false)[0].logEntries[0].flags).toBe(5);
});

it('Detects: editing journal text keeps the first entry note and flags', () => {
  const stages = [stage(10, [{ logText: 'Old', note: 'For the CK', flags: 2 }])];
  expect(setStageText(stages, 10, 'New text')[0].logEntries).toEqual([{ logText: 'New text', note: 'For the CK', flags: 2 }]);
});
