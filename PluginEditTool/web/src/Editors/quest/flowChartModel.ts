import { LOG_ENTRY_FLAG_OPTIONS, type QuestObjective, type QuestStage } from './questModel';

const PAD = 12;
const BOX_WIDTH = 180;
const STAGE_HEIGHT = 112;
const COLUMN_GAP = 56;
const END_GAP_Y = 28;
const END_HEIGHT = 46;
const END_GAP_X = 8;

const ENDINGS = [
  { flag: LOG_ENTRY_FLAG_OPTIONS[0], badge: 'Completes quest' },
  { flag: LOG_ENTRY_FLAG_OPTIONS[1], badge: 'Fails quest' },
];

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
}

export interface FlowArrow {
  id: string;
  kind: 'next' | 'ends';
  from: string;
  to: string;
  title: string;
  x1: number;
  y1: number;
  x2: number;
  y2: number;
}

export interface FlowChart {
  boxes: FlowBox[];
  arrows: FlowArrow[];
  width: number;
  height: number;
}

const stageId = (index: number) => `stage:${index}`;

const columnX = (column: number) => PAD + column * (BOX_WIDTH + COLUMN_GAP);

const hasFlag = (stage: QuestStage, bit: number) => stage.logEntries.some(entry => (entry.flags & bit) !== 0);

function firstJournalLine(stage: QuestStage): string {
  for (const entry of stage.logEntries) {
    const line = entry.logText.split(/\r?\n/).map(part => part.trim()).find(part => part !== '');
    if (line) return line;
  }
  return '';
}

export function buildFlowChart(stages: QuestStage[], objectives: QuestObjective[]): FlowChart {
  const ordered = [...stages].sort((a, b) => a.index - b.index);
  const objectiveNumbers = new Set(objectives.map(objective => objective.index));
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
    boxes.push({ id, kind: 'stage', stageIndex: stage.index, x, y: PAD, width: BOX_WIDTH, height: STAGE_HEIGHT, text, badges });

    const endWidth = (BOX_WIDTH - END_GAP_X * (endings.length - 1)) / endings.length;
    endings.forEach((ending, at) => {
      const endBox: FlowBox = {
        id: `end:${stage.index}:${ending.flag.bit}`,
        kind: 'end',
        stageIndex: stage.index,
        x: x + at * (endWidth + END_GAP_X),
        y: PAD + STAGE_HEIGHT + END_GAP_Y,
        width: endWidth,
        height: END_HEIGHT,
        text: ending.flag.label,
        badges: [],
      };
      boxes.push(endBox);
      arrows.push({
        id: `ends:${stage.index}:${ending.flag.bit}`,
        kind: 'ends',
        from: id,
        to: endBox.id,
        title: `Stage ${stage.index} ends the quest here (${ending.flag.label})`,
        x1: x + BOX_WIDTH / 2,
        y1: PAD + STAGE_HEIGHT,
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
        y1: PAD + STAGE_HEIGHT / 2,
        x2: columnX(column + 1),
        y2: PAD + STAGE_HEIGHT / 2,
      });
    }
  });

  return {
    boxes,
    arrows,
    width: Math.max(0, ...boxes.map(box => box.x + box.width)) + PAD,
    height: Math.max(0, ...boxes.map(box => box.y + box.height)) + PAD,
  };
}
