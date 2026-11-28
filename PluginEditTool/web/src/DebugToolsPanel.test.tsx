import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { DebugToolsHost } from './backend';
import DebugToolsPanel from './DebugToolsPanel';
import { parseRunId, parseToolList, parseToolStatus } from './debugToolsClient';

const DATA_DIR = 'C:\\Games\\Fallout 4\\Data';

const CONTEXT = {
  versions: { fo4ide: '1.2.3', radicleAbi: '7', commit: 'abc1234' }, dataDir: DATA_DIR, mo2Profile: 'Default',
  loadOrder: ['Fallout4.esm', 'Mod.esp'], settings: { theme: 'dark', apiKey: 'sekret-value' }, gameBuild: '1.10.163',
  assetRootsLowToHigh: ['D:\\mods\\base', 'D:\\mods\\top'],
};

const tool = (over: Record<string, unknown>) => ({
  description: 'A tool', readOnly: true, writes: false, needsOutputDir: false, expensive: false, cancellable: false,
  available: true, unavailableReason: '', evidence: ['structural'], params: [], ...over,
});

const LIST = {
  groups: [
    { id: 'plugin', title: 'Plugin checks', tools: [
      tool({ id: 'plugin.compare', title: 'Compare plugin', description: 'Compare a plugin against the reference',
             expensive: true, cancellable: true, params: [{ name: 'plugin', label: 'Plugin', kind: 'plugin', required: true }] }),
      tool({ id: 'lod.report', title: 'Export LOD report', readOnly: false, writes: true, needsOutputDir: true }),
      tool({ id: 'previs.check', title: 'Previs check', available: false, unavailableReason: 'needs the Creation Kit' }),
      tool({ id: 'lod.dryRunGenerate', title: 'LOD generate dry run' }),
    ] },
    { id: 'asset', title: 'Asset checks', tools: [tool({ id: 'nif.verify', title: 'Verify NIF' })] },
  ],
  inventory: [
    { area: 'Plugins', capability: 'Record compare', status: 'integrated', toolId: 'plugin.compare', note: 'vs xEdit' },
    { area: 'LOD', capability: 'Tile compare', status: 'available', toolId: 'lod.report', note: '' },
    { area: 'Previs', capability: 'In-game acceptance', status: 'gap', toolId: '', note: 'no harness' },
  ],
};

const SUITE_LIST = {
  groups: [
    { id: 'asset', title: 'Asset checks', tools: [
      tool({ id: 'a.one', title: 'One', cancellable: true }),
      tool({ id: 'a.two', title: 'Two' }),
      tool({ id: 'a.slow', title: 'Slow tool', expensive: true }),
      tool({ id: 'a.writer', title: 'Writer', readOnly: false, writes: true, needsOutputDir: true }),
    ] },
    { id: 'plugin', title: 'Plugin checks', tools: [tool({ id: 'p.one', title: 'Plug one' }), tool({ id: 'p.two', title: 'Plug two' })] },
  ],
  inventory: [],
};

const RESULTS = [
  { id: 'r1', title: 'Records match', status: 'fail', evidence: ['real-game-data'], durationMs: 5, outputs: ['D:\\out\\r1.txt'],
    left: { label: 'Plugin', source: 'Mod.esp', value: '12 records' }, right: { label: 'Reference', source: 'xEdit', value: '13 records' },
    diffs: ['record 00001234 missing'], detail: '' },
  { id: 'r2', title: 'Game accepts the previs', status: 'unverified', evidence: [], diffs: [], outputs: [], detail: '' },
  { id: 'r3', title: 'Header parses', status: 'pass', evidence: ['structural'], diffs: [], outputs: [], detail: '' },
];

const IDLE = { active: false, cancelled: false, runId: 0, toolId: '', progress: { done: 0, total: 0, text: '' }, results: [], error: '' };
const DONE = { ...IDLE, runId: 1, toolId: 'nif.verify', results: RESULTS, error: 'X-FO4IDE-Token: leak-123' };
const RUNNING = { ...IDLE, active: true, runId: 2, toolId: 'plugin.compare', progress: { done: 3, total: 10, text: 'Comparing records' } };

function installHost(initial: unknown = IDLE, list: unknown = LIST): { host: DebugToolsHost; setStatus: (next: unknown) => void } {
  let current = initial;
  const host: DebugToolsHost = {
    Context: vi.fn(async () => JSON.stringify(CONTEXT)),
    List: vi.fn(async () => JSON.stringify(list)),
    Start: vi.fn(async () => '{"runId":1}'),
    Status: vi.fn(async () => JSON.stringify(current)),
    Cancel: vi.fn(async () => '{"cancelled":true}'),
  };
  Object.assign(window, { chrome: { webview: { hostObjects: { debugTools: host } } } });
  return { host, setStatus: next => { current = next; } };
}

