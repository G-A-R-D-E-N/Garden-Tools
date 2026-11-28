import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxCharacterTools from './HkxCharacterTools';

const report = (over: Record<string, unknown> = {}) => ({
  ok: false, errors: 1, warnings: 0,
  skeleton: { name: 'Rig', bones: 3, mirror: { pairs: 1, lane: 0 } },
  ragdoll: { present: true, status: 'ok', headline: '', bodies: 2, constraints: 1, bone_bindings: 2 },
  mappers: [{ object_index: 4, rows: 3, chains: 1 }],
  skin: {
    status: 'bound',
    shapes: [{
      binding: { block: 2, name: 'Body', vertices: 10, unmatched: 0, complete: true, warning: '' },
      skin_errors: 0, skin_warnings: 0, unbound_vertices: 1,
      repairs: [{ kind: 'normalize_weights', block: 2, vertices: [3, 4], total: 2, reason: 'weights do not sum to one' }],
    }],
    refused: [], unskinned: 0,
  },
  findings: [{ level: 'error', area: 'skeleton', where: 'bone 2', message: 'duplicate name' }],
  ...over,
});

const plan = (over: Record<string, unknown> = {}) => ({
  ok: true,
  steps: [{ document: 0, kind: 'skeleton', reads: 'rig.hkx', writes: 'rig2.hkx', reads_previous_output: false, source_sha256: 'aa11' }],
  findings: [], shifted_references: 0, ...over,
});

const savedResult = (over: Record<string, unknown> = {}) => ({
  saved: true, refusal: '', partial: false, failed_stage: 0, written: ['rig2.hkx', 'ragdoll2.hkx'],
  steps: [], findings: [], shifted_references: 0, ...over,
});

const type = (label: string, value: string) => fireEvent.change(screen.getByLabelText(label), { target: { value } });
const renderTools = (host: unknown) => render(<HkxCharacterTools host={host as HkxHost} disabled={false} />);

afterEach(() => cleanup());

