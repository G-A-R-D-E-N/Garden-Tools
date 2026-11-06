import { LOG_ENTRY_FLAG_OPTIONS, type QuestObjective, type QuestStage, type StageJump } from './questModel';

const PAD = 12;
const BOX_WIDTH = 220;
const STAGE_HEIGHT = 200;
const COLUMN_GAP = 56;
const END_GAP_Y = 28;
const END_HEIGHT = 46;
const END_GAP_X = 8;
const JUMP_LANE = 40;

const ENDINGS = [
  { flag: LOG_ENTRY_FLAG_OPTIONS[0], badge: 'Completes quest' },
  { flag: LOG_ENTRY_FLAG_OPTIONS[1], badge: 'Fails quest' },
];

export type FlowState = 'ok' | 'problem' | 'ending';

export interface FlowBox {
  id: string;
  kind: 'stage' | 'end';
  stageIndex: number;
  x: number;
  y: number;
  width: number;
  height: number;
  text: string;
  badges: string[];
  state: FlowState;
}

export interface FlowArrow {
  id: string;
  kind: 'next' | 'ends' | 'jump';
  from: string;
  to: string;
  title: string;
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  d?: string;
}

export interface FlowChart {
  boxes: FlowBox[];
  arrows: FlowArrow[];
  width: number;
  height: number;
}

const stageId = (index: number) => `stage:${index}`;

const columnX = (column: number) => PAD + column * (BOX_WIDTH + COLUMN_GAP);

export const hasFlag = (stage: QuestStage, bit: number) => stage.logEntries.some(entry => (entry.flags & bit) !== 0);

const journalLine = (text: string): string => text.split(/\r?\n/).map(part => part.trim()).find(part => part !== '') ?? '';

export function journalEntryIndex(stage: QuestStage): number {
  const at = stage.logEntries.findIndex(entry => journalLine(entry.logText) !== '');
  return at === -1 ? 0 : at;
}

function firstJournalLine(stage: QuestStage): string {
  const entry = stage.logEntries[journalEntryIndex(stage)];
  return entry ? journalLine(entry.logText) : '';
}

export function buildFlowChart(stages: QuestStage[], objectives: QuestObjective[], jumps: StageJump[] = []): FlowChart {
  const ordered = [...stages].sort((a, b) => a.index - b.index);
  const objectiveNumbers = new Set(objectives.map(objective => objective.index));
  const centres = new Map<number, number>(ordered.map((stage, column): [number, number] => [stage.index, columnX(column) + BOX_WIDTH / 2]));
  const links = jumps.flatMap(jump => {
    const from = centres.get(jump.from);
    const to = centres.get(jump.to);
    return from === undefined || to === undefined || jump.from === jump.to ? [] : [{ jump, from, to }];
  });
  const arcs = [...new Map(links.map(link => [`${link.jump.from}:${link.jump.to}`, link] as const)).values()];
  const top = PAD + (arcs.length > 0 ? JUMP_LANE : 0);
  const boxes: FlowBox[] = [];
  const arrows: FlowArrow[] = [];

  ordered.forEach((stage, column) => {
    const x = columnX(column);
    const text = firstJournalLine(stage);
    const endings = ENDINGS.filter(ending => hasFlag(stage, ending.flag.bit));
    const badges = [
      ...(text ? ['Has journal text'] : []),
      ...(objectiveNumbers.has(stage.index) ? ['Has objective'] : []),
      ...endings.map(ending => ending.badge),
    ];
    const id = stageId(stage.index);
    const state: FlowState = !text && !objectiveNumbers.has(stage.index) ? 'problem' : endings.length > 0 ? 'ending' : 'ok';
    boxes.push({
      id, kind: 'stage', stageIndex: stage.index, x, y: top, width: BOX_WIDTH, height: STAGE_HEIGHT, text, badges, state,
    });

    const endWidth = (BOX_WIDTH - END_GAP_X * (endings.length - 1)) / endings.length;
    endings.forEach((ending, at) => {
      const endBox: FlowBox = {
        id: `end:${stage.index}:${ending.flag.bit}`,
        kind: 'end',
        stageIndex: stage.index,
        x: x + at * (endWidth + END_GAP_X),
        y: top + STAGE_HEIGHT + END_GAP_Y,
        width: endWidth,
        height: END_HEIGHT,
        text: ending.flag.label,
        badges: [],
        state: 'ending',
      };
      boxes.push(endBox);
      arrows.push({
        id: `ends:${stage.index}:${ending.flag.bit}`,
        kind: 'ends',
        from: id,
        to: endBox.id,
        title: `Stage ${stage.index} ends the quest here (${ending.flag.label})`,
        x1: x + BOX_WIDTH / 2,
        y1: top + STAGE_HEIGHT,
        x2: endBox.x + endWidth / 2,
        y2: endBox.y,
      });
    });

    const next = ordered[column + 1];
    if (endings.length === 0 && next) {
      arrows.push({
        id: `next:${stage.index}:${next.index}`,
        kind: 'next',
        from: id,
        to: stageId(next.index),
        title: `Next in number order: stage ${stage.index} to stage ${next.index}`,
        x1: x + BOX_WIDTH,
        y1: top + STAGE_HEIGHT / 2,
        x2: columnX(column + 1),
        y2: top + STAGE_HEIGHT / 2,
      });
    }
  });

  for (const { jump, from, to } of arcs) {
    arrows.push({
      id: `jump:${jump.from}:${jump.to}`,
      kind: 'jump',
      from: stageId(jump.from),
      to: stageId(jump.to),
      title: `Script jump: stage ${jump.from} sets stage ${jump.to}`,
      x1: from,
      y1: top,
      x2: to,
      y2: top,
      d: `M${from} ${top} C${from} ${top - JUMP_LANE} ${to} ${top - JUMP_LANE} ${to} ${top}`,
    });
  }

  return {
    boxes,
    arrows,
    width: Math.max(0, ...boxes.map(box => box.x + box.width)) + PAD,
    height: Math.max(0, ...boxes.map(box => box.y + box.height)) + PAD,
  };
}
