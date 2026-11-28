import { getDebugChecks } from './backend';

export type CheckStatus = 'PASS' | 'FAIL' | 'RUNNING' | 'INTERRUPTED' | 'NONE';

export interface CheckLast {
  status: CheckStatus;
  sha: string;
  passed: number;
  total: number;
  skip: number;
  seconds: number;
  at: string;
  failing: string[];
  error: string;
}

export interface CheckRunning {
  phase: string;
  current: string;
  passed: number;
  failed: number;
  skipped: number;
  total: number;
}

export interface CheckBox {
  id: string;
  title: string;
  kind: string;
  tags: string[];
  tiers: string[];
  last: CheckLast;
  running: CheckRunning | null;
}

export interface TestList {
  box: string;
  tier: string;
  available: boolean;
  reason: string;
  tests: string[];
}

export interface CheckList {
  resultsRoot: string;
  boxes: CheckBox[];
}

export interface RunTest {
  box: string;
  name: string;
  status: string;
  seconds: number;
}

export interface RunDetail {
  status: string;
  failures: string[];
  failedTests: RunTest[];
  excerpt: string;
}

export interface RunResult {
  box: string;
  status: string;
  exitCode: number;
  sha: string;
  resultPath: string;
  line: string;
  detail: RunDetail | null;
}

export interface RunStatus {
  active: boolean;
  cancelled: boolean;
  kind: string;
  box: string;
  tier: string;
  test: string;
  phase: string;
  error: string;
  plan: string[];
  planSeen: boolean;
  unknown: boolean;
  startedMs: number;
  exitCode: number;
  tests: RunTest[];
  results: RunResult[];
  log: string[];
}

export interface DebugStatus {
  script: string;
  scriptFound: boolean;
  run: RunStatus;
}

export type RunKind = 'box' | 'all' | 'changed';

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);
const text = (value: unknown): string => (typeof value === 'string' ? value : '');
const num = (value: unknown): number => (typeof value === 'number' && Number.isFinite(value) ? value : 0);
const strings = (value: unknown): string[] =>
  Array.isArray(value) ? value.map(item => (typeof item === 'string' ? item : JSON.stringify(item))) : [];
const records = (value: unknown): Record<string, unknown>[] => (Array.isArray(value) ? value.filter(isRecord) : []);

const STATUSES: readonly string[] = ['PASS', 'FAIL', 'RUNNING', 'INTERRUPTED', 'NONE'];
const TEST_STATUSES: Record<string, string> = { passed: 'PASS', failed: 'FAIL', skipped: 'SKIP' };

/** Invariant: an unknown or missing status reads as NONE, so a box never shows a result rdf did not report. */
function statusOf(value: unknown): CheckStatus {
  const upper = text(value).toUpperCase();
  return (STATUSES.includes(upper) ? upper : 'NONE') as CheckStatus;
}

function parseJson(raw: string, what: string): Record<string, unknown> {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch {
    throw new Error(`${what}: the host did not return JSON`);
  }
  if (!isRecord(parsed)) throw new Error(`${what}: expected an object`);
  return parsed;
}

// Invariant: rdf test statuses arrive lower case and the badges key on PASS, FAIL, SKIP.
export function testStatus(value: unknown): string {
  const raw = text(value).toLowerCase();
  return TEST_STATUSES[raw] ?? raw.toUpperCase();
}

function parseTest(row: Record<string, unknown>): RunTest {
  return { box: text(row.box), name: text(row.name), status: testStatus(row.status), seconds: num(row.seconds) };
}

function parseRunning(value: unknown): CheckRunning | null {
  if (!isRecord(value)) return null;
  return {
    phase: text(value.phase), current: text(value.current), passed: num(value.passed), failed: num(value.failed),
    skipped: num(value.skipped), total: num(value.total),
  };
}