function installSuiteHost(opts: { hold?: boolean; statuses?: Record<string, string>; evidence?: string[] } = {}) {
  let runId = 0;
  let toolId = '';
  let current: unknown = IDLE;
  const finish = (cancelled = false) => {
    current = { ...IDLE, runId, toolId, cancelled, results: [{
      id: `${toolId}.r`, title: `${toolId} result`, status: opts.statuses?.[toolId] ?? 'pass', evidence: opts.evidence ?? ['real-game-data'],
      diffs: [], outputs: [], detail: '',
    }] };
  };
  const host: DebugToolsHost = {
    Context: vi.fn(async () => JSON.stringify(CONTEXT)),
    List: vi.fn(async () => JSON.stringify(SUITE_LIST)),
    Start: vi.fn(async (id: string) => {
      runId += 1;
      toolId = id;
      if (opts.hold) current = { ...IDLE, active: true, runId, toolId, progress: { done: 1, total: 2, text: 'working' } };
      else finish();
      return JSON.stringify({ runId });
    }),
    Status: vi.fn(async () => JSON.stringify(current)),
    Cancel: vi.fn(async () => '{"cancelled":true}'),
  };
  Object.assign(window, { chrome: { webview: { hostObjects: { debugTools: host } } } });
  return { host, finish };
}

const treeNav = () => screen.findByRole('navigation', { name: 'Tool tree' });
const pick = async (title: string) => { fireEvent.click(await within(await treeNav()).findByText(title)); };
const centre = () => within(screen.getByRole('region', { name: 'Selected tool' }));
const runButton = (title: string) => centre().getByRole('button', { name: `Run ${title}` }) as HTMLButtonElement;
const startedTools = (host: DebugToolsHost) => vi.mocked(host.Start).mock.calls.map(call => call[0]);
const openSuite = (label: string) => screen.findByRole('button', { name: `Open suite: ${label}` });

afterEach(() => {
  cleanup();
  Reflect.deleteProperty(window, 'chrome');
  Reflect.deleteProperty(navigator, 'clipboard');
});

