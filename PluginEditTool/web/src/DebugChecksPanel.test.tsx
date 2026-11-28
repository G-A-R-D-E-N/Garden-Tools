import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { DebugChecksHost } from './backend';
import DebugChecksPanel from './DebugChecksPanel';
import { ageText, debugChecksEnabled, groupBoxes, parseList, parseStatus, parseTests, testStatus } from './debugChecksClient';

const LIST = {
  version: 1,
  results_root: 'D:\\rdf\\results',
  boxes: [
    { id: 'G16', title: 'Object BTO compare', kind: 'native', tags: ['lod'], tiers: ['fast', 'slow'],
      last: { status: 'FAIL', sha: 'abcdef1234', passed: 3, total: 4, skip: 1, seconds: 19,
              at: '2026-11-20T10:00:00Z', failing: ['LodObjectTileReal.Fixed'], error: '' }, running: null },
    { id: 'A2', title: 'Archive read', kind: 'native', tags: ['archive'], tiers: ['fast'],
      last: { status: 'PASS', sha: 'abcdef1234', passed: 9, total: 9, skip: 0, seconds: 4, at: '', failing: [], error: '' },
      running: null },
    { id: 'N1', title: 'Never run', kind: 'native', tags: [], tiers: ['fast'], last: { status: 'NONE' }, running: null },
  ],
};

const IDLE = { script: 'C:\\ci\\rdf.ps1', scriptFound: true, run: { active: false, kind: '', tests: [], results: [], log: [] } };

const RUNNING = {
  script: 'C:\\ci\\rdf.ps1', scriptFound: true,
  run: {
    active: true, kind: 'box', box: 'G16', tier: 'fast', phase: 'test', startedMs: Date.now() - 5000,
    tests: [
      { box: 'G16', name: 'LodObjectTileReal.Fixed', status: 'passed', seconds: 1.5 },
      { box: 'G16', name: 'LodObjectTileReal.Other', status: 'skipped', seconds: 0 },
    ],
    results: [], log: [],
  },
};

const FAILED = {
  script: 'C:\\ci\\rdf.ps1', scriptFound: true,
  run: {
    active: false, kind: 'box', box: 'G16', tier: 'fast', phase: 'test', startedMs: Date.now() - 9000, exitCode: 1,
    tests: [{ box: 'G16', name: 'LodObjectTileReal.Fixed', status: 'failed', seconds: 2 }],
    results: [{
      box: 'G16', status: 'FAIL', exitCode: 1, sha: 'abcdef1234', resultPath: 'D:\\rdf\\results\\G16\\x\\result.json', line: 'FAIL G16 abcdef1 3/4 19s',
      detail: { status: 'FAIL', failures: ['LodObjectTileReal.Fixed'], failedTests: [],
                excerpt: 'tile 4,8 differs from vanilla at vertex 17' },
    }],
    log: [],
  },
};

function installHost(initial: unknown = IDLE): { host: DebugChecksHost; setStatus: (next: unknown) => void } {
  let current = initial;
  const host: DebugChecksHost = {
    List: vi.fn(async () => JSON.stringify(LIST)),
    Results: vi.fn(async () => '{}'),
    Tests: vi.fn(async () => JSON.stringify({ version: 1, box: 'G16', tier: 'fast', available: true, reason: '', count: 2,
                                              tests: ['LodObjectTileReal.A', 'LodObjectTileReal.B'] })),
    Start: vi.fn(async () => '{"started":true}'),
    Status: vi.fn(async () => JSON.stringify(current)),
    Cancel: vi.fn(async () => '{"cancelled":true}'),
  };
  Object.assign(window, { chrome: { webview: { hostObjects: { debugChecks: host } } } });
  return { host, setStatus: next => { current = next; } };
}

afterEach(() => {
  cleanup();
  Reflect.deleteProperty(window, 'chrome');
});