export function parseList(raw: string): CheckList {
  const parsed = parseJson(raw, 'Debug checks list');
  const boxes = records(parsed.boxes).map(row => {
    const last = isRecord(row.last) ? row.last : {};
    return {
      id: text(row.id),
      title: text(row.title),
      kind: text(row.kind),
      tags: strings(row.tags),
      tiers: strings(row.tiers),
      last: {
        status: statusOf(last.status),
        sha: text(last.sha),
        passed: num(last.passed),
        total: num(last.total),
        skip: num(last.skip),
        seconds: num(last.seconds),
        at: text(last.at),
        failing: strings(last.failing),
        error: text(last.error),
      },
      running: parseRunning(row.running),
    };
  }).filter(box => box.id.length > 0);
  return { resultsRoot: text(parsed.results_root), boxes };
}

export function parseTests(raw: string): TestList {
  const parsed = parseJson(raw, 'Debug checks tests');
  return {
    box: text(parsed.box),
    tier: text(parsed.tier),
    available: parsed.available === true,
    reason: text(parsed.reason),
    tests: strings(parsed.tests),
  };
}

export function parseStatus(raw: string): DebugStatus {
  const parsed = parseJson(raw, 'Debug checks status');
  const run = isRecord(parsed.run) ? parsed.run : {};
  return {
    script: text(parsed.script),
    scriptFound: parsed.scriptFound === true,
    run: {
      active: run.active === true,
      cancelled: run.cancelled === true,
      kind: text(run.kind),
      box: text(run.box),
      tier: text(run.tier),
      test: text(run.test),
      phase: text(run.phase),
      error: text(run.error),
      plan: strings(run.plan),
      planSeen: run.planSeen === true,
      unknown: run.unknown === true,
      startedMs: num(run.startedMs),
      exitCode: typeof run.exitCode === 'number' ? run.exitCode : -1,
      tests: records(run.tests).map(parseTest),
      results: records(run.results).map(row => {
        const detail = isRecord(row.detail) ? row.detail : null;
        return {
          box: text(row.box),
          status: text(row.status).toUpperCase(),
          exitCode: typeof row.exitCode === 'number' ? row.exitCode : -1,
          sha: text(row.sha),
          resultPath: text(row.resultPath),
          line: text(row.line),
          detail: detail && {
            status: text(detail.status),
            failures: strings(detail.failures),
            failedTests: records(detail.failedTests).map(parseTest),
            excerpt: text(detail.excerpt),
          },
        };
      }),
      log: strings(run.log),
    },
  };
}

export function debugChecksHost() {
  const host = getDebugChecks();
  if (!host) throw new Error('Debug checks are off: start FO4IDE with FO4IDE_DEBUG_CHECKS=1');
  return host;
}

/** Invariant: the host object exists only when FO4IDE_DEBUG_CHECKS=1, so any failure of Status means off. */
export async function debugChecksEnabled(): Promise<boolean> {
  try {
    const host = getDebugChecks();
    if (!host) return false;
    await host.Status();
    return true;
  } catch {
    return false;
  }
}

export const loadList = async () => parseList(await debugChecksHost().List());
export const loadStatus = async () => parseStatus(await debugChecksHost().Status());
export const loadTests = async (box: string, tier: string) => parseTests(await debugChecksHost().Tests(box, tier));
export const startRun = (kind: RunKind, box = '', tier = '', test = '', base = '') =>
  base ? debugChecksHost().Start(kind, box, tier, test, base) : debugChecksHost().Start(kind, box, tier, test);
export const cancelRun = () => debugChecksHost().Cancel();

export function ageText(at: string, now: number): string {
  const when = Date.parse(at);
  if (Number.isNaN(when)) return at;
  const seconds = Math.max(0, Math.round((now - when) / 1000));
  if (seconds < 90) return `${seconds}s ago`;
  if (seconds < 5400) return `${Math.round(seconds / 60)}m ago`;
  if (seconds < 172800) return `${Math.round(seconds / 3600)}h ago`;
  return `${Math.round(seconds / 86400)}d ago`;
}

export function groupBoxes(boxes: CheckBox[]): [string, CheckBox[]][] {
  const groups = new Map<string, CheckBox[]>();
  for (const box of boxes) {
    const key = box.tags[0] ?? 'other';
    groups.set(key, [...(groups.get(key) ?? []), box]);
  }
  return [...groups.entries()].sort(([a], [b]) => a.localeCompare(b));
}

export function defaultTier(box: CheckBox): string {
  return box.tiers.includes('fast') ? 'fast' : box.tiers[0] ?? '';
}
