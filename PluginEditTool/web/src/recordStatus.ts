import type { ConflictFieldRow } from './backend';

export const defaultExpanded = (r: ConflictFieldRow) => !(r.IsSummary || r.DisplayLabel.startsWith('['));

export type Status = 'notdefined' | 'none' | 'identical' | 'master' | 'override' | 'win' | 'lose' | 'only';
export function cellStatus(vals: string[], idx: number): Status {
  const me = vals[idx] ?? '';
  if (me === '') return 'notdefined';
  const present = vals.filter(v => v !== '');
  let winner = -1;
  for (let i = vals.length - 1; i >= 0; i--) { if (vals[i] !== '') { winner = i; break; } }
  if (present.length <= 1) return 'only';
  const allSame = present.every(v => v === present[0]);
  if (allSame) return idx === 0 ? 'master' : 'identical';
  if (idx === winner) return 'win';
  if (idx === 0) return 'master';
  if (me === vals[winner]) return 'override';
  return 'lose';
}

export function resolveStatus(r: ConflictFieldRow, idx: number): Status {
  const s = r.Statuses?.[idx];
  if (s) return s as Status;
  return cellStatus(r.Values, idx);
}

export type SeverityFilter = 'all' | 'override' | 'conflict' | 'critical';
export const SEVERITY_SETS: Record<Exclude<SeverityFilter, 'all'>, Set<string>> = {
  override: new Set(['override', 'conflict', 'critical']),
  conflict: new Set(['conflict', 'critical']),
  critical: new Set(['critical']),
};

export const LEVEL_BADGES: Record<string, { label: string; cls: string }> = {
  noconflict: { label: 'No Conflict', cls: 'rv-lvl-noconflict' },
  override:   { label: 'Override', cls: 'rv-lvl-override' },
  conflict:   { label: 'Conflict', cls: 'rv-lvl-conflict' },
  critical:   { label: 'Critical', cls: 'rv-lvl-critical' },
  onlyone:    { label: 'Single Record', cls: 'rv-lvl-onlyone' },
};


export const LEGEND_ROWS: { cls: string; label: string }[] = [
  { cls: 'rv-s-none', label: 'No conflict' },
  { cls: 'rv-s-benign', label: 'Benign conflict' },
  { cls: 'rv-s-override', label: 'Override without conflict' },
  { cls: 'rv-s-conflict', label: 'Conflict' },
  { cls: 'rv-s-critical', label: 'Critical conflict' },
];


export const LEGEND_CELLS: { cls: string; label: string }[] = [
  { cls: 'rv-c-notdefined', label: 'Not defined' },
  { cls: 'rv-c-identical', label: 'Identical to master' },
  { cls: 'rv-c-only', label: 'Single record' },
  { cls: 'rv-c-master', label: 'Master' },
  { cls: 'rv-c-override', label: 'Override without conflict' },
  { cls: 'rv-c-win', label: 'Conflict winner (what the game uses)' },
  { cls: 'rv-c-lose', label: 'Conflict loser' },
];

export const SEV_ORDER: Record<string, number> = { none: 0, benign: 1, override: 2, conflict: 3, critical: 4 };
