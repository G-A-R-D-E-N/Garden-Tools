import { describe, expect, it, vi } from 'vitest';
import type { ToolGroup, ToolInfo } from './debugToolsClient';
import { filterGroups, isDryRun, planSuite, runQueue } from './debugToolsSuite';
import {
  aggregateVerdict, diagReportJson, diagReportText, flattenRuns, runVerdict, sessionReport, summariseRuns,
  type DiagResult, type DiagStatus, type RunRecord,
} from './diagnosticModel';

const result = (status: DiagStatus, over: Partial<DiagResult> = {}): DiagResult => ({
  id: `r-${status}`, title: `T ${status}`, status, evidence: ['real-game-data'], left: null, right: null, diffs: [], detail: '',
  durationMs: 0, outputs: [], ...over,
});

const run = (toolId: string, results: DiagResult[], over: Partial<RunRecord> = {}): RunRecord => ({
  runId: toolId, toolId, title: `Tool ${toolId}`, groupId: 'g', at: '2026-01-01T00:00:00.000Z', suiteId: '', results, error: '',
  cancelled: false, ...over,
});

const tool = (id: string, over: Partial<ToolInfo> = {}): ToolInfo => ({
  id, title: `Tool ${id}`, description: `does ${id}`, readOnly: true, writes: false, needsOutputDir: false, expensive: false,
  cancellable: false, available: true, unavailableReason: '', evidence: ['structural'], params: [], ...over,
});

describe('runVerdict and aggregation (an unrun, skipped, unsupported or unverified part must never read as pass)', () => {
  it('is pass only for passes and nothing else', () => {
    expect(runVerdict(run('a', [result('pass')]))).toBe('pass');
    expect(runVerdict(run('a', [result('pass'), result('skip')]))).toBe('incomplete');
    expect(runVerdict(run('a', [result('pass'), result('unsupported')]))).toBe('incomplete');
    expect(runVerdict(run('a', [result('pass'), result('unverified')]))).toBe('unverified');
    expect(runVerdict(run('a', [result('skip')]))).toBe('incomplete');
    expect(runVerdict(run('a', []))).toBe('empty');
    expect(runVerdict(run('a', [result('pass'), result('fail')]))).toBe('fail');
    expect(runVerdict(run('a', [result('pass')], { error: 'boom' }))).toBe('fail');
    expect(runVerdict(run('a', [result('pass')], { cancelled: true }))).toBe('cancelled');
    expect(runVerdict(run('a', [result('error')]))).toBe('fail');
  });

  it('never makes an aggregate pass while something did not run', () => {
    const passing = [run('a', [result('pass')]), run('b', [result('pass')])];
    expect(aggregateVerdict(passing, [])).toBe('pass');
    expect(aggregateVerdict(passing, [{ toolId: 'c', title: 'C', reason: 'writes' }])).toBe('incomplete');
    expect(aggregateVerdict([], [{ toolId: 'c', title: 'C', reason: 'writes' }])).toBe('empty');
    expect(aggregateVerdict([...passing, run('c', [result('fail')])], [])).toBe('fail');
    expect(aggregateVerdict([...passing, run('c', [result('unverified')])], [])).toBe('unverified');
    expect(aggregateVerdict([...passing, run('c', [])], [])).toBe('incomplete');
  });

  it('labels a pass backed only by structural evidence as structural, not pass, without changing the counts', () => {
    const structural = (id: string) => result('pass', { id, evidence: ['structural'] });
    expect(runVerdict(run('a', [structural('x')]))).toBe('structural');
    expect(runVerdict(run('a', [structural('x'), result('pass')]))).toBe('pass');
    expect(runVerdict(run('a', [structural('x'), result('skip')]))).toBe('incomplete');
    expect(runVerdict(run('a', [structural('x')], { cancelled: true }))).toBe('cancelled');
    const only = [run('a', [structural('x')])];
    expect(aggregateVerdict(only, [])).toBe('structural');
    expect(aggregateVerdict(only, [{ toolId: 'c', title: 'C', reason: 'writes' }])).toBe('incomplete');
    expect(aggregateVerdict([...only, run('b', [result('pass')])], [])).toBe('structural');
    const sum = summariseRuns(only);
    expect(sum).toMatchObject({ pass: 1, structuralOnly: 1 });
    expect(sum.verdicts).toMatchObject({ structural: 1, pass: 0 });
  });

  it('counts skip, unsupported, unverified and not-run apart from pass', () => {
    const runs = [run('a', [result('pass'), result('skip'), result('unsupported'), result('unverified')]), run('b', [result('fail')])];
    const sum = summariseRuns(runs, [{ toolId: 'c', title: 'C', reason: 'x' }, { toolId: 'd', title: 'D', reason: 'y' }]);
    expect(sum).toMatchObject({ runs: 2, notRun: 2, pass: 1, skip: 1, unsupported: 1, unverified: 1, fail: 1, total: 5 });
    expect(sum.verdicts).toMatchObject({ unverified: 1, fail: 1, pass: 0 });
  });
});

