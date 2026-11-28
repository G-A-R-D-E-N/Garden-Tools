export type DiagStatus = 'pass' | 'fail' | 'skip' | 'unsupported' | 'unverified' | 'error';
export type EvidenceKind = 'structural' | 'real-game-data' | 'independent-reference' | 'observed-in-game';

export interface DiagSide {
  label: string;
  source: string;
  value: string;
}

export interface DiagResult {
  id: string;
  title: string;
  status: DiagStatus;
  evidence: EvidenceKind[];
  left: DiagSide | null;
  right: DiagSide | null;
  diffs: string[];
  detail: string;
  durationMs: number;
  outputs: string[];
}

export interface DiagContext {
  versions: { fo4ide: string; radicleAbi: string; commit: string };
  dataDir: string;
  mo2Profile: string;
  loadOrder: string[];
  assetRootsLowToHigh: string[];
  settings: Record<string, string>;
  gameBuild: string;
}

export interface DiagSummary {
  total: number;
  pass: number;
  fail: number;
  skip: number;
  unsupported: number;
  unverified: number;
  error: number;
  structuralOnly: number;
}

export type RunVerdict = 'fail' | 'cancelled' | 'unverified' | 'incomplete' | 'structural' | 'pass' | 'empty';

export interface RunRecord {
  runId: string;
  toolId: string;
  title: string;
  groupId: string;
  at: string;
  suiteId: string;
  results: DiagResult[];
  error: string;
  cancelled: boolean;
}

export interface NotRun {
  toolId: string;
  title: string;
  reason: string;
}

export interface SessionSummary extends DiagSummary {
  runs: number;
  notRun: number;
  verdicts: Record<RunVerdict, number>;
}

export interface DiagReportInput {
  context: DiagContext | null;
  results: DiagResult[];
  toolId?: string;
  error?: string;
  cancelled?: boolean;
  generatedAt?: string;
  runs?: RunRecord[];
  notRun?: NotRun[];
}

export const DIAG_STATUSES: readonly DiagStatus[] = ['pass', 'fail', 'skip', 'unsupported', 'unverified', 'error'];
export const EVIDENCE_KINDS: readonly EvidenceKind[] = ['structural', 'real-game-data', 'independent-reference', 'observed-in-game'];
export const STATUS_ORDER: readonly DiagStatus[] = ['fail', 'error', 'unverified', 'pass', 'skip', 'unsupported'];
export const CANCELLED_NOTE = 'Run was cancelled: results are partial';
export const STRUCTURAL_LABEL = 'structural only — not proof the game accepts it';
export const BANNER = 'Parser/structural checks do not prove Fallout 4 accepts a file. '
  + 'In-game behaviour is unverified unless an observed-in-game result is shown.';

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);
const text = (value: unknown): string => (typeof value === 'string' ? value : '');
const num = (value: unknown): number => (typeof value === 'number' && Number.isFinite(value) ? value : 0);
const strings = (value: unknown): string[] =>
  Array.isArray(value) ? value.map(item => (typeof item === 'string' ? item : JSON.stringify(item))) : [];

function sideValue(value: unknown): string {
  if (typeof value === 'string') return value;
  if (value === undefined || value === null) return '';
  return typeof value === 'object' ? JSON.stringify(value) : String(value);
}

function parseSide(value: unknown): DiagSide | null {
  if (!isRecord(value)) return null;
  return { label: text(value.label), source: text(value.source), value: sideValue(value.value) };
}

// Invariant: an unknown or missing status is unverified, never pass.
function statusOf(value: unknown): DiagStatus {
  const lower = text(value).toLowerCase();
  return (DIAG_STATUSES as readonly string[]).includes(lower) ? lower as DiagStatus : 'unverified';
}

const MALFORMED: DiagResult = {
  id: '(no id)', title: '(no id)', status: 'unverified', evidence: [], left: null, right: null, diffs: [],
  detail: 'malformed row', durationMs: 0, outputs: [],
};

