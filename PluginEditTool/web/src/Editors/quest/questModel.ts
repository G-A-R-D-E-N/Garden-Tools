export type AliasKind = 'reference' | 'location' | 'collection';

export interface StageLogEntry { logText: string; note: string; flags: number; }
export interface QuestStage { index: number; flags: number; logEntries: StageLogEntry[]; }
export interface StageSelection { index: number; }
export interface StageJump { from: number; to: number; function: string; }
export type StageEdit = (current: QuestStage[]) => QuestStage[];
export interface ObjectiveTarget { alias: number; aliasName: string; flags: number; }
export interface QuestObjective { index: number; flags: number; displayText: string; targets: ObjectiveTarget[]; }
export interface CollectionEntry { id: number; maxInitialFillCount: number | null; }
export interface QuestAlias {
  id: number;
  kind: AliasKind;
  name: string;
  flags: number | null;
  collectionEntries: CollectionEntry[];
  fill?: Record<string, unknown>;
}

export interface FlagOption { bit: number; label: string; }

export const STAGE_FLAG_OPTIONS: readonly FlagOption[] = [
  { bit: 0x02, label: 'Run when the stage starts' },
  { bit: 0x04, label: 'Run when the stage stops' },
  { bit: 0x08, label: 'Keep instance data from here on' },
];

export const LOG_ENTRY_FLAG_OPTIONS: readonly FlagOption[] = [
  { bit: 1, label: 'Completes the quest' },
  { bit: 2, label: 'Fails the quest' },
];

export const OBJECTIVE_FLAG_OPTIONS: readonly FlagOption[] = [
  { bit: 0x01, label: 'Shown together with the previous objective (OR)' },
  { bit: 0x02, label: 'Do not count in the player statistics' },
];

export const ALIAS_KINDS: readonly AliasKind[] = ['reference', 'location', 'collection'];

interface RawLogEntry { logText?: string; note?: string; flags: number; }
interface RawStage { index: number; flags: number; logEntries?: RawLogEntry[]; }
interface RawTarget { alias: number; aliasName?: string; flags: number; }
interface RawObjective { index: number; flags: number; displayText?: string; targets?: RawTarget[]; }
interface RawCollectionEntry { id: number; maxInitialFillCount?: number; }
interface RawAlias {
  id: number;
  kind: AliasKind;
  name?: string;
  flags?: number;
  collectionEntries?: RawCollectionEntry[];
}

export function parseStages(raw: unknown): QuestStage[] {
  const rows = (raw as { stages?: RawStage[] } | null)?.stages ?? [];
  return rows.map(row => ({
    index: row.index,
    flags: row.flags,
    logEntries: (row.logEntries ?? []).map(entry => ({
      logText: entry.logText ?? '',
      note: entry.note ?? '',
      flags: entry.flags,
    })),
  }));
}

export function parseObjectives(raw: unknown): QuestObjective[] {
  const rows = (raw as { objectives?: RawObjective[] } | null)?.objectives ?? [];
  return rows.map(row => ({
    index: row.index,
    flags: row.flags,
    displayText: row.displayText ?? '',
    targets: (row.targets ?? []).map(target => ({
      alias: target.alias,
      aliasName: target.aliasName ?? '',
      flags: target.flags,
    })),
  }));
}

export function parseAliases(raw: unknown): QuestAlias[] {
  const rows = (raw as { aliases?: RawAlias[] } | null)?.aliases ?? [];
  return rows.map(row => ({
    id: row.id,
    kind: row.kind,
    name: row.name ?? '',
    flags: row.flags ?? null,
    collectionEntries: (row.collectionEntries ?? []).map(entry => ({
      id: entry.id,
      maxInitialFillCount: entry.maxInitialFillCount ?? null,
    })),
  }));
}

export function stagesJson(stages: QuestStage[]): string {
  return JSON.stringify(stages.map(stage => ({
    index: stage.index,
    flags: stage.flags,
    logEntries: stage.logEntries.map(entry => ({
      flags: entry.flags,
      ...(entry.logText ? { logText: entry.logText } : {}),
      ...(entry.note ? { note: entry.note } : {}),
    })),
  })));
}

export function objectivesJson(objectives: QuestObjective[]): string {
  return JSON.stringify(objectives.map(objective => ({
    index: objective.index,
    flags: objective.flags,
    displayText: objective.displayText,
    targets: objective.targets.map(target => ({ alias: target.alias, flags: target.flags })),
  })));
}

export function aliasesJson(aliases: QuestAlias[]): string {
  return JSON.stringify(aliases.map(alias => {
    const row: Record<string, unknown> = { kind: alias.kind };
    if (alias.kind === 'collection') {
      row.entries = alias.collectionEntries.map(entry => (
        entry.maxInitialFillCount === null
          ? { id: entry.id }
          : { id: entry.id, maxInitialFillCount: entry.maxInitialFillCount }
      ));
    } else {
      row.id = alias.id;
      if (alias.flags !== null) row.flags = alias.flags;
    }
    if (alias.name) row.name = alias.name;
    if (alias.fill) Object.assign(row, alias.fill);
    return row;
  }));
}

export function toggleBit(flags: number, bit: number, on: boolean): number {
  return on ? flags | bit : flags & ~bit;
}

export const normalizeFormKey = (formKey: string) => formKey.trim().toLowerCase();

export function duplicateNumbers(values: number[]): number[] {
  const counts = new Map<number, number>();
  for (const value of values) counts.set(value, (counts.get(value) ?? 0) + 1);
  return [...counts].filter(([, count]) => count > 1).map(([value]) => value).sort((a, b) => a - b);
}

export function duplicateObjectiveNumbers(objectives: QuestObjective[]): number[] {
  return duplicateNumbers(objectives.map(objective => objective.index));
}

export function duplicateObjectiveWarning(objectives: QuestObjective[]): string {
  const numbers = duplicateObjectiveNumbers(objectives);
  if (numbers.length === 0) return '';
  return `Two or more objectives use the same number (${numbers.join(', ')}). Each objective needs its own number, `
    + 'so change one of them.';
}
