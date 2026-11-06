import { expect, it, vi } from 'vitest';
import type { Backend } from '../../backend';
import { buildFlowChart, type FlowArrow, type FlowChart } from './flowChartModel';
import { parseStageJumps, readFragments } from './questBackend';
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

const jumpArrows = (chart: FlowChart) => chart.arrows.filter(arrow => arrow.kind === 'jump');
const jump = (from: number, to: number) => ({ from, to, function: 'Fragment_Stage_0010_Item_00' });

it('Detects: a script jump draws one arc from the top centre of the source box to the top centre of the target, and moves the box row down to make room', () => {
  const plain = buildFlowChart([stage(10), stage(20), stage(30)], []);
  const chart = buildFlowChart([stage(10), stage(20), stage(30)], [], [jump(10, 30)]);
  const [arc] = jumpArrows(chart);
  expect(arc).toMatchObject({ id: 'jump:10:30', from: 'stage:10', to: 'stage:30', title: 'Script jump: stage 10 sets stage 30' });
  expect(arc.d).toBeDefined();
  expect(arc.x1).toBe(stageBox(chart, 10).x + stageBox(chart, 10).width / 2);
  expect(arc.x2).toBe(stageBox(chart, 30).x + stageBox(chart, 30).width / 2);
  expect(arc.y1).toBe(stageBox(chart, 10).y);
  expect(stageBox(chart, 10).y).toBeGreaterThan(stageBox(plain, 10).y);
  expect(nextLinks(chart)).toEqual([['stage:10', 'stage:20'], ['stage:20', 'stage:30']]);
});

it('Detects: a jump to a missing stage or from a stage to itself draws nothing and does not move the box row', () => {
  const chart = buildFlowChart([stage(10), stage(20)], [], [jump(10, 99), jump(77, 10), jump(20, 20)]);
  expect(jumpArrows(chart)).toEqual([]);
  expect(stageBox(chart, 10).y).toBe(stageBox(buildFlowChart([stage(10), stage(20)], []), 10).y);
});

it('Detects: the same stage pair listed twice draws a single arc', () => {
  const chart = buildFlowChart([stage(10), stage(20)], [], [jump(10, 20), jump(10, 20)]);
  expect(jumpArrows(chart).map(arrow => arrow.id)).toEqual(['jump:10:20']);
});

const binding = (stageIndex: number, fn: string) => ({ stage: stageIndex, function: fn, fragmentScript: 'QF_Courier' });
const scripts = (source: string) => new Map([['QF_Courier', source]]);

it('Detects: SetStage and SetCurrentStageID in a bound function become jumps from the binding stage, whatever the case', () => {
  const source = 'Function fragment_stage_0010_item_00()\n  setstage( 20 )\n  GetOwningQuest().SetCurrentStageID(30)\nEndFunction';
  expect(parseStageJumps([binding(10, 'Fragment_Stage_0010_Item_00')], scripts(source)))
    .toEqual([{ from: 10, to: 20, function: 'Fragment_Stage_0010_Item_00' }, { from: 10, to: 30, function: 'Fragment_Stage_0010_Item_00' }]);
});

it('Detects: a SetStage in a function that no binding names is not a jump', () => {
  const source = 'Function Helper()\n  SetStage(99)\nEndFunction\nFunction Fragment_Stage_0010_Item_00()\n  SetStage(20)\nEndFunction';
  expect(parseStageJumps([binding(10, 'Fragment_Stage_0010_Item_00')], scripts(source)))
    .toEqual([{ from: 10, to: 20, function: 'Fragment_Stage_0010_Item_00' }]);
});

it('Detects: every SetStage in a bound function is read, including one in a typed Int Function', () => {
  const source = 'Int Function Fragment_Stage_0010_Item_00()\n  SetStage(20)\n  If x\n    SetStage(30)\n  EndIf\nEndFunction';
  expect(parseStageJumps([binding(10, 'Fragment_Stage_0010_Item_00')], scripts(source)).map(found => found.to)).toEqual([20, 30]);
});

it('Detects: a bound function the source does not define, or a script with no source, yields no jumps', () => {
  const source = 'Function Other()\n  SetStage(20)\nEndFunction';
  expect(parseStageJumps([binding(10, 'Fragment_Stage_0010_Item_00')], scripts(source))).toEqual([]);
  expect(parseStageJumps([binding(10, 'Other')], new Map())).toEqual([]);
});

it('Detects: a script whose source cannot be read is skipped, and the other scripts still give their jumps', async () => {
  const backend = {
    GetQuestFragments: async () => JSON.stringify([
      { stage: 10, logEntryIndex: 0, script: 'QF_Courier', function: 'Fragment_Stage_0010_Item_00', fragmentScript: 'QF_Broken' },
      { stage: 20, logEntryIndex: 0, script: 'QF_Courier', function: 'Fragment_Stage_0020_Item_00', fragmentScript: 'QF_Courier' },
    ]),
    GetQuestScript: async (name: string) => {
      if (name === 'QF_Broken') throw new Error('QuestScriptNotFound');
      return JSON.stringify({ script: name, source: 'Function Fragment_Stage_0020_Item_00()\n  SetStage(40)\nEndFunction' });
    },
  };
  expect((await readFragments(backend as unknown as Backend, 'Patch.esp', '000800:Patch.esp')).jumps)
    .toEqual([{ from: 20, to: 40, function: 'Fragment_Stage_0020_Item_00' }]);
});

it('Detects: the imports folder is passed to GetQuestScript, so a script written to that folder can be read', async () => {
  const GetQuestScript = vi.fn(async () => JSON.stringify({ source: '' }));
  const backend = {
    GetQuestFragments: async () => JSON.stringify([
      { stage: 10, logEntryIndex: 0, script: 'QF_Courier', function: 'Fragment_Stage_0010_Item_00', fragmentScript: 'QF_Courier' },
    ]),
    GetQuestScript,
  };
  await readFragments(backend as unknown as Backend, 'Patch.esp', '000800:Patch.esp', 'C:/Out');
  expect(GetQuestScript).toHaveBeenCalledWith('QF_Courier', 'C:/Out');
});

it('Detects: a SetStage inside a Papyrus line comment or block comment is not a jump', () => {
  const source = 'Function Fragment_Stage_0010_Item_00()\n  ; SetStage(30)\n  ;/ SetStage(40) /;\n  ;/\n  SetStage(50)\n  /;\nEndFunction';
  expect(parseStageJumps([binding(10, 'Fragment_Stage_0010_Item_00')], scripts(source))).toEqual([]);
});

it('Detects: a bodiless native declaration does not swallow the bound function that follows it', () => {
  const source = 'Function Native_Helper() global native\nFunction Fragment_Stage_0010_Item_00()\n  SetStage(20)\nEndFunction';
  expect(parseStageJumps([binding(10, 'Fragment_Stage_0010_Item_00')], scripts(source)))
    .toEqual([{ from: 10, to: 20, function: 'Fragment_Stage_0010_Item_00' }]);
});
