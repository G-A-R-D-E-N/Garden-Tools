import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxSweepTools from './HkxSweepTools';

const DATA = 'C:\\Game\\Data';
const MO2 = 'C:\\MO2';
const BIG_ID = '10121085405136975821';

const sweepReply = (over: Record<string, unknown> = {}) => ({
  ok: true, complete: true, manifests: 4, duplicate_ids: 0, weapon_checked: 2, behaviors_read: 3, behaviors_missing: 0,
  behaviors_unreadable: 0, manifests_unread: 0, unchecked: 0, failures_total: 0, failures: [], weapon_sets: 5,
  weapon_sets_reason: '', notes: [], ...over,
});

const diffReply = (over: Record<string, unknown> = {}) => ({
  complete: true, vanilla_manifests: 3, modded_manifests: 5, new_ids_total: 2, gone_ids_total: 0, new_behaviors_total: 1,
  new_weapon_total: 1, new_ids: [BIG_ID, '222'], gone_ids: [], new_behaviors: ['actors\\new\\behaviors\\w.hkx'],
  clusters: [{ behavior: 'actors\\new\\behaviors\\w.hkx', new_manifests: 2 }], new_weapon_behaviors: ['actors\\new\\behaviors\\w.hkx'],
  notes: [], ...over,
});

const crashReply = (over: Record<string, unknown> = {}) => ({
  refusal: '', id_found: true, id: BIG_ID, matched: 'bare_number', confirmed: false, manifest_found: true,
  manifest: 'meshes/animtextdata/animationfiledata/1.txt', source: 'loose', behavior_paths: ['Actors\\Foo\\Behaviors\\B.hkx'],
  animation_total: 2, animation_present: 1, missing_total: 1, missing: ['Actors\\Foo\\Animations\\gone.hkx'], offsets_found: false,
  offsets_hint: '', weapon_subgraph: false, gap_total: 0, gaps: [], notes: [], ...over,
});

const scanReply = (over: Record<string, unknown> = {}) => ({
  mode: 'characters', ok: false, complete: true, mod_roots: 1, candidates: 2, scanned: 2, checked: 2, broken: 1, missing: 1,
  unreadable: 0, round_trip_losses: 0, findings_total: 1,
  findings: [{ kind: 'missing-animations', path: 'meshes/actors/foo/characters/foo.hkx', source: 'loose', details: ['Animations\\b.hkx'], detail_total: 1 }],
  notes: [], ...over,
});

const type = (label: string, value: string) => fireEvent.change(screen.getByLabelText(label), { target: { value } });
const pick = (label: string, value: string) => fireEvent.change(screen.getByLabelText(label), { target: { value } });
const renderTools = (host: unknown) => render(<HkxSweepTools host={host as HkxHost} disabled={false} />);
const run = () => fireEvent.click(screen.getByRole('button', { name: 'Run' }));

afterEach(() => cleanup());

