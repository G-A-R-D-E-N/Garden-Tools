import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxChainTools from './HkxChainTools';

const PROJECT = 'Actors/Foo/Foo.hkx';
const MO2 = 'C:\\MO2';
const BIG_ID = '18446744073709551557';

const link = (role: string, declared: string, over: Record<string, unknown> = {}) =>
  ({ role, declared, resolved: declared.toLowerCase(), source: 'Data', exists: true, blocked: false, note: '', ...over });

const chainReply = (over: Record<string, unknown> = {}) => ({
  root: 'actors/foo',
  links: [link('project', PROJECT), link('character', 'Actors/Foo/Characters/Foo.hkx')],
  animations: [link('animation', 'Actors/Foo/Animations/Gone.hkx', { exists: false, source: '' })],
  problems: ['animation Actors/Foo/Animations/Gone.hkx is missing'],
  warnings: [],
  ok: false,
  notes: [],
  ...over,
});

const checkReply = (over: Record<string, unknown> = {}) => ({
  root: 'actors/foo',
  chain_problems: [],
  chain_warnings: [],
  files: 3,
  read: 3,
  truncated: false,
  complete: true,
  errors: 0,
  warnings: 0,
  findings: [],
  unreadable: 0,
  round_trip_losses: 0,
  files_with_round_trip_losses: 0,
  summary: '3 of 3 behaviour files read: 0 errors, 0 warnings',
  report: {
    project: 'actors/foo', summary: '', truncated: false, project_findings: [], files: [],
    totals: { files: 3, readable: 3, unreadable: 0, errors: 0, warnings: 0, round_trip_losses: 0, files_with_round_trip_losses: 0 },
  },
  ok: true,
  notes: [],
  ...over,
});

const lossFile = (name: string, over: Record<string, unknown> = {}) => ({
  path: `actors/foo/behaviors/${name}`, name, unreadable: null, errors: 0, warnings: 0, findings: [],
  round_trip_losses: [{ kind: 'UnsupportedClass', object_id: null, member: null, message: 'hkbFoo is a class this build has no definition for', blocks_save: true }],
  ...over,
});

const type = (label: string, value: string) => fireEvent.change(screen.getByLabelText(label), { target: { value } });

const fill = () => {
  type('Chain project hkx', PROJECT);
  type('Chain MO2 instance folder', MO2);
  type('Chain MO2 profile', 'Test');
};

const renderTools = (host: unknown) => render(<HkxChainTools host={host as HkxHost} disabled={false} />);

afterEach(() => cleanup());

