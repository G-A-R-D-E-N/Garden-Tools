import { hasFlag, isProblemStage } from './flowChartModel';
import type { QuestScope, QuestSnapshot, ScopeKind } from './questBackend';
import { duplicateNumbers, LOG_ENTRY_FLAG_OPTIONS, normalizeFormKey } from './questModel';

export type CheckSeverity = 'error' | 'warning' | 'info';
export type CheckPane = 'stages' | 'objectives' | 'aliases' | 'fragments';

export interface CheckFinding {
  severity: CheckSeverity;
  message: string;
  area: string;
  pane?: CheckPane;
  stage?: number;
  record?: string;
  detail?: string;
}

export interface CheckRow {
  Code?: string;
  Detail?: string;
  Severity?: string;
  Plugin?: string;
  FormKey?: string;
  Subrecord?: string;
  Target?: string;
  Message?: string;
}

export const CHECK_LIMIT = 5000;

export const PANE_HEADINGS: Record<CheckPane, string> = {
  stages: 'quest-stages-title',
  objectives: 'quest-objectives-title',
  aliases: 'quest-aliases-title',
  fragments: 'quest-fragments-title',
};

export const PANE_AREAS: Record<CheckPane, string> = {
  stages: 'Stages',
  objectives: 'Objectives',
  aliases: 'Aliases',
  fragments: 'Fragments',
};

const UNREADABLE = 'The game record check returned a result the editor could not read.';
const TRUNCATED = 'The game record check stopped at its row limit, so this list may be missing problems.';

const FRIENDLY_CODES: Partial<Record<string, string>> = {
  unsupported_vmad_property: 'A script property uses a type this editor cannot check.',
};

const SUBRECORD_PANES: Partial<Record<string, CheckPane>> = {
  INDX: 'stages',
  QSDT: 'stages',
  QOBJ: 'objectives',
  QSTA: 'objectives',
  ALST: 'aliases',
  ALID: 'aliases',
  ALFR: 'aliases',
  ALFL: 'aliases',
  VMAD: 'fragments',
};

const KIND_AREAS: Record<ScopeKind, string> = {
  quest: 'Quest record',
  dialogue: 'Dialogue record',
  scene: 'Scene record',
};

const SEVERITY_ORDER: Record<CheckSeverity, number> = { error: 0, warning: 1, info: 2 };

const finding = (severity: CheckSeverity, message: string, area: string, pane?: CheckPane, stage?: number): CheckFinding =>
  ({ severity, message, area, pane, stage });

export function localFindings(snapshot: QuestSnapshot): CheckFinding[] {
  const objectiveNumbers = new Set(snapshot.objectives.map(objective => objective.index));
  const stageNumbers = new Set(snapshot.stages.map(stage => stage.index));
  const aliasIds = new Set(snapshot.aliases.map(alias => alias.id));
  const findings: CheckFinding[] = [];

  if (snapshot.stages.length > 0 && !snapshot.stages.some(stage => LOG_ENTRY_FLAG_OPTIONS.some(option => hasFlag(stage, option.bit)))) {
    findings.push(finding('warning', 'No stage completes or fails the quest, so the quest can never finish.', PANE_AREAS.stages, 'stages'));
  }
  for (const stage of snapshot.stages) {
    if (isProblemStage(stage, objectiveNumbers)) {
      findings.push(finding('warning', `Stage ${stage.index} has no journal text and no objective with its number.`,
        `Stage ${stage.index}`, 'stages', stage.index));
    }
  }
  for (const number of duplicateNumbers(snapshot.stages.map(stage => stage.index))) {
    findings.push(finding('error', `Two or more stages use the number ${number}.`, `Stage ${number}`, 'stages', number));
  }
  for (const objective of snapshot.objectives) {
    if (!stageNumbers.has(objective.index)) {
      findings.push(finding('warning', `Objective ${objective.index} has no stage with the same number.`,
        `Objective ${objective.index}`, 'objectives'));
    }
    for (const alias of new Set(objective.targets.map(target => target.alias).filter(id => !aliasIds.has(id)))) {
      findings.push(finding('error', `Objective ${objective.index} points at alias ${alias}, which does not exist.`,
        `Objective ${objective.index}`, 'objectives'));
    }
  }
  const missingJumps = new Map(snapshot.jumps
    .filter(jump => !stageNumbers.has(jump.to))
    .map(jump => [`${jump.from}:${jump.to}`, jump] as const));
  for (const jump of missingJumps.values()) {
    findings.push(finding('error', `Stage ${jump.from} sets stage ${jump.to}, which does not exist.`, PANE_AREAS.fragments, 'fragments'));
  }
  if (snapshot.fragmentScripts === null) {
    findings.push(finding('info', 'The fragment scripts could not be read, so script jumps and script errors were not checked.',
      PANE_AREAS.fragments, 'fragments'));
  }
  for (const script of snapshot.fragmentScripts ?? []) {
    if (script.parses === false) {
      findings.push(finding('error', `Fragment script ${script.name} has syntax errors, so the game cannot compile it.`,
        PANE_AREAS.fragments, 'fragments'));
    } else if (script.parses === null) {
      findings.push(finding('info', `Fragment script ${script.name} could not be read, so it was not checked.`,
        PANE_AREAS.fragments, 'fragments'));
    }
  }
  return findings;
}

export function nativeFindings(rows: CheckRow[], scope: QuestScope): CheckFinding[] {
  return rows.flatMap((row): CheckFinding[] => {
    const code = row.Code ?? '';
    const detail = JSON.stringify(row, null, 2);
    if (code === 'results_truncated') return [{ severity: 'warning', message: TRUNCATED, area: 'Whole check', detail }];
    const kind = scope.get(normalizeFormKey(row.FormKey ?? ''));
    if (kind === undefined) return [];
    const pane = kind === 'quest' ? SUBRECORD_PANES[row.Subrecord ?? ''] : undefined;
    return [{
      severity: row.Severity === 'error' ? 'error' : 'warning',
      message: FRIENDLY_CODES[code] ?? (row.Message || code),
      area: pane ? PANE_AREAS[pane] : KIND_AREAS[kind],
      pane,
      record: kind === 'quest' ? undefined : row.FormKey,
      detail,
    }];
  });
}

const parseJson = (raw: string): unknown => {
  try {
    return JSON.parse(raw);
  } catch {
    throw new Error(UNREADABLE);
  }
};

export function parseCheckRows(raw: string | object): CheckRow[] {
  const rows = typeof raw === 'string' ? parseJson(raw) : raw;
  if (!Array.isArray(rows) || rows.some(row => typeof row !== 'object' || row === null)) throw new Error(UNREADABLE);
  return rows as CheckRow[];
}

export function sortFindings(findings: CheckFinding[]): CheckFinding[] {
  return [...findings].sort((a, b) => SEVERITY_ORDER[a.severity] - SEVERITY_ORDER[b.severity]);
}
