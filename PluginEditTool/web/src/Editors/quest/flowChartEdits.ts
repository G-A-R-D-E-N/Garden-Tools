import { toggleBit, type QuestStage, type StageLogEntry } from './questModel';

const EMPTY_ENTRY: StageLogEntry = { logText: '', note: '', flags: 0 };

export function nextStageNumber(stages: QuestStage[], after: number): number {
  const used = new Set(stages.map(stage => stage.index));
  const nearby = (Math.floor(after / 10) + 1) * 10;
  if (!used.has(nearby)) return nearby;
  return stages.reduce((highest, stage) => Math.max(highest, stage.index), 0) + 10;
}

export function addStageAfter(stages: QuestStage[], after: number): QuestStage[] {
  const index = nextStageNumber(stages, after);
  return [...stages, { index, flags: 0, logEntries: [] }].sort((a, b) => a.index - b.index);
}

export function removeStage(stages: QuestStage[], index: number): QuestStage[] {
  return stages.filter(stage => stage.index !== index);
}

function editFirstEntry(stages: QuestStage[], index: number, change: (entry: StageLogEntry) => StageLogEntry): QuestStage[] {
  return stages.map(stage => {
    if (stage.index !== index) return stage;
    const [first = EMPTY_ENTRY, ...rest] = stage.logEntries;
    return { ...stage, logEntries: [change(first), ...rest] };
  });
}

export function setStageText(stages: QuestStage[], index: number, text: string): QuestStage[] {
  return editFirstEntry(stages, index, entry => ({ ...entry, logText: text }));
}

export function setOutcome(stages: QuestStage[], index: number, bit: number, on: boolean): QuestStage[] {
  return editFirstEntry(stages, index, entry => ({ ...entry, flags: toggleBit(entry.flags, bit, on) }));
}