describe('HkxChainTools', () => {
  it('lists a missing animation as a problem and passes the MO2 sources to the host', async () => {
    // Constraint: detects a chain view that shows the links but hides a missing animation, or drops the MO2 profile.
    const ProjectChain = vi.fn(async () => JSON.stringify(chainReply()));
    renderTools({ ProjectChain });
    fill();
    fireEvent.click(screen.getByText('Follow chain'));
    await waitFor(() => expect(screen.getByLabelText('Chain problems').textContent).toContain('Gone.hkx is missing'));
    expect(ProjectChain).toHaveBeenCalledWith(PROJECT, '', MO2, 'Test');
    expect(screen.getByLabelText('Chain animations').textContent).toContain('(missing)');
    expect(screen.getByRole('status').textContent).toContain('Chain has problems');
  });

  it('never calls a chain with problems ok, even when the host flag says ok', async () => {
    // Constraint: detects a verdict taken from ok alone, which a host that drops its problems list would turn into a false OK.
    renderTools({ ProjectChain: vi.fn(async () => JSON.stringify(chainReply({ ok: true }))) });
    fill();
    fireEvent.click(screen.getByText('Follow chain'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('Chain has problems'));
  });

  it('shows a truncated check as stopped and not clean', async () => {
    // Constraint: detects a check that stopped at the file limit being shown as a small clean project.
    const ProjectCheck = vi.fn(async () => JSON.stringify(checkReply({ files: 5000, read: 2000, truncated: true, complete: false, ok: false })));
    renderTools({ ProjectCheck });
    fill();
    type('Check file limit', '2000');
    fireEvent.click(screen.getByText('Check project'));
    await waitFor(() => expect(screen.getByLabelText('Check verdict').textContent).toContain('Stopped at the file limit: 2000 of 5000'));
    expect(screen.getByLabelText('Check verdict').textContent).toContain('Not clean');
    expect(screen.getByRole('status').textContent).toContain('Incomplete check, not clean');
    expect(ProjectCheck).toHaveBeenCalledWith(PROJECT, '', MO2, 'Test', 2000);
  });

  it('does not show a check clean when ok is true but the scan is incomplete', async () => {
    // Constraint: detects a verdict that trusts ok without complete, so a partial scan reads as clean.
    renderTools({ ProjectCheck: vi.fn(async () => JSON.stringify(checkReply({ complete: false }))) });
    fill();
    fireEvent.click(screen.getByText('Check project'));
    await waitFor(() => expect(screen.getByLabelText('Check verdict').textContent).toContain('Not clean'));
  });

  it('lists each finding with its level, code and file, and a clean complete check says clean', async () => {
    // Constraint: detects findings counted but not listed, and a clean verdict that is never shown for a good project.
    const finding = { file: 'actors/foo/behaviors/foobehavior.hkx', level: 'error', code: 'MISSING_ANIM', message: 'clip is absent' };
    const ProjectCheck = vi.fn()
      .mockResolvedValueOnce(JSON.stringify(checkReply({ errors: 1, ok: false, findings: [finding] })))
      .mockResolvedValueOnce(JSON.stringify(checkReply()));
    renderTools({ ProjectCheck });
    fill();
    fireEvent.click(screen.getByText('Check project'));
    await waitFor(() => expect(screen.getByLabelText('Check findings').textContent).toContain('error MISSING_ANIM in actors/foo/behaviors/foobehavior.hkx: clip is absent'));
    expect(screen.getByLabelText('Check verdict').textContent).toContain('Not clean');
    await waitFor(() => expect((screen.getByText('Check project') as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByText('Check project'));
    await waitFor(() => expect(screen.getByLabelText('Check verdict').textContent).toContain('Clean'));
    expect(screen.getByLabelText('Check verdict').textContent).not.toContain('Not clean');
    expect(screen.queryByLabelText('Check findings')).toBeNull();
  });

  it('does not show a check clean when the reply has no truncated field', async () => {
    // Constraint: detects a verdict that treats a missing truncated field as false, so a host that drops it reads as clean.
    const noTruncated: Record<string, unknown> = checkReply();
    delete noTruncated.truncated;
    renderTools({ ProjectCheck: vi.fn(async () => JSON.stringify(noTruncated)) });
    fill();
    fireEvent.click(screen.getByText('Check project'));
    await waitFor(() => expect(screen.getByLabelText('Check verdict').textContent).toContain('Not clean'));
    expect(screen.getByRole('status').textContent).toContain('Incomplete check, not clean');
  });

  it('lists a round-trip loss and an unreadable file and never calls the check clean, even when the host flag says ok', async () => {
    // Constraint: detects a project whose file cannot be saved natively being shown as clean because the verdict trusted ok alone, or the loss being counted but never listed.
    const report = checkReply().report;
    const reply = checkReply({
      round_trip_losses: 1, files_with_round_trip_losses: 1, unreadable: 1, ok: true,
      summary: '2 of 3 behaviour files read: 0 errors, 0 warnings, 1 round-trip issue in 1 file, 1 could not be unpacked',
      report: { ...report, files: [lossFile('Unsafe.hkx'), lossFile('Broken.hkx', { unreadable: 'cannot be read as a packfile: bad magic', round_trip_losses: [] })] },
    });
    renderTools({ ProjectCheck: vi.fn(async () => JSON.stringify(reply)) });
    fill();
    fireEvent.click(screen.getByText('Check project'));
    await waitFor(() => expect(screen.getByLabelText('Check file problems').textContent).toContain('Unsafe.hkx: round-trip UnsupportedClass, hkbFoo is a class this build has no definition for'));
    expect(screen.getByLabelText('Check file problems').textContent).toContain('Broken.hkx: could not be unpacked, cannot be read as a packfile: bad magic');
    expect(screen.getByLabelText('Check summary').textContent).toContain('1 round-trip issue in 1 file');
    expect(screen.getByLabelText('Check verdict').textContent).toContain('Not clean');
    expect(screen.getByRole('status').textContent).toContain('Check found problems');
  });

  it('reads a reply with no round-trip fields as zero losses instead of printing undefined or throwing', async () => {
    // Constraint: detects a host reply without round_trip_losses (top level or per file) showing "undefined round-trip issue(s)" or crashing the file list.
    const noLosses = JSON.parse(JSON.stringify(
      checkReply({ report: { ...checkReply().report, files: [lossFile('Odd.hkx', { unreadable: 'bad magic' })] } })));
    delete noLosses.round_trip_losses;
    delete noLosses.files_with_round_trip_losses;
    delete noLosses.report.files[0].round_trip_losses;
    renderTools({ ProjectCheck: vi.fn(async () => JSON.stringify(noLosses)) });
    fill();
    fireEvent.click(screen.getByText('Check project'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('0 round-trip issue(s)'));
    expect(screen.getByRole('status').textContent).not.toContain('undefined');
    expect(screen.queryByRole('alert')).toBeNull();
    expect(screen.getByLabelText('Check file problems').textContent).toContain('Odd.hkx: could not be unpacked, bad magic');
  });

  it('asks the host for the CSV or HTML report and shows its text', async () => {
    // Constraint: detects a report button that checks the project but never asks for the rendered text, or asks for the wrong format.
    const ProjectCheck = vi.fn()
      .mockResolvedValueOnce(JSON.stringify(checkReply({ format: 'csv', rendered: 'project,file\nactors/foo,a.hkx\n' })))
      .mockResolvedValueOnce(JSON.stringify(checkReply({ format: 'html', rendered: '<!doctype html>' })));
    renderTools({ ProjectCheck });
    fill();
    fireEvent.click(screen.getByText('Report CSV'));
    await waitFor(() => expect((screen.getByLabelText('Check report text') as HTMLTextAreaElement).value).toContain('actors/foo,a.hkx'));
    expect(ProjectCheck).toHaveBeenLastCalledWith(PROJECT, '', MO2, 'Test', 2000, 'csv');
    await waitFor(() => expect((screen.getByText('Report HTML') as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByText('Report HTML'));
    await waitFor(() => expect((screen.getByLabelText('Check report text') as HTMLTextAreaElement).value).toBe('<!doctype html>'));
    expect(ProjectCheck).toHaveBeenLastCalledWith(PROJECT, '', MO2, 'Test', 2000, 'html');
  });

  it('refuses a check reply that has no report', async () => {
    // Constraint: detects a host that drops the report being shown as a check with nothing to list.
    const noReport: Record<string, unknown> = checkReply();
    delete noReport.report;
    renderTools({ ProjectCheck: vi.fn(async () => JSON.stringify(noReport)) });
    fill();
    fireEvent.click(screen.getByText('Check project'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('missing the report'));
    expect(screen.queryByLabelText('Check verdict')).toBeNull();
  });

  it('says the host replied with no result for a null reply', async () => {
    // Constraint: detects a literal null reply surfacing as a TypeError instead of a readable message.
    renderTools({ ProjectChain: vi.fn(async () => 'null') });
    fill();
    fireEvent.click(screen.getByText('Follow chain'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('replied with no result'));
  });

  it('shows a host refusal as an error and no result', async () => {
    // Constraint: detects a refused source set (for example a wrong MO2 profile) shown as an empty clean result.
    renderTools({ ProjectCheck: vi.fn(async () => JSON.stringify({ error: 'MO2 profile Test not found' })) });
    fill();
    fireEvent.click(screen.getByText('Check project'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('MO2 profile Test not found'));
    expect(screen.queryByLabelText('Check verdict')).toBeNull();
    expect(screen.queryByRole('status')).toBeNull();
  });

  it('refuses a limit that is not a whole number without calling the host', async () => {
    // Constraint: detects a bad limit sent to the host as NaN, which reads back as the default scan size.
    const ProjectCheck = vi.fn(async () => JSON.stringify(checkReply()));
    renderTools({ ProjectCheck });
    fill();
    type('Check file limit', 'many');
    fireEvent.click(screen.getByText('Check project'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('limit must be a whole number'));
    expect(ProjectCheck).not.toHaveBeenCalled();
  });

  it('says an older host build cannot follow a chain instead of showing an empty one', async () => {
    // Constraint: detects a host without the RPC shown as a project with no links.
    renderTools({});
    fill();
    fireEvent.click(screen.getByText('Follow chain'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot follow a project chain'));
  });

  it('needs a path and a Data folder or MO2 instance before it enables a run', () => {
    // Constraint: detects a run sent with no source, which the host would only refuse after the click.
    renderTools({ ProjectChain: vi.fn() });
    expect((screen.getByText('Follow chain') as HTMLButtonElement).disabled).toBe(true);
    type('Chain project hkx', PROJECT);
    expect((screen.getByText('Follow chain') as HTMLButtonElement).disabled).toBe(true);
    type('Chain Data folder', 'C:\\Games\\Data');
    expect((screen.getByText('Follow chain') as HTMLButtonElement).disabled).toBe(false);
  });

  it('reads a crash subgraph id through the MO2 sources and keeps the id exact', async () => {
    // Constraint: detects a 64-bit id passed as a number (digits lost) or an MO2 profile dropped from the crash lookup.
    const manifest = {
      source: 'C:\\MO2\\mods\\High', path: 'meshes\\animtextdata\\animationfiledata\\x.txt',
      behavior_paths: ['Actors\\Foo\\Behaviors\\FooBehavior.hkx'],
      animation_paths: ['Actors\\Foo\\a.hkx', 'Actors\\Foo\\b.hkx'], present: 1, unresolved: ['Actors\\Foo\\b.hkx'],
    };
    const SubgraphManifest = vi.fn(async () => JSON.stringify({ id_source: 'given', manifest, notes: [] }));
    renderTools({ SubgraphManifest });
    type('Chain MO2 instance folder', MO2);
    type('Chain MO2 profile', 'Test');
    type('Crash subgraph id', BIG_ID);
    fireEvent.click(screen.getByText('Read subgraph'));
    await waitFor(() => expect(screen.getByLabelText('Subgraph manifest').textContent).toContain('1 of 2 animations present'));
    expect(SubgraphManifest).toHaveBeenCalledWith(BIG_ID, '', null, '', MO2, 'Test');
    expect(screen.getByLabelText('Subgraph unresolved animations').textContent).toContain('b.hkx');
    expect(screen.getByRole('status').textContent).toContain('1 unresolved');
  });

  it('shows the notes and says no manifest was read when the id has none', async () => {
    // Constraint: detects an id with no manifest shown as a fully resolved subgraph.
    renderTools({ SubgraphManifest: vi.fn(async () => JSON.stringify({ id_source: 'given', manifest: null, notes: ['no meshes/animtextdata/animationfiledata/7.txt under C:\\Data'] })) });
    type('Chain Data folder', 'C:\\Data');
    type('Crash subgraph id', '7');
    fireEvent.click(screen.getByText('Read subgraph'));
    await waitFor(() => expect(screen.getByLabelText('Subgraph notes').textContent).toContain('no meshes/animtextdata'));
    expect(screen.getByRole('status').textContent).toContain('No manifest read');
    expect(screen.queryByLabelText('Subgraph manifest')).toBeNull();
  });
});