export function parseDiagResults(value: unknown): DiagResult[] {
  if (!Array.isArray(value)) return [];
  const results: DiagResult[] = [];
  for (const row of value) {
    const id = isRecord(row) ? text(row.id) : '';
    const title = isRecord(row) ? text(row.title) : '';
    if (!isRecord(row) || (!id && !title)) {
      results.push({ ...MALFORMED });
      continue;
    }
    const detail = text(row.detail);
    // Invariant: observed-in-game is trusted only when the result carries the observation as detail.
    const evidence = strings(row.evidence).filter((kind): kind is EvidenceKind =>
      (EVIDENCE_KINDS as readonly string[]).includes(kind) && (kind !== 'observed-in-game' || detail.trim().length > 0));
    results.push({
      id: id || title, title: title || id, status: statusOf(row.status), evidence,
      left: parseSide(row.left), right: parseSide(row.right), diffs: strings(row.diffs),
      detail, durationMs: num(row.durationMs), outputs: strings(row.outputs),
    });
  }
  return results;
}

// Invariant: empty evidence counts as structural only, so a pass with no declared proof is never shown as proof.
export const isStructuralOnly = (result: DiagResult): boolean =>
  result.status === 'pass' && result.evidence.every(kind => kind === 'structural');

export function summarise(results: DiagResult[]): DiagSummary {
  const summary: DiagSummary = { total: results.length, pass: 0, fail: 0, skip: 0, unsupported: 0, unverified: 0, error: 0, structuralOnly: 0 };
  for (const result of results) {
    summary[result.status] += 1;
    if (isStructuralOnly(result)) summary.structuralOnly += 1;
  }
  return summary;
}

export const VERDICT_LABELS: Record<RunVerdict, string> = {
  fail: 'FAIL', cancelled: 'CANCELLED', unverified: 'UNVERIFIED', incomplete: 'INCOMPLETE', structural: 'PASS (structural only)',
  pass: 'PASS', empty: 'NO RESULTS',
};

export const VERDICT_ORDER: readonly RunVerdict[] = ['fail', 'cancelled', 'unverified', 'incomplete', 'structural', 'pass', 'empty'];

// Invariant: only a run with a pass with real evidence and nothing failed, cancelled, unverified, skipped or unsupported is a pass.
export function runVerdict(run: Pick<RunRecord, 'results' | 'error' | 'cancelled'>): RunVerdict {
  const sum = summarise(run.results);
  if (run.error || sum.fail > 0 || sum.error > 0) return 'fail';
  if (run.cancelled) return 'cancelled';
  if (sum.unverified > 0) return 'unverified';
  if (sum.skip > 0 || sum.unsupported > 0) return 'incomplete';
  if (sum.pass === 0) return 'empty';
  return sum.structuralOnly === sum.pass ? 'structural' : 'pass';
}

// Invariant: anything not run keeps an aggregate from reading as pass.
export function aggregateVerdict(runs: RunRecord[], notRun: NotRun[]): RunVerdict {
  if (runs.length === 0) return 'empty';
  const worst = VERDICT_ORDER.find(verdict => verdict !== 'empty' && runs.some(run => runVerdict(run) === verdict)) ?? 'empty';
  const green = worst === 'pass' || worst === 'structural';
  return green && (notRun.length > 0 || runs.some(run => runVerdict(run) === 'empty')) ? 'incomplete' : worst;
}

export const flattenRuns = (runs: RunRecord[]): DiagResult[] =>
  runs.flatMap(run => run.results.map(result => ({ ...result, id: `${run.toolId}:${result.id}`, title: `[${run.title}] ${result.title}` })));

export function summariseRuns(runs: RunRecord[], notRun: NotRun[] = []): SessionSummary {
  const verdicts: Record<RunVerdict, number> = { fail: 0, cancelled: 0, unverified: 0, incomplete: 0, structural: 0, pass: 0, empty: 0 };
  for (const run of runs) verdicts[runVerdict(run)] += 1;
  return { ...summarise(flattenRuns(runs)), runs: runs.length, notRun: notRun.length, verdicts };
}