describe('session report', () => {
  const runs = [
    run('a', [result('pass', { id: 'same' })]),
    run('b', [result('fail', { id: 'same', detail: 'Authorization: Bearer abc-secret-1' })], { cancelled: true }),
  ];
  const notRun = [{ toolId: 'w', title: 'Writer', reason: 'writes or is not read-only: a suite never runs it' }];

  it('keeps tools apart, lists the not-run tools as not passes and notes a cancelled run as partial', () => {
    const input = sessionReport(null, runs, notRun, 'session');
    expect(flattenRuns(runs).map(row => row.id)).toEqual(['a:same', 'b:same']);
    const text = diagReportText(input);
    expect(text).toContain('== RUNS (2) ==');
    expect(text).toContain('[PASS] Tool a (a) run a');
    expect(text).toContain('== NOT RUN (1) ==');
    expect(text).toContain('Tools that did not run are not passes.');
    expect(text).toContain('Writer (w): writes or is not read-only');
    expect(text).toContain('Run was cancelled: results are partial');
    expect(text).toContain('Passed: 1 (structural only: 0)');
    expect(text).not.toContain('abc-secret-1');
    const json = JSON.parse(diagReportJson(input));
    expect(json.runs).toHaveLength(2);
    expect(json.notRun).toEqual(notRun);
    expect(json.summary.pass).toBe(1);
    expect(diagReportJson(input)).not.toContain('abc-secret-1');
  });

  it('lists a tool once as not run and not at all when it ran on its own', () => {
    const repeated = [
      { toolId: 'w', title: 'Writer', reason: 'first' }, { toolId: 'w', title: 'Writer', reason: 'second' },
      { toolId: 'a', title: 'Tool a', reason: 'ran by hand later' },
    ];
    expect(sessionReport(null, runs, repeated, 'session').notRun).toEqual([{ toolId: 'w', title: 'Writer', reason: 'second' }]);
  });

  it('leaves the single-run report without runs or not-run sections', () => {
    const text = diagReportText({ context: null, results: [result('pass')] });
    expect(text).not.toContain('== RUNS');
    expect(text).not.toContain('== NOT RUN');
  });
});

describe('filterGroups', () => {
  const groups: ToolGroup[] = [
    { id: 'plugin', title: 'Plugin checks', tools: [tool('plugin.compare'), tool('plugin.roundtrip', { description: 'round trip a file' })] },
    { id: 'lod', title: 'LOD', tools: [tool('lod.report')] },
  ];

  it('matches id, title, description and group title, case-insensitively, and drops empty groups', () => {
    expect(filterGroups(groups, '')).toBe(groups);
    expect(filterGroups(groups, 'COMPARE').map(group => group.id)).toEqual(['plugin']);
    expect(filterGroups(groups, 'COMPARE')[0].tools.map(entry => entry.id)).toEqual(['plugin.compare']);
    expect(filterGroups(groups, 'round trip')[0].tools.map(entry => entry.id)).toEqual(['plugin.roundtrip']);
    expect(filterGroups(groups, 'plugin checks')[0].tools).toHaveLength(2);
    expect(filterGroups(groups, 'nothing-matches')).toEqual([]);
  });

  it('labels a dry-run tool only by its id or title', () => {
    expect(isDryRun(tool('lod.dryRunGenerate'))).toBe(true);
    expect(isDryRun(tool('x', { title: 'Previs generate dry run' }))).toBe(true);
    expect(isDryRun(tool('lod.report'))).toBe(false);
  });
});

