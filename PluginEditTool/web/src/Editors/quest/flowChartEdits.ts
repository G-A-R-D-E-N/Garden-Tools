import { journalEntryIndex } from './flowChartModel';
import { toggleBit, type QuestStage, type StageLogEntry } from './questModel';

const EMPTY_ENTRY: StageLogEntry = { logText: '', note: '', flags: 0 };

export function nextStageNumber(stages: QuestStage[], after: number): number {
  const used = new Set(stages.map(stage => stage.index));
  const plusTen = after + 10;
  if (!used.has(plusTen)) return plusTen;
  const following = stages.map(stage => stage.index).filter(index => index > after).sort((a, b) => a - b)[0];
  if (following !== undefined) {
    const middle = Math.floor((after + following) / 2);
    if (middle > after && !used.has(middle)) return middle;
  }
  return stages.reduce((highest, stage) => Math.max(highest, stage.index), 0) + 10;
}

export function addStageAfter(stages: QuestStage[], after: number): QuestStage[] {
  const index = nextStageNumber(stages, after);
  return [...stages, { index, flags: 0, logEntries: [] }].sort((a, b) => a.index - b.index);
}

export function removeStage(stages: QuestStage[], index: number): QuestStage[] {
  return stages.filter(stage => stage.index !== index);
}

function editEntry(entries: StageLogEntry[], at: number, change: (entry: StageLogEntry) => StageLogEntry): StageLogEntry[] {
  const list = entries.length === 0 ? [EMPTY_ENTRY] : entries;
  return list.map((entry, i) => (i === at ? change(entry) : entry));
}

function editStage(stages: QuestStage[], index: number, change: (stage: QuestStage) => QuestStage): QuestStage[] {
  return stages.map(stage => (stage.index === index ? change(stage) : stage));
}

export function setStageText(stages: QuestStage[], index: number, text: string): QuestStage[] {
  return editStage(stages, index, stage => ({
    ...stage,
    logEntries: editEntry(stage.logEntries, journalEntryIndex(stage), entry => ({ ...entry, logText: text })),
  }));
}

export function setOutcome(stages: QuestStage[], index: number, bit: number, on: boolean): QuestStage[] {
  return editStage(stages, index, stage => ({
    ...stage,
    logEntries: on
      ? editEntry(stage.logEntries, 0, entry => ({ ...entry, flags: toggleBit(entry.flags, bit, true) }))
      : stage.logEntries.map(entry => ({ ...entry, flags: toggleBit(entry.flags, bit, false) })),
  }));
}