export function sessionReport(context: DiagContext | null, runs: RunRecord[], notRun: NotRun[], label: string): DiagReportInput {
  const ran = new Set(runs.map(run => run.toolId));
  const unrun = [...new Map(notRun.filter(item => !ran.has(item.toolId)).map(item => [item.toolId, item] as const)).values()];
  return {
    context, results: flattenRuns(runs), toolId: label, runs, notRun: unrun,
    error: runs.filter(run => run.error).map(run => `${run.title}: ${run.error}`).join('; '),
    cancelled: runs.some(run => run.cancelled),
  };
}

const SECRET_WORDS = 'token|secret|password|passwd|cookie|authorization|api[_-]?key|session[_-]?id|private[_-]?key|credential';
const SECRET_KEY = new RegExp(SECRET_WORDS, 'i');
const KEY_VALUE = new RegExp(`((?:${SECRET_WORDS})[\\w-]*["']?\\s*[:=]\\s*)("[^"]*"|'[^']*'|[^\\s,;&}]+)`, 'gi');
const WINDOWS_PROFILE = /[A-Za-z]:[\\/]+Users[\\/]+[^\\/"'<>|,;\r\n]+/gi;
const UNC_PROFILE = /\\\\[^\\\s"'<>|]+\\[^\\\s"'<>|]+\\(?:Users|home)\\[^\\/"'<>|,;\r\n]+/gi;
const POSIX_PROFILE = /(^|[\s"'=([<,;:/])\/(?:Users|home)\/[^/\s"']+/g;
const BEARER = /\bBearer\s+[A-Za-z0-9._~+/=-]+/gi;
const HEADER_LINE = /\b((?:Set-|Proxy-)?(?:Cookie|Authorization)["']?\s*:\s*)[^\r\n]*/gi;
const JWT = /\beyJ[\w-]{8,}\.[\w-]{8,}\.[\w-]*/g;
const URL_TOKEN = /([?&#]t=)[^&\s#"']+/gi;
const REDACTED = '[redacted]';
const PROFILE = '%USERPROFILE%';

export function redactText(value: string): string {
  return value
    .replace(UNC_PROFILE, PROFILE)
    .replace(WINDOWS_PROFILE, PROFILE)
    .replace(POSIX_PROFILE, `$1${PROFILE}`)
    .replace(BEARER, `Bearer ${REDACTED}`)
    .replace(HEADER_LINE, `$1${REDACTED}`)
    .replace(JWT, REDACTED)
    .replace(URL_TOKEN, `$1${REDACTED}`)
    .replace(KEY_VALUE, `$1${REDACTED}`);
}

export const redactSetting = (key: string, value: string): string =>
  SECRET_KEY.test(key) ? REDACTED : redactText(value);

const SECTION_TITLES: Record<DiagStatus, string> = {
  fail: 'FAILED', error: 'ERRORS', unverified: 'UNVERIFIED', pass: 'PASSED',
  skip: 'SKIPPED (not passed)', unsupported: 'UNSUPPORTED (not passed)',
};

function sideLine(name: string, side: DiagSide | null): string {
  return side ? `  ${name}: ${side.label} [${side.source}] = ${side.value}` : `  ${name}: (none)`;
}

function resultLines(result: DiagResult): string[] {
  const lines = [`[${result.status.toUpperCase()}] ${result.title} (${result.id})`,
    `  evidence: ${result.evidence.join(', ') || '(none)'}`];
  if (isStructuralOnly(result)) lines.push(`  ${STRUCTURAL_LABEL}`);
  if (result.left || result.right) lines.push(sideLine('left', result.left), sideLine('right', result.right));
  if (result.diffs.length > 0) lines.push('  diffs:', ...result.diffs.map(diff => `    - ${diff}`));
  if (result.detail) lines.push(`  detail: ${result.detail}`);
  if (result.outputs.length > 0) lines.push('  outputs:', ...result.outputs.map(path => `    - ${path}`));
  if (result.durationMs > 0) lines.push(`  duration: ${result.durationMs} ms`);
  return lines;
}

function contextLines(context: DiagContext | null): string[] {
  if (!context) return ['Context: (not loaded)'];
  const lines = [
    `FO4IDE version: ${context.versions.fo4ide || '(unknown)'}`,
    `Build commit: ${context.versions.commit || '(unknown)'}`,
    `Radicle ABI: ${context.versions.radicleAbi || '(unknown)'}`,
    `Game build: ${context.gameBuild || '(unknown)'}`,
    `Fallout 4 data dir: ${context.dataDir || '(unknown)'}`,
    `MO2 profile: ${context.mo2Profile || '(none)'}`,
    `Load order (${context.loadOrder.length}):`,
    ...context.loadOrder.map((name, index) => `  ${index + 1}. ${name}`),
    `Asset roots, low to high priority (${context.assetRootsLowToHigh.length}):`,
    ...context.assetRootsLowToHigh.map((root, index) => `  ${index + 1}. ${root}`),
    'Settings:',
  ];
  const keys = Object.keys(context.settings).sort();
  lines.push(...(keys.length > 0 ? keys.map(key => `  ${key}: ${redactSetting(key, context.settings[key])}`) : ['  (none)']));
  return lines;
}

export function diagReportText(input: DiagReportInput): string {
  const { context, results } = input;
  const summary = summarise(results);
  const lines = [
    'FO4IDE diagnostic report',
    `Generated: ${input.generatedAt ?? new Date().toISOString()}`,
    ...(input.toolId ? [`Checks run: ${input.toolId}`] : []),
    BANNER, '', '== Context ==', ...contextLines(context), '',
    '== Summary ==',
    `Passed: ${summary.pass} (structural only: ${summary.structuralOnly})`,
    `Failed: ${summary.fail}`, `Errors: ${summary.error}`, `Unverified: ${summary.unverified}`,
    `Skipped: ${summary.skip}`, `Unsupported: ${summary.unsupported}`,
  ];
  if (input.cancelled) lines.push(CANCELLED_NOTE);
  if (input.error) lines.push(`Run error: ${input.error}`);
  if (input.runs) {
    lines.push('', `== RUNS (${input.runs.length}) ==`,
      ...input.runs.map(run => `[${VERDICT_LABELS[runVerdict(run)]}] ${run.title} (${run.toolId}) run ${run.runId}`));
  }
  if (input.notRun && input.notRun.length > 0) {
    lines.push('', `== NOT RUN (${input.notRun.length}) ==`, 'Tools that did not run are not passes.',
      ...input.notRun.map(item => `- ${item.title} (${item.toolId}): ${item.reason}`));
  }
  for (const status of STATUS_ORDER) {
    const rows = results.filter(result => result.status === status);
    if (rows.length === 0) continue;
    lines.push('', `== ${SECTION_TITLES[status]} (${rows.length}) ==`);
    for (const row of rows) lines.push(...resultLines(row));
  }
  return redactText(lines.join('\n'));
}

export function diagReportJson(input: DiagReportInput): string {
  const grouped = Object.fromEntries(STATUS_ORDER.map(status => [status,
    input.results.filter(result => result.status === status)
      .map(result => ({ ...result, structuralOnly: isStructuralOnly(result) }))]));
  const report = {
    generatedAt: input.generatedAt ?? new Date().toISOString(),
    banner: BANNER,
    toolId: input.toolId ?? '',
    error: input.error ?? '',
    cancelled: input.cancelled === true,
    note: input.cancelled ? CANCELLED_NOTE : '',
    context: input.context,
    summary: summarise(input.results),
    runs: (input.runs ?? []).map(run => ({ runId: run.runId, toolId: run.toolId, title: run.title, verdict: runVerdict(run), error: run.error, cancelled: run.cancelled })),
    notRun: input.notRun ?? [],
    results: grouped,
  };
  return JSON.stringify(report, (key, value: unknown) => {
    if (SECRET_KEY.test(key) && value !== null && typeof value !== 'boolean') return REDACTED;
    return typeof value === 'string' ? redactText(value) : value;
  }, 2);
}