describe('planSuite (a suite must never run a tool that writes, needs a folder, costs a lot unasked or cannot run)', () => {
  const none = () => '';
  const tools = [
    tool('ok'), tool('writes', { readOnly: false, writes: true }), tool('mixed', { readOnly: true, writes: true }),
    tool('folder', { needsOutputDir: true }), tool('slow', { expensive: true }),
    tool('off', { available: false, unavailableReason: 'needs the Creation Kit' }),
    tool('param', { params: [{ name: 'plugin', label: 'Plugin', kind: 'plugin', required: true }] }),
  ];

  it('runs only the plain read-only tools by default and says why each other one did not run', () => {
    const plan = planSuite(tools, tool2 => (tool2.id === 'off' ? 'needs the Creation Kit' : tool2.id === 'param' ? 'required: Plugin' : ''), false);
    expect(plan.tools.map(entry => entry.id)).toEqual(['ok']);
    expect(plan.notRun.map(item => item.toolId)).toEqual(['writes', 'mixed', 'folder', 'slow', 'off', 'param']);
    expect(plan.notRun.find(item => item.toolId === 'off')?.reason).toBe('needs the Creation Kit');
    expect(plan.notRun.find(item => item.toolId === 'slow')?.reason).toMatch(/expensive/);
  });

  it('includes an expensive read-only tool when asked, but never a writing or folder tool', () => {
    const plan = planSuite(tools, none, true);
    expect(plan.tools.map(entry => entry.id)).toEqual(['ok', 'slow', 'off', 'param']);
    expect(plan.tools.some(entry => entry.writes || entry.needsOutputDir)).toBe(false);
    expect(plan.notRun.map(item => item.toolId)).toEqual(['writes', 'mixed', 'folder']);
  });
});

describe('runQueue (cancel stops the queue; whatever did not run is not run, never pass)', () => {
  const three = [tool('a'), tool('b'), tool('c')];
  const finished = (id: string, over: Partial<RunRecord> = {}) => run(id, [result('pass')], over);

  it('runs the tools in order, one at a time', async () => {
    const order: string[] = [];
    let open = 0;
    const outcome = await runQueue(three, {
      stopped: () => false,
      start: async entry => {
        open += 1;
        expect(open).toBe(1);
        order.push(entry.id);
        await Promise.resolve();
        open -= 1;
        return finished(entry.id);
      },
    });
    expect(order).toEqual(['a', 'b', 'c']);
    expect(outcome.records).toHaveLength(3);
    expect(outcome.notRun).toEqual([]);
  });

  it('stops before the next start once cancelled and reports the rest as not run', async () => {
    let stop = false;
    const start = vi.fn(async (entry: ToolInfo) => { stop = true; return finished(entry.id); });
    const outcome = await runQueue(three, { stopped: () => stop, start });
    expect(start).toHaveBeenCalledTimes(1);
    expect(outcome.records.map(record => record.toolId)).toEqual(['a']);
    expect(outcome.notRun.map(item => item.toolId)).toEqual(['b', 'c']);
    expect(aggregateVerdict(outcome.records, outcome.notRun)).toBe('incomplete');
    expect(outcome.stopped).toBe(true);
  });

  it('is not stopped when the stop lands during the last tool, so nothing was left unrun', async () => {
    let stop = false;
    const start = vi.fn(async (entry: ToolInfo) => { if (entry.id === 'c') stop = true; return finished(entry.id); });
    const outcome = await runQueue(three, { stopped: () => stop, start });
    expect(start).toHaveBeenCalledTimes(3);
    expect(outcome.notRun).toEqual([]);
    expect(outcome.stopped).toBe(false);
  });

  it('stops after a run the host reports as cancelled', async () => {
    const start = vi.fn(async (entry: ToolInfo) => finished(entry.id, { cancelled: true }));
    const outcome = await runQueue(three, { stopped: () => false, start });
    expect(start).toHaveBeenCalledTimes(1);
    expect(outcome.notRun.map(item => item.toolId)).toEqual(['b', 'c']);
    expect(outcome.stopped).toBe(true);
    expect(aggregateVerdict(outcome.records, outcome.notRun)).toBe('cancelled');
  });

  it('is not stopped when the last tool is the cancelled one', async () => {
    const start = vi.fn(async (entry: ToolInfo) => finished(entry.id, { cancelled: entry.id === 'c' }));
    const outcome = await runQueue(three, { stopped: () => false, start });
    expect(outcome.notRun).toEqual([]);
    expect(outcome.stopped).toBe(false);
  });

  it('records a refused Start as not run and keeps going, and stops when a run never reports back', async () => {
    const start = vi.fn(async (entry: ToolInfo) => {
      if (entry.id === 'a') throw new Error('debugTools.Start: DebugToolsBusy: another run is active');
      return entry.id === 'c' ? null : finished(entry.id);
    });
    const outcome = await runQueue(three, { stopped: () => false, start });
    expect(outcome.records.map(record => record.toolId)).toEqual(['b']);
    expect(outcome.notRun.map(item => item.toolId)).toEqual(['a', 'c']);
    expect(outcome.notRun[0].reason).toMatch(/DebugToolsBusy/);
    expect(outcome.stopped).toBe(false);
  });
});