describe('DebugChecksPanel', () => {
  it('groups the listed boxes by system and shows status, counts and the results folder', async () => {
    installHost();
    render(<DebugChecksPanel onClose={() => undefined} />);
    expect(await screen.findByText('Object BTO compare')).toBeTruthy();
    expect(screen.getByText('lod')).toBeTruthy();
    expect(screen.getByText('archive')).toBeTruthy();
    expect(screen.getByText('other')).toBeTruthy();
    expect(screen.getByText(/abcdef1\s+3\/4\s+1 skipped\s+19s/)).toBeTruthy();
    expect(screen.getByText('never run')).toBeTruthy();
    expect(screen.getByText('Results folder: D:\\rdf\\results')).toBeTruthy();
  });

  it('starts a box run with the tier chosen in the row, not a fixed one', async () => {
    const { host } = installHost();
    render(<DebugChecksPanel onClose={() => undefined} />);
    const row = (await screen.findByText('Object BTO compare')).closest('.dbg-box-row') as HTMLElement;
    fireEvent.change(within(row).getByRole('combobox'), { target: { value: 'slow' } });
    fireEvent.click(within(row).getByRole('button', { name: /Run/ }));
    await waitFor(() => expect(host.Start).toHaveBeenCalledWith('box', 'G16', 'slow', ''));
  });

  it('runs all and changed through their own kinds', async () => {
    const { host } = installHost();
    render(<DebugChecksPanel onClose={() => undefined} />);
    await screen.findByText('Object BTO compare');
    fireEvent.click(screen.getByRole('button', { name: 'Run all' }));
    await waitFor(() => expect(host.Start).toHaveBeenCalledWith('all', '', '', ''));
    fireEvent.click(screen.getByRole('button', { name: 'Run changed' }));
    await waitFor(() => expect(host.Start).toHaveBeenCalledWith('changed', '', '', ''));
  });

  it('sends the typed base ref with Run changed', async () => {
    const { host } = installHost();
    render(<DebugChecksPanel onClose={() => undefined} />);
    await screen.findByText('Object BTO compare');
    fireEvent.change(screen.getByPlaceholderText(/base/), { target: { value: ' origin/main ' } });
    fireEvent.click(screen.getByRole('button', { name: 'Run changed' }));
    await waitFor(() => expect(host.Start).toHaveBeenCalledWith('changed', '', '', '', 'origin/main'));
  });

  it('lists a tier\'s tests and runs one of them, or shows why the list is unavailable', async () => {
    const { host } = installHost();
    render(<DebugChecksPanel onClose={() => undefined} />);
    const row = (await screen.findByText('Object BTO compare')).closest('.dbg-box-row') as HTMLElement;
    fireEvent.click(within(row).getByTitle('List the tests of this tier'));
    await waitFor(() => expect(host.Tests).toHaveBeenCalledWith('G16', 'fast'));
    const test = (await screen.findByText('LodObjectTileReal.B')).closest('.dbg-test') as HTMLElement;
    fireEvent.click(within(test).getByTitle('Run only this test'));
    await waitFor(() => expect(host.Start).toHaveBeenCalledWith('box', 'G16', 'fast', 'LodObjectTileReal.B'));

    vi.mocked(host.Tests).mockResolvedValue(JSON.stringify({
      version: 1, box: 'G16', tier: 'fast', available: false, count: 0, tests: [],
      reason: 'tests unavailable until the first run has built the box',
    }));
    fireEvent.click(within(row).getByTitle('List the tests of this tier'));
    expect(await screen.findByText(/unavailable until the first run has built the box/)).toBeTruthy();
    expect(screen.queryByText('LodObjectTileReal.B')).toBeNull();
  });

  it('shows an interrupted box with its reason and an externally running box with its live phase', async () => {
    const { host } = installHost();
    vi.mocked(host.List).mockResolvedValue(JSON.stringify({
      version: 1, results_root: 'D:\\rdf\\results', boxes: [
        { id: 'X1', title: 'Dead run', kind: 'native', tags: ['a'], tiers: ['fast'], running: null,
          last: { status: 'INTERRUPTED', sha: 'abc1234', passed: 0, total: 0, skip: 0, seconds: 0,
                  at: '2026-11-20T13:57:01Z', failing: [], error: 'interrupted' } },
        { id: 'X2', title: 'Someone else', kind: 'native', tags: ['a'], tiers: ['fast'],
          last: { status: 'RUNNING', sha: 'abc1234', passed: 0, total: 0, skip: 0, seconds: 0,
                  at: '2026-11-20T13:57:01Z', failing: [], error: '' },
          running: { box: 'X2', phase: 'test', current: 'Lod.Slow', passed: 2, failed: 0, skipped: 1, total: 8 } },
      ],
    }));
    render(<DebugChecksPanel onClose={() => undefined} />);
    const dead = (await screen.findByText('Dead run')).closest('.dbg-box-row') as HTMLElement;
    expect(within(dead).getByText('INTERRUPTED').className).toContain('dbg-interrupted');
    expect(within(dead).getByText(/interrupted/, { selector: '.dbg-meta' })).toBeTruthy();
    const live = (await screen.findByText('Someone else')).closest('.dbg-box-row') as HTMLElement;
    expect(within(live).getByText('RUNNING').className).toContain('dbg-running');
    expect(within(live).getByText(/test\s+Lod\.Slow\s+3\/8/)).toBeTruthy();
  });

  it('shows the plan of a changed run, and says so when nothing was affected', async () => {
    const { setStatus } = installHost();
    render(<DebugChecksPanel onClose={() => undefined} />);
    await screen.findByText('Object BTO compare');
    setStatus({ ...IDLE, run: { ...IDLE.run, kind: 'changed', planSeen: true, plan: [], unknown: false, exitCode: 0,
      results: [{ box: '', status: 'PASS', exitCode: 0, sha: '', resultPath: '', line: 'No checks affected.', detail: null }] } });
    fireEvent.click(screen.getByRole('button', { name: 'Run changed' }));
    expect((await screen.findAllByText(/No checks affected\./)).length).toBe(2);
    setStatus({ ...IDLE, run: { ...IDLE.run, kind: 'changed', planSeen: true, plan: ['G16', 'G15'], unknown: true } });
    fireEvent.click(screen.getByRole('button', { name: 'Run changed' }));
    expect(await screen.findByText(/Plan: G16, G15 \(the change could not be mapped/)).toBeTruthy();
  });

  it('re-attaches to a run that is already active when the panel opens and blocks a second start', async () => {
    const { host } = installHost(RUNNING);
    render(<DebugChecksPanel onClose={() => undefined} pollMs={10} />);
    expect(await screen.findByText('LodObjectTileReal.Other')).toBeTruthy();
    expect(screen.getByText(/Running: box G16\/fast/)).toBeTruthy();
    expect((screen.getByRole('button', { name: 'Run all' }) as HTMLButtonElement).disabled).toBe(true);
    expect((screen.getByRole('button', { name: /Cancel/ }) as HTMLButtonElement).disabled).toBe(false);
    fireEvent.click(screen.getByRole('button', { name: /Cancel/ }));
    await waitFor(() => expect(host.Cancel).toHaveBeenCalledTimes(1));
  });

  it('keeps polling while a run is active and shows the finished run once it ends', async () => {
    const { host, setStatus } = installHost(RUNNING);
    render(<DebugChecksPanel onClose={() => undefined} pollMs={10} />);
    await screen.findByText('LodObjectTileReal.Other');
    setStatus(FAILED);
    expect(await screen.findByText('FAIL G16 abcdef1 3/4 19s')).toBeTruthy();
    expect(screen.getByText(/Last run: box G16\/fast/)).toBeTruthy();
    await waitFor(() => expect(vi.mocked(host.List).mock.calls.length).toBeGreaterThanOrEqual(2));
    await new Promise(resolve => setTimeout(resolve, 40));
    const calls = vi.mocked(host.Status).mock.calls.length;
    await new Promise(resolve => setTimeout(resolve, 60));
    expect(vi.mocked(host.Status).mock.calls.length).toBe(calls);
  });

  it('expands the failing tests and the failure excerpt of a failed box', async () => {
    installHost(FAILED);
    render(<DebugChecksPanel onClose={() => undefined} />);
    const row = (await screen.findByText('Object BTO compare')).closest('.dbg-box-row') as HTMLElement;
    expect(screen.queryByText(/differs from vanilla/)).toBeNull();
    fireEvent.click(within(row).getByTitle('Failing tests and the failure excerpt'));
    expect(await screen.findByText('tile 4,8 differs from vanilla at vertex 17')).toBeTruthy();
    expect(within(row.parentElement as HTMLElement).getAllByText('LodObjectTileReal.Fixed').length).toBeGreaterThan(0);
  });

  it('re-runs a single test of the run with that run\'s box and tier', async () => {
    const { host } = installHost(FAILED);
    render(<DebugChecksPanel onClose={() => undefined} />);
    const row = (await screen.findAllByText('LodObjectTileReal.Fixed'))
      .map(node => node.closest('.dbg-test')).find(Boolean) as HTMLElement;
    fireEvent.click(within(row).getByTitle('Re-run only this test'));
    await waitFor(() => expect(host.Start).toHaveBeenCalledWith('box', 'G16', 'fast', 'LodObjectTileReal.Fixed'));
  });

  it('is disabled and says why when the host did not register the debug target', async () => {
    Object.assign(window, { chrome: { webview: { hostObjects: { debugChecks: {
      Status: vi.fn(async () => { throw new Error('debugChecks.Status: UnknownTarget'); }),
    } } } } });
    render(<DebugChecksPanel onClose={() => undefined} />);
    expect(await screen.findByText(/FO4IDE_DEBUG_CHECKS=1/)).toBeTruthy();
    expect((screen.getByRole('button', { name: 'Run all' }) as HTMLButtonElement).disabled).toBe(true);
    expect((screen.getByRole('button', { name: 'Run changed' }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('shows the host error when rdf.ps1 is missing', async () => {
    const { host } = installHost({ ...IDLE, scriptFound: false });
    vi.mocked(host.List).mockRejectedValue(new Error('debugChecks.List: RdfMissing: rdf.ps1 was not found'));
    render(<DebugChecksPanel onClose={() => undefined} />);
    expect(await screen.findByText(/rdf.ps1 not found at C:\\ci\\rdf.ps1/)).toBeTruthy();
    expect(screen.getByText(/RdfMissing/)).toBeTruthy();
  });

  it('keeps a failed start visible after a successful list refresh', async () => {
    const { host } = installHost();
    vi.mocked(host.Start).mockRejectedValue(new Error('debugChecks.Start: Busy: a run is already active'));
    render(<DebugChecksPanel onClose={() => undefined} />);
    await screen.findByText('Archive read');
    fireEvent.click(screen.getByRole('button', { name: 'Run all' }));
    expect(await screen.findByText(/Busy: a run is already active/)).toBeTruthy();
    const lists = vi.mocked(host.List).mock.calls.length;
    fireEvent.click(screen.getByRole('button', { name: /Refresh/ }));
    await waitFor(() => expect(vi.mocked(host.List).mock.calls.length).toBe(lists + 1));
    await screen.findByText('Archive read');
    expect(screen.getByText(/Busy: a run is already active/)).toBeTruthy();
  });

  it('starts polling when Start worked but the first Status read failed', async () => {
    const { host } = installHost();
    render(<DebugChecksPanel onClose={() => undefined} pollMs={10} />);
    await screen.findByText('Archive read');
    vi.mocked(host.Status).mockRejectedValueOnce(new Error('debugChecks.Status: transient'));
    vi.mocked(host.Status).mockResolvedValue(JSON.stringify(RUNNING));
    fireEvent.click(screen.getByRole('button', { name: 'Run all' }));
    expect(await screen.findByText('LodObjectTileReal.Other')).toBeTruthy();
    expect(vi.mocked(host.Start)).toHaveBeenCalledTimes(1);
  });

  it('exposes a labelled dialog and named controls for assistive technology', async () => {
    installHost();
    render(<DebugChecksPanel onClose={() => undefined} />);
    const dialog = await screen.findByRole('dialog', { name: 'Debug checks' });
    await screen.findByText('Archive read');
    expect(within(dialog).getByRole('combobox', { name: 'Tier of G16' })).toBeTruthy();
    expect(within(dialog).getByRole('button', { name: 'List tests of G16' })).toBeTruthy();
    expect(within(dialog).getByRole('button', { name: 'Details of G16' })).toBeTruthy();
    expect(within(dialog).getByRole('button', { name: 'Close debug checks' })).toBeTruthy();
  });
});

describe('debugChecksClient', () => {
  it('treats a missing host, a failing Status and a working Status as off, off and on', async () => {
    expect(await debugChecksEnabled()).toBe(false);
    Object.assign(window, { chrome: { webview: { hostObjects: { debugChecks: {
      Status: vi.fn(async () => { throw new Error('UnknownTarget'); }),
    } } } } });
    expect(await debugChecksEnabled()).toBe(false);
    installHost();
    expect(await debugChecksEnabled()).toBe(true);
  });

  it('maps rdf test statuses and reads the tests document, including the unavailable shape', () => {
    expect(['passed', 'failed', 'skipped', 'running', ''].map(testStatus)).toEqual(['PASS', 'FAIL', 'SKIP', 'RUNNING', '']);
    expect(parseTests(JSON.stringify({ version: 1, box: 'G16', tier: 'fast', available: true, reason: '', count: 1, tests: ['A.B'] })))
      .toEqual({ box: 'G16', tier: 'fast', available: true, reason: '', tests: ['A.B'] });
    expect(parseTests('{"available":false,"reason":"no ctest","tests":[]}').available).toBe(false);
    expect(parseTests('{}').available).toBe(false);
    expect(parseStatus(JSON.stringify({ run: { tests: [{ name: 'A.B', status: 'failed' }] } })).run.tests[0].status).toBe('FAIL');
  });

  it('reads an unknown or missing status as NONE and drops boxes without an id', () => {
    const list = parseList(JSON.stringify({ boxes: [
      { id: 'X1', last: { status: 'weird' } }, { id: 'X2' }, { title: 'no id' },
    ] }));
    expect(list.boxes.map(box => [box.id, box.last.status])).toEqual([['X1', 'NONE'], ['X2', 'NONE']]);
    expect(() => parseList('not json')).toThrow(/did not return JSON/);
    expect(parseStatus('{}').run.active).toBe(false);
  });

  it('groups by the first tag, puts untagged boxes under other and ages timestamps', () => {
    const list = parseList(JSON.stringify(LIST));
    expect(groupBoxes(list.boxes).map(([tag]) => tag)).toEqual(['archive', 'lod', 'other']);
    const now = Date.parse('2026-11-20T10:05:00Z');
    expect(ageText('2026-11-20T10:00:00Z', now)).toBe('5m ago');
    expect(ageText('garbled', now)).toBe('garbled');
  });
});