describe('DebugToolsPanel', () => {
  it('is a full workspace with the context header, a tool tree and a badge per tool trait (a tool must not hide that it writes or costs)', async () => {
    installHost();
    render(<DebugToolsPanel onClose={() => undefined} />);
    expect(await screen.findByText('Plugin checks')).toBeTruthy();
    expect(screen.queryByRole('dialog')).toBeNull();
    expect(screen.getByRole('region', { name: 'Debug tools' })).toBeTruthy();
    const nav = within(await treeNav());
    expect(nav.getByText('Asset checks')).toBeTruthy();
    expect(nav.getAllByText('READ-ONLY').length).toBe(4);
    expect(nav.getByText('WRITES')).toBeTruthy();
    expect(nav.getByText('EXPENSIVE')).toBeTruthy();
    expect(nav.getByText('UNAVAILABLE')).toBeTruthy();
    expect(nav.getByText('DRY-RUN')).toBeTruthy();
    expect(nav.getAllByRole('img', { name: 'Not run yet' }).length).toBe(5);
    expect(screen.getByText(DATA_DIR)).toBeTruthy();
    expect(screen.getByText('Load order (2)')).toBeTruthy();
    expect(screen.getByText('abc1234')).toBeTruthy();
    expect(screen.getByText('Asset roots, low to high priority (2)')).toBeTruthy();
    expect(screen.getByText('2. D:\\mods\\top')).toBeTruthy();
    expect(screen.getByText('apiKey: [redacted]')).toBeTruthy();
    expect(screen.queryByText(/sekret-value/)).toBeNull();
    await pick('Verify NIF');
    expect(screen.getByText('Evidence: structural')).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Group Plugin checks' }));
    expect(nav.queryByText('Compare plugin')).toBeNull();
    expect(nav.getByText('Verify NIF')).toBeTruthy();
  });

  it('shows the environment as an open collapsible header with a one-line summary that stays on screen while tools are picked', async () => {
    installHost();
    render(<DebugToolsPanel onClose={() => undefined} />);
    const header = (await screen.findByText(/1\.2\.3 \| profile Default \| 2 plugins \| 2 asset roots/)).closest('details');
    expect(header?.hasAttribute('open')).toBe(true);
    await pick('Verify NIF');
    expect(header?.isConnected).toBe(true);
  });

  it('disables an unavailable tool and says why (an unavailable tool must not be startable or silent)', async () => {
    const { host } = installHost();
    render(<DebugToolsPanel onClose={() => undefined} />);
    await pick('Previs check');
    expect(runButton('Previs check').disabled).toBe(true);
    expect(centre().getByText('Run disabled: needs the Creation Kit')).toBeTruthy();
    fireEvent.click(runButton('Previs check'));
    expect(host.Start).not.toHaveBeenCalled();
  });

  it('keeps a WRITES tool disabled until an output folder is typed, shows the destination and refuses the Data folder', async () => {
    const { host } = installHost();
    render(<DebugToolsPanel onClose={() => undefined} />);
    await pick('Export LOD report');
    expect(runButton('Export LOD report').disabled).toBe(true);
    expect(centre().getByText('Run disabled: type an output folder')).toBeTruthy();
    const input = centre().getByLabelText('Output folder');
    expect((input as HTMLInputElement).value).toBe('');

    fireEvent.change(input, { target: { value: `${DATA_DIR}\\reports` } });
    expect(runButton('Export LOD report').disabled).toBe(true);
    expect(centre().getByText(/must not be the Fallout 4 Data folder/)).toBeTruthy();

    fireEvent.change(input, { target: { value: `${DATA_DIR.toLowerCase()}\\\\` } });
    expect(runButton('Export LOD report').disabled).toBe(true);

    for (const spelled of [`\\\\?\\${DATA_DIR}\\reports`, `//?/${DATA_DIR.replace(/\\/g, '/')}/reports`, `\\\\.\\${DATA_DIR}`]) {
      fireEvent.change(input, { target: { value: spelled } });
      expect(runButton('Export LOD report').disabled).toBe(true);
      expect(centre().getByText(/must not be the Fallout 4 Data folder/)).toBeTruthy();
    }

    fireEvent.change(input, { target: { value: 'reports' } });
    expect(runButton('Export LOD report').disabled).toBe(true);
    expect(centre().getByText(/must be an absolute path/)).toBeTruthy();

    fireEvent.change(input, { target: { value: 'D:\\reports\\..\\..' } });
    expect(centre().getByText(/must not contain '\.\.'/)).toBeTruthy();

    fireEvent.change(input, { target: { value: 'D:\\reports' } });
    expect(centre().getByText('will create debugtools-<time> inside: D:\\reports (folder must exist)')).toBeTruthy();
    expect(runButton('Export LOD report').disabled).toBe(false);
    fireEvent.click(runButton('Export LOD report'));
    await waitFor(() => expect(host.Start).toHaveBeenCalledWith('lod.report', JSON.stringify({ outputDir: 'D:\\reports' })));
  });

  it('fills the output folder from the folder dialog but still applies the safety rules to it', async () => {
    const { host } = installHost();
    const browse = vi.fn(async () => `${DATA_DIR}\\reports`);
    (window as unknown as { chrome: { webview: { hostObjects: Record<string, unknown> } } })
      .chrome.webview.hostObjects.archive = { BrowseForFolder: browse };
    render(<DebugToolsPanel onClose={() => undefined} />);
    await pick('Export LOD report');
    fireEvent.click(centre().getByRole('button', { name: 'Browse...' }));
    await waitFor(() => expect((centre().getByLabelText('Output folder') as HTMLInputElement).value).toBe(`${DATA_DIR}\\reports`));
    expect(runButton('Export LOD report').disabled).toBe(true);
    expect(centre().getByText(/must not be the Fallout 4 Data folder/)).toBeTruthy();
    browse.mockResolvedValue('D:\\reports');
    fireEvent.click(centre().getByRole('button', { name: 'Browse...' }));
    await waitFor(() => expect(runButton('Export LOD report').disabled).toBe(false));
    expect(host.Start).not.toHaveBeenCalled();
  });

  it('offers no Browse button when the host has no folder dialog', async () => {
    installHost();
    render(<DebugToolsPanel onClose={() => undefined} />);
    await pick('Export LOD report');
    expect(centre().queryByRole('button', { name: 'Browse...' })).toBeNull();
  });

  it('blocks a writing tool while the context failed to load (a folder inside the unknown Data folder cannot be ruled out)', async () => {
    const { host } = installHost();
    vi.mocked(host.Context).mockRejectedValue(new Error('debugTools.Context: HTTP 500 boom'));
    render(<DebugToolsPanel onClose={() => undefined} />);
    await pick('Export LOD report');
    fireEvent.change(centre().getByLabelText('Output folder'), { target: { value: 'D:\\reports' } });
    expect(runButton('Export LOD report').disabled).toBe(true);
    expect(centre().getByText(/Data folder is unknown/)).toBeTruthy();
  });

  it('needs the required plugin param and starts with the tool id and the chosen plugin (a Start with the wrong args runs the wrong check)', async () => {
    const { host } = installHost();
    render(<DebugToolsPanel onClose={() => undefined} />);
    await pick('Compare plugin');
    expect(runButton('Compare plugin').disabled).toBe(true);
    expect(centre().getByText('Run disabled: required: Plugin')).toBeTruthy();
    fireEvent.change(centre().getByLabelText('Plugin'), { target: { value: 'Mod.esp' } });
    fireEvent.click(runButton('Compare plugin'));
    await waitFor(() => expect(host.Start).toHaveBeenCalledWith('plugin.compare', JSON.stringify({ plugin: 'Mod.esp' })));
  });

  it('shows a refused Start as an error and keeps the panel usable (a busy host must not look like a started run)', async () => {
    const { host } = installHost();
    vi.mocked(host.Start).mockRejectedValue(new Error('debugTools.Start: DebugToolsBusy: another run is active'));
    render(<DebugToolsPanel onClose={() => undefined} />);
    await pick('Verify NIF');
    fireEvent.click(runButton('Verify NIF'));
    expect(await screen.findByText(/DebugToolsBusy: another run is active/)).toBeTruthy();
    expect(runButton('Verify NIF').disabled).toBe(false);
  });

  it('renders polled results with both sides, diffs and outputs, unverified above passed and the banner (no hidden or one-sided results)', async () => {
    const { setStatus } = installHost();
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    setStatus(DONE);
    await pick('Verify NIF');
    fireEvent.click(runButton('Verify NIF'));
    expect(await screen.findByText('Records match')).toBeTruthy();
    const table = within(screen.getByRole('table'));
    for (const text of ['12 records', '13 records', 'Mod.esp', 'xEdit', 'Plugin', 'Reference']) {
      expect(table.getByText(text)).toBeTruthy();
    }
    expect(screen.getByText('record 00001234 missing')).toBeTruthy();
    expect(screen.getByText('Output: D:\\out\\r1.txt')).toBeTruthy();
    expect(screen.getByRole('note').textContent).toMatch(/do not prove Fallout 4 accepts a file.*unverified unless an observed-in-game/);
    expect(screen.getByText('Header parses').closest('.dt-result')?.textContent).toContain('structural only');
    const unverified = screen.getByText('Unverified (1)');
    expect(unverified.compareDocumentPosition(screen.getByText('Passed (1)')) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(unverified.parentElement?.textContent).toContain('Game accepts the previs');
    expect(screen.getByText('UNVERIFIED').className).toContain('dbg-unverified');
    expect(screen.getAllByText('FAIL').some(el => el.className.includes('dbg-fail'))).toBe(true);
    expect(within(await treeNav()).getByRole('img', { name: 'Last run: fail' })).toBeTruthy();
  });

  it('re-attaches to an active run, shows progress, blocks a second start and cancels through the host', async () => {
    const { host } = installHost(RUNNING);
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    expect(await screen.findByText(/Comparing records \(3\/10\)/)).toBeTruthy();
    await pick('Compare plugin');
    fireEvent.change(centre().getByLabelText('Plugin'), { target: { value: 'Mod.esp' } });
    expect(runButton('Compare plugin').disabled).toBe(true);
    expect((screen.getByRole('button', { name: 'Run suite all' }) as HTMLButtonElement).disabled).toBe(true);
    fireEvent.click(screen.getByRole('button', { name: /Cancel/ }));
    await waitFor(() => expect(host.Cancel).toHaveBeenCalledTimes(1));
  });

  it('leaves Cancel disabled for a tool that is not cancellable (Cancel must not promise to stop what it cannot)', async () => {
    installHost({ ...RUNNING, toolId: 'nif.verify' });
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    await treeNav();
    await screen.findByText(/Comparing records/);
    expect((screen.getByRole('button', { name: /Cancel/ }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('copies a report of the selected run with the context and without secrets (a pasted report must not leak the token or key)', async () => {
    const { setStatus } = installHost();
    const writeText = vi.fn(async () => undefined);
    Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true });
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    setStatus(DONE);
    await pick('Verify NIF');
    fireEvent.click(runButton('Verify NIF'));
    fireEvent.click(await screen.findByRole('button', { name: 'Copy report' }));
    await waitFor(() => expect(writeText).toHaveBeenCalledTimes(1));
    const report = (writeText.mock.calls[0] as unknown as [string])[0];
    expect(report).toContain(`Fallout 4 data dir: ${DATA_DIR}`);
    expect(report).toContain('Mod.esp');
    expect(report).toContain('== UNVERIFIED');
    expect(report).toContain('apiKey: [redacted]');
    expect(report).not.toContain('sekret-value');
    expect(report).not.toContain('leak-123');
    expect(report).not.toContain('== RUNS');
  });

  it('lists the capability inventory with counts and says the coverage is partial (a gap must not be hidden)', async () => {
    installHost();
    render(<DebugToolsPanel onClose={() => undefined} />);
    await screen.findByText('Plugin checks');
    fireEvent.click(screen.getByRole('tab', { name: 'Capability inventory' }));
    expect(screen.getByText(/Coverage is partial/)).toBeTruthy();
    expect(screen.getByText('Integrated: 1 | Available: 1 | Gap: 1')).toBeTruthy();
    expect(screen.getByText('In-game acceptance').closest('tr')?.textContent).toContain('GAP');
    expect(screen.getByText('Record compare').closest('tr')?.textContent).toContain('plugin.compare');
    expect(screen.queryByText('Plugin checks')).toBeNull();
  });

  it('shows a plain message when the host has no debugTools object (an older host must not crash the menu)', async () => {
    render(<DebugToolsPanel onClose={() => undefined} />);
    expect(await screen.findByText(/Debug tools are not available: this FO4IDE host has no debugTools object/)).toBeTruthy();
    expect((screen.getByRole('button', { name: /Refresh/ }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('recovers from a failed first Status probe through Retry (one transient failure must not disable the panel until reload)', async () => {
    const { host } = installHost();
    vi.mocked(host.Status).mockRejectedValueOnce(new Error('debugTools.Status: HTTP 503 starting'));
    render(<DebugToolsPanel onClose={() => undefined} />);
    expect(await screen.findByText(/Debug tools are not available: debugTools\.Status: HTTP 503 starting/)).toBeTruthy();
    expect(host.List).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Retry' }));
    await waitFor(() => expect(screen.queryByText(/Debug tools are not available/)).toBeNull());
    expect(screen.queryByRole('button', { name: 'Retry' })).toBeNull();
    expect(host.List).toHaveBeenCalledTimes(1);
    expect(await treeNav()).toBeTruthy();
    expect((screen.getByRole('button', { name: /Refresh/ }) as HTMLButtonElement).disabled).toBe(false);
  });

  it('shows one plain message, not three raw errors, when the bridge proxies debugTools but the host target is missing', async () => {
    const { host } = installHost();
    for (const method of ['Context', 'List', 'Start', 'Status', 'Cancel'] as const) {
      (host[method] as ReturnType<typeof vi.fn>).mockRejectedValue(new Error(`debugTools.${method}: HTTP 500 no such target`));
    }
    render(<DebugToolsPanel onClose={() => undefined} />);
    expect(await screen.findByText(/Debug tools are not available: debugTools\.Status: HTTP 500 no such target/)).toBeTruthy();
    expect(screen.queryByText(/debugTools\.(Context|List): HTTP 500/)).toBeNull();
    expect(host.List).not.toHaveBeenCalled();
    expect((screen.getByRole('button', { name: /Refresh/ }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('still offers the report when a run ended with an error and no results (the failed run is when a report is wanted)', async () => {
    const { setStatus } = installHost();
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    setStatus({ ...IDLE, runId: 3, toolId: 'nif.verify', error: 'nif.verify: native failure' });
    await pick('Verify NIF');
    fireEvent.click(runButton('Verify NIF'));
    expect(await screen.findByText('nif.verify: native failure')).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Copy report' })).toBeTruthy();
  });

  it('says a cancelled run is partial (partial rows must not read as a complete run)', async () => {
    const { setStatus } = installHost();
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    setStatus({ ...DONE, error: '', cancelled: true });
    await pick('Verify NIF');
    fireEvent.click(runButton('Verify NIF'));
    expect(await screen.findByText('Run was cancelled: results are partial')).toBeTruthy();
  });

  it('redacts secrets and the user profile name in what it shows on screen (a screenshot must not leak them)', async () => {
    const { setStatus } = installHost();
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    setStatus({ ...IDLE, active: true, runId: 5, toolId: 'nif.verify', progress: { done: 1, total: 2, text: 'reading C:\\Users\\Bob\\a.esp' } });
    await pick('Verify NIF');
    fireEvent.click(runButton('Verify NIF'));
    await screen.findByText(/reading %USERPROFILE%/);
    setStatus({ ...IDLE, runId: 5, toolId: 'nif.verify',
      results: [{ id: 'x', title: 'Leaky', status: 'fail', evidence: ['real-game-data'], outputs: ['C:\\Users\\Bob\\out.txt'],
        detail: 'Authorization: Bearer abc-secret-111', diffs: ['token=diff-tok-222'],
        left: { label: 'L', source: '/home/alice/a.esp', value: 'password=pw-333' }, right: null }] });
    await screen.findByText('Leaky');
    const text = document.body.textContent ?? '';
    for (const leak of ['abc-secret-111', 'diff-tok-222', 'pw-333', 'Bob', 'alice']) expect(text).not.toContain(leak);
    expect(text).toContain('%USERPROFILE%');
  });

  it('offers the older Debug checks panel only when the shell passes a way to open it', async () => {
    installHost();
    const onOpenChecks = vi.fn();
    const { rerender } = render(<DebugToolsPanel onClose={() => undefined} onOpenChecks={onOpenChecks} />);
    fireEvent.click(await screen.findByRole('button', { name: /Debug checks/ }));
    expect(onOpenChecks).toHaveBeenCalledTimes(1);
    rerender(<DebugToolsPanel onClose={() => undefined} />);
    expect(screen.queryByRole('button', { name: /Debug checks/ })).toBeNull();
  });

  it('filters the tree by id, title, description and group, and says when nothing matches', async () => {
    installHost();
    render(<DebugToolsPanel onClose={() => undefined} />);
    const nav = within(await treeNav());
    await nav.findByText('Verify NIF');
    const filter = nav.getByLabelText('Filter tools');
    fireEvent.change(filter, { target: { value: 'nif' } });
    expect(nav.getByText('Verify NIF')).toBeTruthy();
    expect(nav.queryByText('Compare plugin')).toBeNull();
    expect(nav.queryByText('Plugin checks')).toBeNull();
    fireEvent.change(filter, { target: { value: 'COMPARE' } });
    expect(nav.getByText('Compare plugin')).toBeTruthy();
    expect(nav.queryByText('Verify NIF')).toBeNull();
    fireEvent.change(filter, { target: { value: 'asset checks' } });
    expect(nav.getByText('Verify NIF')).toBeTruthy();
    fireEvent.change(filter, { target: { value: 'no-such-tool' } });
    expect(nav.getByText('No tools match the filter.')).toBeTruthy();
    fireEvent.click(nav.getByRole('button', { name: 'Clear filter' }));
    expect(nav.getByText('Compare plugin')).toBeTruthy();
    expect(nav.getByText('Verify NIF')).toBeTruthy();
  });
});

describe('DebugToolsPanel suites and history', () => {
  it('runs the read-only tools of one group in order and aggregates them without counting skipped or unrun tools as passed', async () => {
    const { host } = installSuiteHost({ statuses: { 'a.two': 'skip' } });
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    await pick('Writer');
    fireEvent.change(centre().getByLabelText('Output folder'), { target: { value: 'D:\\reports' } });
    expect(runButton('Writer').disabled).toBe(false);
    fireEvent.click(screen.getByRole('button', { name: 'Run suite Asset checks' }));
    await openSuite('Asset checks');
    expect(startedTools(host)).toEqual(['a.one', 'a.two']);
    expect(screen.getByText('Tools run: 2 | Not run: 2 (never counted as passed)')).toBeTruthy();
    expect(screen.getByText('Runs: pass 1 | pass (structural only) 0 | fail 0 | cancelled 0 | unverified 0 | incomplete 1 | no results 0')).toBeTruthy();
    expect(screen.getByText('Passed: 1 (structural only: 0) | Failed: 0 | Errors: 0 | Unverified: 0 | Skipped: 1 | Unsupported: 0')).toBeTruthy();
    expect(screen.getByText(/Writer: writes or is not read-only/)).toBeTruthy();
    expect(screen.getByText(/Slow tool: expensive/)).toBeTruthy();
    const item = screen.getByRole('button', { name: 'Open suite: Asset checks' });
    expect(item.textContent).toContain('INCOMPLETE');
    expect(item.textContent).not.toContain('PASS');
  });

  it('runs every read-only tool for "all", adds the expensive ones only when asked and never a writing one', async () => {
    const { host } = installSuiteHost();
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    await within(await treeNav()).findByText('One');
    fireEvent.click(screen.getByRole('button', { name: 'Run suite all' }));
    await openSuite('all read-only tools');
    expect(startedTools(host)).toEqual(['a.one', 'a.two', 'p.one', 'p.two']);
    cleanup();
    Reflect.deleteProperty(window, 'chrome');

    const second = installSuiteHost();
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    await within(await treeNav()).findByText('One');
    fireEvent.click(screen.getByLabelText('Include expensive tools'));
    fireEvent.click(screen.getByRole('button', { name: 'Run suite all' }));
    const item = await openSuite('all read-only tools');
    expect(startedTools(second.host)).toEqual(['a.one', 'a.two', 'a.slow', 'p.one', 'p.two']);
    expect(screen.getByText('Tools run: 5 | Not run: 1 (never counted as passed)')).toBeTruthy();
    expect(item.textContent).toContain('INCOMPLETE');
  });

  it('stops the queue on Stop suite, cancels a cancellable tool and reports the rest as not run', async () => {
    const { host, finish } = installSuiteHost({ hold: true });
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    fireEvent.click(await screen.findByRole('button', { name: 'Run suite Asset checks' }));
    expect(await screen.findByText(/Suite Asset checks: tool 1 of 2/)).toBeTruthy();
    await screen.findByText(/working \(1\/2\)/);
    fireEvent.click(screen.getByRole('button', { name: 'Stop suite' }));
    await waitFor(() => expect(host.Cancel).toHaveBeenCalledTimes(1));
    finish(true);
    await openSuite('Asset checks');
    expect(startedTools(host)).toEqual(['a.one']);
    expect(screen.getByText('Suite was cancelled: the tools after the stop did not run')).toBeTruthy();
    expect(screen.getByText('Tools run: 1 | Not run: 3 (never counted as passed)')).toBeTruthy();
    expect(screen.getByText('Runs: pass 0 | pass (structural only) 0 | fail 0 | cancelled 1 | unverified 0 | incomplete 0 | no results 0')).toBeTruthy();
    expect(screen.getByText(/Two: the previous tool was cancelled/)).toBeTruthy();
  });

  it('stops the queue after a tool that cannot be cancelled, without calling Cancel, and says it is stopping', async () => {
    const { host, finish } = installSuiteHost({ hold: true });
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    fireEvent.click(await screen.findByRole('button', { name: 'Run suite Plugin checks' }));
    await screen.findByText(/working \(1\/2\)/);
    fireEvent.click(screen.getByRole('button', { name: 'Stop suite' }));
    expect(await screen.findByText('Stopping after the current tool finishes.')).toBeTruthy();
    expect(host.Cancel).not.toHaveBeenCalled();
    finish(false);
    await openSuite('Plugin checks');
    expect(startedTools(host)).toEqual(['p.one']);
    expect(screen.getByText(/Plug two: the queue was cancelled before this tool started/)).toBeTruthy();
    expect(screen.getByText('Suite was cancelled: the tools after the stop did not run')).toBeTruthy();
  });

  it('keeps a suite waiting while Status rejects and finishes it when Status answers again (an unreachable Status must not skip or end the queue)', async () => {
    const { host } = installSuiteHost();
    const answer = vi.mocked(host.Status).getMockImplementation()!;
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    const suiteButton = await screen.findByRole('button', { name: 'Run suite Plugin checks' });
    await waitFor(() => expect((suiteButton as HTMLButtonElement).disabled).toBe(false));
    vi.mocked(host.Status).mockRejectedValue(new Error('debugTools.Status: bridge down'));
    fireEvent.click(suiteButton);
    expect(await screen.findByText(/debugTools\.Status: bridge down/)).toBeTruthy();
    await waitFor(() => expect(vi.mocked(host.Status).mock.calls.length).toBeGreaterThanOrEqual(4));
    expect(startedTools(host)).toEqual(['p.one']);
    expect(screen.queryByRole('button', { name: 'Open suite: Plugin checks' })).toBeNull();
    vi.mocked(host.Status).mockImplementation(answer);
    await openSuite('Plugin checks');
    expect(startedTools(host)).toEqual(['p.one', 'p.two']);
    expect(screen.getByText('Tools run: 2 | Not run: 0 (never counted as passed)')).toBeTruthy();
  });

  it('ends the suite when the host never reports the started run, with every tool listed as not run', async () => {
    const { host } = installHost(IDLE, SUITE_LIST);
    vi.mocked(host.Start).mockResolvedValue('{"runId":9}');
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    fireEvent.click(await screen.findByRole('button', { name: 'Run suite Plugin checks' }));
    await openSuite('Plugin checks');
    expect(startedTools(host)).toEqual(['p.one']);
    expect(screen.getByText('Tools run: 0 | Not run: 2 (never counted as passed)')).toBeTruthy();
    expect(screen.getByText(/Plug one: the run did not report back/)).toBeTruthy();
  });

  it('keeps a run history for the session, reopens a past result and shows a status dot per tool', async () => {
    installSuiteHost({ statuses: { 'a.two': 'skip' } });
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    await pick('One');
    fireEvent.click(runButton('One'));
    await screen.findByText('Last run: One (run 1)');
    await pick('Two');
    fireEvent.click(runButton('Two'));
    await screen.findByText('Last run: Two (run 2)');
    const nav = within(await treeNav());
    expect(nav.getByRole('img', { name: 'Last run: pass' })).toBeTruthy();
    expect(nav.getByRole('img', { name: 'Last run: incomplete' })).toBeTruthy();
    expect(nav.getAllByRole('img', { name: 'Not run yet' }).length).toBe(4);

    fireEvent.click(await screen.findByRole('tab', { name: 'Run history (2)' }));
    expect(screen.getByRole('button', { name: 'Open run 2: Two' })).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Open run 1: One' }));
    expect(await screen.findByText('Run: One (run 1)')).toBeTruthy();
    expect(screen.getByText('a.one result')).toBeTruthy();
    expect(screen.queryByText('a.two result')).toBeNull();
  });

  it('shows a run whose passes are structural only as "PASS (structural only)", not as a plain pass', async () => {
    installSuiteHost({ evidence: ['structural'] });
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    await pick('One');
    fireEvent.click(runButton('One'));
    await screen.findByText('Last run: One (run 1)');
    const nav = within(await treeNav());
    expect(nav.getByRole('img', { name: 'Last run: pass (structural only)' })).toBeTruthy();
    expect(nav.queryByRole('img', { name: 'Last run: pass' })).toBeNull();
    expect(centre().getByText('PASS (structural only)')).toBeTruthy();
    expect(screen.getByText('Passed: 1 (structural only: 1) | Failed: 0 | Errors: 0 | Unverified: 0 | Skipped: 0 | Unsupported: 0')).toBeTruthy();
  });

  it('copies a suite report and a session report that list the runs and the tools that did not run, without secrets', async () => {
    installSuiteHost();
    const writeText = vi.fn(async () => undefined);
    Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true });
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    expect((await screen.findByRole('button', { name: 'Copy session report' }) as HTMLButtonElement).disabled).toBe(true);
    fireEvent.click(await screen.findByRole('button', { name: 'Run suite Asset checks' }));
    await openSuite('Asset checks');

    fireEvent.click(screen.getByRole('button', { name: 'Copy suite report' }));
    await waitFor(() => expect(writeText).toHaveBeenCalledTimes(1));
    const suite = (writeText.mock.calls[0] as unknown as [string])[0];
    expect(suite).toContain('Checks run: suite Asset checks');
    expect(suite).toContain('== RUNS (2) ==');
    expect(suite).toContain('== NOT RUN (2) ==');
    expect(suite).toContain('Tools that did not run are not passes.');
    expect(suite).toContain('[One] a.one result');

    fireEvent.click(screen.getByRole('button', { name: 'Copy session report' }));
    await waitFor(() => expect(writeText).toHaveBeenCalledTimes(2));
    const session = (writeText.mock.calls[1] as unknown as [string])[0];
    expect(session).toContain('Checks run: session');
    expect(session).toContain('== RUNS (2) ==');
    expect(session).toContain('Writer (a.writer)');
    expect(session).toContain(`Fallout 4 data dir: ${DATA_DIR}`);
    expect(session).not.toContain('sekret-value');
  });
});

describe('debugToolsClient parsers', () => {
  it('fail safe: unknown inventory status is a gap, an unlisted availability is unavailable, a writing tool is never read-only', () => {
    const list = parseToolList(JSON.stringify({
      groups: [{ id: 'g', tools: [{ id: 't', readOnly: true, writes: true }, { id: '' }] }],
      inventory: [{ area: 'a', capability: 'c', status: 'done' }],
    }));
    expect(list.groups[0].tools).toHaveLength(1);
    expect(list.groups[0].tools[0]).toMatchObject({ available: false, readOnly: false, writes: true });
    expect(list.inventory[0].status).toBe('gap');
    expect(() => parseToolStatus('not json')).toThrow(/did not return JSON/);
  });

  it('reads the numeric runId the host sends from Start and Status (a text-only read throws after the run already started)', () => {
    expect(parseRunId('{"runId":7}')).toBe('7');
    expect(() => parseRunId('{}')).toThrow(/no runId/);
    expect(parseToolStatus('{"runId":7,"cancelled":true,"active":true}')).toMatchObject({ runId: '7', cancelled: true, active: true });
  });

  it('stamps a run first seen after it finished with the host finish time, not the time the panel looked', async () => {
    const finishedAt = '2020-01-02T03:04:05.000Z';
    expect(parseToolStatus(JSON.stringify({ ...DONE, finishedAt })).finishedAt).toBe(finishedAt);
    installHost({ ...DONE, finishedAt });
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    fireEvent.click(await screen.findByRole('tab', { name: 'Run history (1)' }));
    expect(await screen.findByText(new Date(finishedAt).toLocaleTimeString())).toBeTruthy();
  });

  it('finds a started run through the panel when Start answers with a number', async () => {
    const { host } = installHost();
    vi.mocked(host.Start).mockResolvedValue('{"runId":42}');
    render(<DebugToolsPanel onClose={() => undefined} pollMs={10} />);
    await pick('Verify NIF');
    fireEvent.click(runButton('Verify NIF'));
    await waitFor(() => expect(vi.mocked(host.Status).mock.calls.length).toBeGreaterThanOrEqual(3));
    expect(screen.queryByText(/did not return|no runId/)).toBeNull();
  });
});
