import { expect, it } from 'vitest';
import { buildFlowChart, type FlowArrow, type FlowChart } from './flowChartModel';
import type { QuestObjective, QuestStage } from './questModel';

const stage = (index: number, logText = '', flags = 0): QuestStage =>
  ({ index, flags: 0, logEntries: [{ logText, note: '', flags }] });
const objective = (index: number): QuestObjective => ({ index, flags: 0, displayText: '', targets: [] });
const stageBox = (chart: FlowChart, index: number) => chart.boxes.find(box => box.id === `stage:${index}`)!;
const nextLinks = (chart: FlowChart) => chart.arrows
  .filter((arrow: FlowArrow) => arrow.kind === 'next')
  .map(arrow => [arrow.from, arrow.to]);

it('Detects: a stage gets the Has objective badge only when an objective has the same number, not the same position', () => {
  const chart = buildFlowChart([stage(10), stage(20)], [objective(20)]);
  expect(stageBox(chart, 10).badges).not.toContain('Has objective');
  expect(stageBox(chart, 20).badges).toContain('Has objective');
});

it('Detects: a stage whose log entry completes or fails the quest gets its badge and end box, and no next-in-number arrow', () => {
  const chart = buildFlowChart([stage(10, 'The end', 1), stage(20), stage(30, 'Gone', 2)], []);
  expect(stageBox(chart, 10).badges).toContain('Completes quest');
  expect(stageBox(chart, 30).badges).toContain('Fails quest');
  expect(chart.boxes.find(box => box.kind === 'end' && box.stageIndex === 10)?.text).toBe('Completes the quest');
  expect(chart.boxes.find(box => box.kind === 'end' && box.stageIndex === 30)?.text).toBe('Fails the quest');
  expect(chart.arrows.some(arrow => arrow.kind === 'ends' && arrow.from === 'stage:10')).toBe(true);
  expect(nextLinks(chart)).toEqual([['stage:20', 'stage:30']]);
});

it('Detects: stages are chained by number, not by the order they are stored in', () => {
  const chart = buildFlowChart([stage(30), stage(10), stage(20)], []);
  expect(nextLinks(chart)).toEqual([['stage:10', 'stage:20'], ['stage:20', 'stage:30']]);
  expect(stageBox(chart, 10).x).toBeLessThan(stageBox(chart, 20).x);
});

it('Detects: the box shows the first non-blank journal line, skipping blank lines and blank entries', () => {
  const chart = buildFlowChart([{
    index: 10,
    flags: 0,
    logEntries: [
      { logText: '   ', note: '', flags: 0 },
      { logText: '\n  Find the courier\nSecond line', note: '', flags: 0 },
    ],
  }], []);
  expect(stageBox(chart, 10).text).toBe('Find the courier');
  expect(stageBox(chart, 10).badges).toContain('Has journal text');
});

it('Detects: a stage with no journal text and no objective of the same number is drawn as a problem, and a completing stage as an ending', () => {
  const chart = buildFlowChart([stage(10), stage(20, 'Go', 1), stage(30, 'Here')], [objective(30)]);
  expect(stageBox(chart, 10).state).toBe('problem');
  expect(stageBox(chart, 20).state).toBe('ending');
  expect(stageBox(chart, 30).state).toBe('ok');
});