describe('HkxSweepTools', () => {
  it('shows a sweep with gaps as found gaps and passes the sources to the host', async () => {
    // Constraint: detects a sweep verdict that reads a failure as clean, or sources that do not reach the host.
    const failure = { id: BIG_ID, behavior: 'Actors\\Foo\\Behaviors\\W.hkx', gap_total: 1, crash_gaps: 1,
      gaps: [{ leaf: 'Idle.hkx', message: "'Rifle' cannot resolve Idle.hkx", crash: true }] };
    const SubgraphSweep = vi.fn(async () => JSON.stringify(sweepReply({ ok: false, failures_total: 1, failures: [failure] })));
    renderTools({ SubgraphSweep });
    type('Sweep Data folder', DATA);
    type('Sweep MO2 instance folder', MO2);
    type('Sweep MO2 profile', 'Test');
    run();
    await waitFor(() => expect(screen.getByRole('status').textContent).toMatch(/^Sweep found gaps/));
    expect(SubgraphSweep).toHaveBeenCalledWith(DATA, MO2, 'Test');
    expect(screen.getByLabelText(`Sweep failure ${BIG_ID}`).textContent).toContain(BIG_ID);
    expect(screen.getByLabelText(`Sweep failure ${BIG_ID}`).textContent).toContain('crash');
  });

  it('never calls an incomplete or field-less sweep clean', async () => {
    // Constraint: detects ok true with complete false, or a reply without the complete flag, reading as a clean sweep.
    const noFlag: Record<string, unknown> = sweepReply();
    delete noFlag.complete;
    const replies = [sweepReply({ complete: false }), noFlag];
    for (const reply of replies) {
      const SubgraphSweep = vi.fn(async () => JSON.stringify(reply));
      renderTools({ SubgraphSweep });
      type('Sweep Data folder', DATA);
      run();
      await waitFor(() => expect(screen.getByRole('status').textContent).toMatch(/^Incomplete sweep, not clean/));
      cleanup();
    }
  });

  it('keeps a 64-bit diff id as the decimal string and needs an MO2 instance', async () => {
    // Constraint: detects an id turned into a number, or a diff run without an MO2 instance.
    const SubgraphDiff = vi.fn(async () => JSON.stringify(diffReply()));
    renderTools({ SubgraphDiff });
    pick('Sweep mode', 'diff');
    type('Sweep Data folder', DATA);
    expect((screen.getByRole('button', { name: 'Run' }) as HTMLButtonElement).disabled).toBe(true);
    type('Sweep MO2 instance folder', MO2);
    run();
    await waitFor(() => expect(screen.getByRole('status').textContent).toMatch(/^Diff complete/));
    expect(screen.getByLabelText('Diff new ids').textContent).toContain(BIG_ID);
    expect(SubgraphDiff).toHaveBeenCalledWith(DATA, MO2, '');
  });

  it('marks a bare-number crash id unconfirmed and lists the missing animations', async () => {
    // Constraint: detects a guessed id shown as confirmed, or the missing animations being dropped.
    const CrashSubgraph = vi.fn(async () => JSON.stringify(crashReply()));
    renderTools({ CrashSubgraph });
    pick('Sweep mode', 'crash');
    type('Sweep Data folder', DATA);
    expect((screen.getByRole('button', { name: 'Run' }) as HTMLButtonElement).disabled).toBe(true);
    type('Crash log text', `crashed near ${BIG_ID}`);
    run();
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('unconfirmed'));
    expect(screen.getByRole('status').textContent).toContain(BIG_ID);
    expect(screen.getByLabelText('Crash missing animations').textContent).toContain('gone.hkx');
    expect(CrashSubgraph).toHaveBeenCalledWith('', `crashed near ${BIG_ID}`, DATA, '', '');
  });

  it('shows a refused crash log and not an empty result', async () => {
    // Constraint: detects a refusal reading as no id found.
    const CrashSubgraph = vi.fn(async () => JSON.stringify(crashReply({ refusal: 'the crash log is 99 bytes, over the 4 byte limit', id_found: false, id: null })));
    renderTools({ CrashSubgraph });
    pick('Sweep mode', 'crash');
    type('Sweep Data folder', DATA);
    type('Crash log path', 'C:\\logs\\crash.log');
    run();
    await waitFor(() => expect(screen.getByRole('status').textContent).toMatch(/^Refused: the crash log is 99 bytes/));
  });

  it('sends the scan mode to the host and lists the findings', async () => {
    // Constraint: detects the clips mode being sent as characters, or a finding being hidden.
    const ModlistScan = vi.fn(async () => JSON.stringify(scanReply({ mode: 'clips' })));
    renderTools({ ModlistScan });
    pick('Sweep mode', 'modlist');
    pick('Modlist scan mode', 'clips');
    type('Sweep MO2 instance folder', MO2);
    run();
    await waitFor(() => expect(screen.getByRole('status').textContent).toMatch(/^Scan found problems/));
    expect(ModlistScan).toHaveBeenCalledWith('clips', '', MO2, '');
    expect(screen.getByLabelText('Scan finding missing-animations').textContent).toContain('Animations\\b.hkx');
  });

  it('reports a host that carries no sweep RPC and a host error instead of a result', async () => {
    // Constraint: detects an older host build or a host error reading as an empty clean result.
    renderTools({});
    type('Sweep Data folder', DATA);
    run();
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot sweep subgraphs'));
    cleanup();

    const SubgraphSweep = vi.fn(async () => JSON.stringify({ error: 'a data folder or an MO2 instance is required' }));
    renderTools({ SubgraphSweep });
    type('Sweep Data folder', DATA);
    run();
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('required'));
    expect(screen.queryByRole('status')).toBeNull();
  });

  it('renders N shown of M where the host cut a list, and nothing when nothing was cut', async () => {
    // Constraint: detects a truncated sweep, diff, crash or scan list shown as if it were the whole result, or a cut line shown for an uncut list.
    const failure = { id: BIG_ID, behavior: 'Actors/Foo/Behaviors/W.hkx', gap_total: 8, crash_gaps: 1,
      gaps: [{ leaf: 'Idle.hkx', message: 'gap', crash: true }] };
    const SubgraphSweep = vi.fn(async () => JSON.stringify(sweepReply({
      ok: false, complete: false, failures_total: 5, failures: [failure], truncated_failures: true, behaviors_skipped: 2 })));
    renderTools({ SubgraphSweep });
    type('Sweep Data folder', DATA);
    run();
    await waitFor(() => expect(screen.getByLabelText('Sweep cuts')).toBeTruthy());
    const cuts = screen.getByLabelText('Sweep cuts').textContent ?? '';
    expect(cuts).toContain('failures: 1 shown of 5');
    expect(cuts).toContain(`gaps of ${BIG_ID}: 1 shown of 8`);
    expect(cuts).toContain('2 behaviour path(s) not under a Behaviors folder were skipped');
    cleanup();

    // Constraint: detects an incomplete verdict whose cause (missing or unreadable file, unread manifest, cut listing) the cuts do not name.
    const reasons = vi.fn(async () => JSON.stringify(sweepReply({
      complete: false, manifests_unread: 1, behaviors_missing: 2, behaviors_unreadable: 3, truncated_failures: true, incomplete: true })));
    renderTools({ SubgraphSweep: reasons });
    type('Sweep Data folder', DATA);
    run();
    await waitFor(() => expect(screen.getByLabelText('Sweep cuts')).toBeTruthy());
    const reasonCuts = screen.getByLabelText('Sweep cuts').textContent ?? '';
    expect(reasonCuts).toContain('1 manifest(s) could not be read');
    expect(reasonCuts).toContain('2 behaviour file(s) named by a manifest are missing');
    expect(reasonCuts).toContain('3 behaviour file(s) could not be read');
    expect(reasonCuts).toContain('the failure list was cut');
    expect(reasonCuts).toContain('the file listing was incomplete');
    cleanup();

    const whole = vi.fn(async () => JSON.stringify(sweepReply()));
    renderTools({ SubgraphSweep: whole });
    type('Sweep Data folder', DATA);
    run();
    await waitFor(() => expect(screen.getByRole('status').textContent).toMatch(/^Sweep clean/));
    expect(screen.queryByLabelText('Sweep cuts')).toBeNull();
    cleanup();

    const SubgraphDiff = vi.fn(async () => JSON.stringify(diffReply({ complete: false, new_ids_total: 9, truncated_lists: true })));
    renderTools({ SubgraphDiff });
    pick('Sweep mode', 'diff');
    type('Sweep MO2 instance folder', MO2);
    run();
    await waitFor(() => expect(screen.getByLabelText('Diff cuts')).toBeTruthy());
    expect(screen.getByLabelText('Diff cuts').textContent).toContain('new ids: 2 shown of 9');
    cleanup();

    const DiffReasons = vi.fn(async () => JSON.stringify(diffReply({
      complete: false, weapon_missing: 1, weapon_unreadable: 2, manifests_unread: 3, truncated_weapon_reads: true, incomplete: true })));
    renderTools({ SubgraphDiff: DiffReasons });
    pick('Sweep mode', 'diff');
    type('Sweep MO2 instance folder', MO2);
    run();
    await waitFor(() => expect(screen.getByLabelText('Diff cuts')).toBeTruthy());
    const diffReasonCuts = screen.getByLabelText('Diff cuts').textContent ?? '';
    expect(diffReasonCuts).toContain('1 new behaviour file(s) are missing');
    expect(diffReasonCuts).toContain('2 new behaviour file(s) could not be read');
    expect(diffReasonCuts).toContain('3 manifest(s) could not be read');
    expect(diffReasonCuts).toContain('the weapon read limit was reached');
    expect(diffReasonCuts).toContain('the file listing was incomplete');
    cleanup();

    const CrashSubgraph = vi.fn(async () => JSON.stringify(crashReply({
      missing_total: 30, gap_total: 9, gaps: [{ leaf: 'a', message: 'm', crash: false }], unchecked_behaviors: ['Actors/Foo/Behaviors/B.hkx: missing'] })));
    renderTools({ CrashSubgraph });
    pick('Sweep mode', 'crash');
    type('Sweep Data folder', DATA);
    type('Crash log text', 'x');
    run();
    await waitFor(() => expect(screen.getByLabelText('Crash cuts')).toBeTruthy());
    const crashCutText = screen.getByLabelText('Crash cuts').textContent ?? '';
    expect(crashCutText).toContain('missing animations: 1 shown of 30');
    expect(crashCutText).toContain('weapon gaps: 1 shown of 9');
    expect(crashCutText).toContain('not checked: Actors/Foo/Behaviors/B.hkx: missing');
    cleanup();

    const ModlistScan = vi.fn(async () => JSON.stringify(scanReply({
      complete: false, findings_total: 7, truncated_findings: true, truncated_files: true, scanned: 2, candidates: 50,
      findings: [{ kind: 'missing-animations', path: 'meshes/a.hkx', source: 'loose', details: ['x'], detail_total: 40 }] })));
    renderTools({ ModlistScan });
    pick('Sweep mode', 'modlist');
    type('Sweep MO2 instance folder', MO2);
    run();
    await waitFor(() => expect(screen.getByLabelText('Scan cuts')).toBeTruthy());
    const scanCutText = screen.getByLabelText('Scan cuts').textContent ?? '';
    expect(scanCutText).toContain('findings: 1 shown of 7');
    expect(scanCutText).toContain('details of meshes/a.hkx: 1 shown of 40');
    expect(scanCutText).toContain('files: 2 scanned of 50');
    expect(scanCutText).toContain('the findings list was cut at the host limit');
    cleanup();

    const ScanReasons = vi.fn(async () => JSON.stringify(scanReply({ complete: false, unreadable: 4, incomplete: true })));
    renderTools({ ModlistScan: ScanReasons });
    pick('Sweep mode', 'modlist');
    type('Sweep MO2 instance folder', MO2);
    run();
    await waitFor(() => expect(screen.getByLabelText('Scan cuts')).toBeTruthy());
    const scanReasonCuts = screen.getByLabelText('Scan cuts').textContent ?? '';
    expect(scanReasonCuts).toContain('4 file(s) could not be read');
    expect(scanReasonCuts).toContain('the file listing was incomplete');
  });
});
