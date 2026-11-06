import { expect, it } from 'vitest';
import { addStageAfter, nextStageNumber, removeStage, setOutcome, setStageText } from './flowChartEdits';
import type { QuestStage } from './questModel';

const stage = (index: number, logEntries: QuestStage['logEntries'] = []): QuestStage => ({ index, flags: 0, logEntries });
const numbers = (stages: QuestStage[]) => stages.map(item => item.index);

it('Detects: adding a step after the last stage never reuses an existing stage number', () => {
  expect(nextStageNumber([stage(10), stage(20)], 20)).toBe(30);
  expect(numbers(addStageAfter([stage(10), stage(20)], 20))).toEqual([10, 20, 30]);
  expect(nextStageNumber([stage(10), stage(30)], 10)).toBe(20);
});

it('Detects: when stage plus 10 is taken, the new step takes the free number between it and the next stage, or else the highest plus 10', () => {
  expect(nextStageNumber([stage(10), stage(20), stage(30)], 10)).toBe(15);
  expect(nextStageNumber([stage(10), stage(11), stage(20)], 10)).toBe(30);
});

it('Detects: removing a stage leaves the other stages exactly as they were', () => {
  const kept = stage(10, [{ logText: 'Keep me', note: 'For the CK', flags: 1 }]);
  const last = stage(30);
  const result = removeStage([kept, stage(20), last], 20);
  expect(result).toEqual([kept, last]);
  expect(result[0]).toBe(kept);
});

it('Detects: setting an outcome writes only its bit on the first log entry, keeps other flag bits, and creates the entry when there is none', () => {
  const second = { logText: 'Later', note: '', flags: 0 };
  const stages = [stage(10, [{ logText: 'Find it', note: '', flags: 6 }, second])];
  const completed = setOutcome(stages, 10, 1, true);
  expect(completed[0].logEntries[0].flags).toBe(7);
  expect(completed[0].logEntries[1]).toBe(second);
  expect(setOutcome([stage(10)], 10, 2, true)[0].logEntries).toEqual([{ logText: '', note: '', flags: 2 }]);
});

it('Detects: clearing an outcome removes that flag from every log entry, so a flag on a later entry cannot survive an uncheck', () => {
  const stages = [stage(10, [{ logText: 'A', note: '', flags: 7 }, { logText: 'B', note: '', flags: 9 }])];
  expect(setOutcome(stages, 10, 1, false)[0].logEntries.map(entry => entry.flags)).toEqual([6, 8]);
});

it('Detects: editing journal text keeps the first entry note and flags', () => {
  const stages = [stage(10, [{ logText: 'Old', note: 'For the CK', flags: 2 }])];
  expect(setStageText(stages, 10, 'New text')[0].logEntries).toEqual([{ logText: 'New text', note: 'For the CK', flags: 2 }]);
});

it('Detects: editing journal text changes the entry the box shows, not a blank first entry', () => {
  const stages = [stage(10, [
    { logText: '   ', note: 'a', flags: 0 },
    { logText: 'Second', note: 'b', flags: 2 },
  ])];
  expect(setStageText(stages, 10, 'New')[0].logEntries).toEqual([
    { logText: '   ', note: 'a', flags: 0 },
    { logText: 'New', note: 'b', flags: 2 },
  ]);
});