describe('HkxCharacterTools', () => {
  it('passes the five paths to the host and lists findings and skin repairs', async () => {
    // Constraint: detects a panel that drops a path, hides a finding, or shows a repair as applied.
    const CharacterCheck = vi.fn(async () => JSON.stringify(report()));
    renderTools({ CharacterCheck });
    type('Character skeleton hkx', ' C:\\rig.hkx ');
    type('Character body nif', 'C:\\body.nif');
    fireEvent.click(screen.getByText('Check character'));
    await waitFor(() => expect(screen.getByLabelText('Character findings').textContent).toContain('duplicate name'));
    expect(CharacterCheck).toHaveBeenCalledWith('C:\\rig.hkx', '', '', 'C:\\body.nif', '', '', '', '', '');
    expect(screen.getByLabelText('Character skin repairs').textContent).toContain('normalize_weights');
    expect(screen.getByLabelText('Character verdict').textContent).toBe('Not clean');
    expect(screen.getByRole('status').textContent).toContain('Character has errors');
  });

  it('never calls a report with an error finding clean, even when the host flag says ok', async () => {
    // Constraint: detects a verdict taken from ok alone, which a host that drops its counts would turn into a false OK.
    renderTools({ CharacterCheck: vi.fn(async () => JSON.stringify(report({ ok: true, errors: 0 }))) });
    type('Character skeleton hkx', 'rig.hkx');
    fireEvent.click(screen.getByText('Check character'));
    await waitFor(() => expect(screen.getByLabelText('Character verdict').textContent).toBe('Not clean'));
  });

  it('reports a host without the RPC and a host error instead of an empty report', async () => {
    // Constraint: detects an old host or a failed call shown as a clean empty character.
    const { unmount } = renderTools({});
    type('Character skeleton hkx', 'rig.hkx');
    fireEvent.click(screen.getByText('Check character'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot check a character'));
    expect(screen.queryByLabelText('Character verdict')).toBeNull();
    unmount();
    renderTools({ CharacterCheck: vi.fn(async () => JSON.stringify({ error: 'Skeleton not found' })) });
    type('Character skeleton hkx', 'rig.hkx');
    fireEvent.click(screen.getByText('Check character'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('Skeleton not found'));
  });

  it('plans a save with the out paths and edits, and says nothing is written', async () => {
    // Constraint: detects a plan view that drops a target, the edits text or the refusal, or claims a write.
    const CharacterSavePlan = vi.fn(async () => JSON.stringify(plan()));
    renderTools({ CharacterSavePlan });
    type('Character skeleton hkx', 'rig.hkx');
    type('Plan skeleton out', 'rig2.hkx');
    type('Plan skeleton edits', '[{"kind":"rename","bone":2,"name":"Skull"}]');
    fireEvent.click(screen.getByText('Plan save'));
    await waitFor(() => expect(screen.getByLabelText('Plan steps').textContent).toContain('rig.hkx -> rig2.hkx'));
    expect(CharacterSavePlan).toHaveBeenCalledWith('rig.hkx', 'rig2.hkx', '', '', '', '', '', '',
                                                   '[{"kind":"rename","bone":2,"name":"Skull"}]', false);
    expect(screen.getByRole('status').textContent).toContain('nothing written');
  });

  it('shows a refused plan with its findings', async () => {
    // Constraint: detects a refused plan shown as ready.
    const refused = plan({ ok: false, steps: [], findings: [{ level: 'error', area: 'ragdoll', where: 'out', message: 'needs an out path' }] });
    renderTools({ CharacterSavePlan: vi.fn(async () => JSON.stringify(refused)) });
    type('Character ragdoll hkx', 'ragdoll.hkx');
    fireEvent.click(screen.getByText('Plan save'));
    await waitFor(() => expect(screen.getByLabelText('Plan verdict').textContent).toBe('Plan refused'));
    expect(screen.getByLabelText('Plan findings').textContent).toContain('needs an out path');
  });

  it('passes the project, data folder and MO2 fields and lists the animation links', async () => {
    // Constraint: detects a panel that drops the project fields, or shows an unchecked chain as checked.
    const chain = { checked: true, root: 'Actors/Foo/Foo.hkx', links: 4, animations: 3, missing_animations: 1, problems: ['x'], warnings: [] };
    const CharacterCheck = vi.fn(async () => JSON.stringify(report({ chain })));
    renderTools({ CharacterCheck });
    type('Character skeleton hkx', 'rig.hkx');
    expect(screen.queryByLabelText('Character animation links')).toBeNull();
    type('Character project hkx', ' Actors/Foo/Foo.hkx ');
    type('Character data folder', 'C:\\Data');
    fireEvent.click(screen.getByText('Check character'));
    await waitFor(() => expect(screen.getByLabelText('Character animation links').textContent).toContain('3 clip(s), 1 missing'));
    expect(CharacterCheck).toHaveBeenCalledWith('rig.hkx', '', '', '', '', 'Actors/Foo/Foo.hkx', 'C:\\Data', '', '');
  });

  it('says the links were not checked when the host reports no chain', async () => {
    // Constraint: detects an absent chain shown as zero missing animations.
    renderTools({ CharacterCheck: vi.fn(async () => JSON.stringify(report())) });
    type('Character skeleton hkx', 'rig.hkx');
    fireEvent.click(screen.getByText('Check character'));
    await waitFor(() => expect(screen.getByLabelText('Character animation links').textContent).toContain('not checked'));
  });

  it('keeps Save disabled until a plan is ready for the current fields and sends every stage to the host', async () => {
    // Constraint: detects a Save that is enabled before a preview, after a refused plan, or after a field changed.
    const CharacterSavePlan = vi.fn(async () => JSON.stringify(plan()));
    const CharacterSave = vi.fn(async () => JSON.stringify(savedResult()));
    renderTools({ CharacterSavePlan, CharacterSave });
    const saveButton = () => screen.getByText('Save') as HTMLButtonElement;
    type('Character skeleton hkx', 'rig.hkx');
    type('Plan skeleton out', 'rig2.hkx');
    type('Character ragdoll hkx', 'ragdoll.hkx');
    type('Plan ragdoll out', 'ragdoll2.hkx');
    type('Plan ragdoll edits', '{"bodies":[]}');
    type('Plan mapper index', '2');
    fireEvent.click(screen.getByLabelText('Plan overwrite'));
    expect(saveButton().disabled).toBe(true);
    fireEvent.click(screen.getByText('Plan save'));
    await waitFor(() => expect(saveButton().disabled).toBe(false));
    type('Plan skeleton out', 'rig3.hkx');
    expect(saveButton().disabled).toBe(true);
    type('Plan skeleton out', 'rig2.hkx');
    expect(saveButton().disabled).toBe(false);
    fireEvent.click(saveButton());
    await waitFor(() => expect(screen.getByLabelText('Save verdict').textContent).toBe('Saved 2 file(s)'));
    expect(CharacterSave).toHaveBeenCalledWith('rig.hkx', 'rig2.hkx', '', 'aa11', 'ragdoll.hkx', 'ragdoll2.hkx', '{"bodies":[]}', '',
                                               '', '', '', '', 2, '', '', '', '', true);
    expect(screen.getByLabelText('Files written').textContent).toContain('ragdoll2.hkx');
    expect(saveButton().disabled).toBe(true);
  });

  it('sends back the source hash of each document the plan hashed and none for a step that reads a previous output', async () => {
    // Constraint: detects a Save that drops the plan's hashes, or sends a hash for a file the plan had not read from disk.
    const chained = plan({
      steps: [
        { document: 0, kind: 'skeleton', reads: 'rig.hkx', writes: 'rig2.hkx', reads_previous_output: false, source_sha256: 'aa11' },
        { document: 1, kind: 'ragdoll', reads: 'rig2.hkx', writes: 'ragdoll2.hkx', reads_previous_output: true, source_sha256: 'ee55' },
        { document: 2, kind: 'skin', reads: 'body.nif', writes: 'body2.nif', reads_previous_output: false, source_sha256: 'cc33' },
      ],
    });
    const CharacterSave = vi.fn(async () => JSON.stringify(savedResult()));
    renderTools({ CharacterSavePlan: vi.fn(async () => JSON.stringify(chained)), CharacterSave });
    type('Character skeleton hkx', 'rig.hkx');
    type('Plan skeleton out', 'rig2.hkx');
    fireEvent.click(screen.getByText('Plan save'));
    await waitFor(() => expect((screen.getByText('Save') as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByText('Save'));
    await waitFor(() => expect(CharacterSave).toHaveBeenCalled());
    const args = CharacterSave.mock.calls[0] as unknown as unknown[];
    expect([args[3], args[7], args[11], args[16]]).toEqual(['aa11', '', '', 'cc33']);
  });

  it('does not enable Save for a refused plan', async () => {
    // Constraint: detects a Save enabled after the plan was refused.
    renderTools({ CharacterSavePlan: vi.fn(async () => JSON.stringify(plan({ ok: false, steps: [] }))), CharacterSave: vi.fn() });
    type('Character skeleton hkx', 'rig.hkx');
    fireEvent.click(screen.getByText('Plan save'));
    await waitFor(() => expect(screen.getByLabelText('Plan verdict').textContent).toBe('Plan refused'));
    expect((screen.getByText('Save') as HTMLButtonElement).disabled).toBe(true);
  });

  it('shows a failure midway with the files already written', async () => {
    // Constraint: detects a partial failure shown as a clean refusal or a success, or hidden written files.
    const partial = savedResult({
      saved: false, partial: true, failed_stage: 1, written: ['rig2.hkx'],
      refusal: 'step 2 (ragdoll) failed: not saved; already written: rig2.hkx',
    });
    renderTools({ CharacterSavePlan: vi.fn(async () => JSON.stringify(plan())), CharacterSave: vi.fn(async () => JSON.stringify(partial)) });
    type('Character skeleton hkx', 'rig.hkx');
    fireEvent.click(screen.getByText('Plan save'));
    await waitFor(() => expect((screen.getByText('Save') as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByText('Save'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('already written: rig2.hkx'));
    expect(screen.getByLabelText('Save verdict').textContent).toContain('stopped after writing 1 file(s)');
    expect(screen.getByLabelText('Files written').textContent).toContain('rig2.hkx');
    expect(screen.queryByRole('status')?.textContent ?? '').not.toContain('Saved');
  });

  it('reports a host without the save RPC instead of saving', async () => {
    // Constraint: detects an old host shown as a saved character.
    renderTools({ CharacterSavePlan: vi.fn(async () => JSON.stringify(plan())) });
    type('Character skeleton hkx', 'rig.hkx');
    fireEvent.click(screen.getByText('Plan save'));
    await waitFor(() => expect((screen.getByText('Save') as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByText('Save'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot save a character'));
  });

  it('refuses a mapper index that is not a whole number before calling the host', async () => {
    // Constraint: detects a negative index passed to the host, or '1.5' and '1abc' saved as mapper 1.
    const CharacterSave = vi.fn();
    renderTools({ CharacterSavePlan: vi.fn(async () => JSON.stringify(plan())), CharacterSave });
    type('Character skeleton hkx', 'rig.hkx');
    for (const bad of ['-1', '1.5', '1abc', '2147483648']) {
      type('Plan mapper index', bad);
      fireEvent.click(screen.getByText('Plan save'));
      await waitFor(() => expect((screen.getByText('Save') as HTMLButtonElement).disabled).toBe(false));
      fireEvent.click(screen.getByText('Save'));
      await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('mapper index must be a whole number'));
    }
    expect(CharacterSave).not.toHaveBeenCalled();
  });
});
