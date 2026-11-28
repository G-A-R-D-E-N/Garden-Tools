import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxInspectTools from './HkxInspectTools';

const PATH = 'C:\\mods\\asset.hkx';
const NOTE = 'values are raw hex and objects are addressed by packfile index';

const link = (over: Record<string, unknown>) => ({
  source_object: 0, source_offset: 0, member: '', target_section: 2, target_offset: 0, target_object: -1, target_class: '', inner: false, ...over,
});

const detail = {
  object: 1, object_count: 3, signature: 'test-signature', class_name: 'TestChild', layout_known: true,
  outgoing: [link({ source_object: 1, member: 'label', target_offset: 5, inner: true })], outgoing_total: 1,
  incoming: [link({ source_object: 0, member: 'children', target_object: 1, target_class: 'TestChild' })], incoming_total: 1,
  fields: [
    { name: 'label', offset: 16, size: 8, hex: 'aa', type: 'stringptr', value: '"Idle"', reason: '' },
    { name: 'localTime', offset: 320, size: 4, hex: '0000 1041', type: 'real', value: '', reason: 'not stored in files' },
    { name: 'legacy', offset: 324, size: 4, hex: 'bb' },
    { name: 'probe', offset: 336, size: 4, hex: 'cc', type: 'testtype', value: '{flag=?, time=1}', reason: 'flag: test reason' },
  ], field_total: 4,
  truncated: false, partial: true, note: NOTE,
};

const row = (shape: Record<string, unknown>) => ({ name: '', animation: '', cut: false, repeated: false, ...shape });

const tree = {
  object_count: 2, roots: [0], unreachable: 0, truncated: false, partial: true, note: NOTE,
  nodes: [
    row({ node: 0, object: 0, parent: -1, depth: 0, class_name: 'TestOwner', member: '' }),
    row({ node: 1, object: 1, parent: 0, depth: 1, class_name: 'TestChild', member: 'children', summary: 'name="Idle" animationName="walk.hkx"' }),
    row({ node: 2, object: 0, parent: 1, depth: 2, class_name: 'TestOwner', member: 'back', repeated: true }),
  ],
};

const hostWith = (method: string, reply: unknown) => ({ [method]: vi.fn(async () => JSON.stringify(reply)) } as unknown as HkxHost);

const choose = (label: string) => fireEvent.change(screen.getByLabelText('Inspection'), { target: { value: label } });
const inspect = () => fireEvent.click(screen.getByText('Inspect', { selector: 'button' }));

afterEach(() => cleanup());

describe('HkxInspectTools', () => {
  it('lists the incoming reference to an object, shows an inner pointer as inside its own span and says values are partial', async () => {
    // Constraint: detects a string pointer listed as a reference to another object, an incoming reference left out, or raw hex presented as a full decode.
    render(<HkxInspectTools host={hostWith('InspectObject', detail)} initialPath={PATH} disabled={false} />);
    fireEvent.change(screen.getByLabelText('Object id'), { target: { value: '1' } });
    inspect();
    await waitFor(() => expect(screen.getByLabelText('Incoming references')).toBeTruthy());
    expect(screen.getByLabelText('Incoming references').textContent).toContain('from object 0: children -> object 1 (TestChild)');
    expect(screen.getByLabelText('Outgoing pointers').textContent).toContain('inside its own span at 5');
    expect(screen.getByLabelText('Inspection note').textContent).toContain('raw hex');
    expect(screen.getByRole('status').textContent).toContain('1 incoming reference');
  });

  it('shows a decoded value next to its hex and the reason for a field that is not decoded', async () => {
    // Constraint: detects decoded values dropped from the object view, an undecoded field shown with no reason, or a partly decoded struct shown without its reason.
    render(<HkxInspectTools host={hostWith('InspectObject', detail)} initialPath={PATH} disabled={false} />);
    fireEvent.change(screen.getByLabelText('Object id'), { target: { value: '1' } });
    inspect();
    await waitFor(() => expect(screen.getByLabelText('Object fields')).toBeTruthy());
    const fields = screen.getByLabelText('Object fields').querySelectorAll('li');
    expect(fields[0].textContent).toContain('label +16: "Idle" (aa)');
    expect(fields[1].textContent).toContain('localTime +320: 0000 1041 (not stored in files)');
    expect(fields[2].textContent).toContain('legacy +324: bb');
    expect(fields[3].textContent).toContain('probe +336: {flag=?, time=1} (cc) (flag: test reason)');
  });

  it('marks a repeated object in the tree instead of listing it as a new subtree', async () => {
    // Constraint: detects a cyclic object graph shown as an endless or duplicated tree.
    render(<HkxInspectTools host={hostWith('ObjectTree', tree)} initialPath={PATH} disabled={false} />);
    choose('tree');
    inspect();
    await waitFor(() => expect(screen.getByLabelText('Object tree')).toBeTruthy());
    const rows = screen.getByLabelText('Object tree').querySelectorAll('li');
    expect(rows).toHaveLength(3);
    expect(rows[2].textContent).toContain('(shown above)');
    expect(rows[1].textContent).toContain('name="Idle" animationName="walk.hkx"');
    expect(screen.getByLabelText('Inspection note')).toBeTruthy();
  });

  it('lists search hits with their kind, file and object and the packfile classes with their issues', async () => {
    // Constraint: detects a search or packfile reply that is fetched but dropped from the view.
    const search = { file_count: 1, skipped: 0, hit_count: 1, truncated: false,
      hits: [{ file: 'a.hkx', kind: 'event', text: 'TestEvent', object: 4 }] };
    const first = render(<HkxInspectTools host={hostWith('SearchHkx', search)} initialPath={PATH} disabled={false} />);
    choose('search');
    fireEvent.change(screen.getByLabelText('Search text'), { target: { value: 'test' } });
    inspect();
    await waitFor(() => expect(screen.getByLabelText('Search hits')).toBeTruthy());
    expect(screen.getByLabelText('Search hits').textContent).toContain('[event] TestEvent - a.hkx #4');
    first.unmount();

    const packfile = { readable: true, object_count: 2, issues: ['TestChild object is shorter than its layout'],
      classes: [{ name: 'TestChild', signature: 'test-signature', count: 2, layout_status: 'signature_mismatch' }] };
    render(<HkxInspectTools host={hostWith('PackfileDiagnostics', packfile)} initialPath={PATH} disabled={false} />);
    choose('packfile');
    inspect();
    await waitFor(() => expect(screen.getByLabelText('Packfile issues')).toBeTruthy());
    expect(screen.getByLabelText('Packfile issues').textContent).toContain('shorter than its layout');
    expect(screen.getByLabelText('Packfile classes').textContent).toContain('TestChild x2: signature mismatch');
  });

  it('shows the first difference of a repack that reads back differently and stays quiet for a clean or writer-refused file', async () => {
    // Constraint: detects a differing round trip reported as clean, or a writer-refused layout shown as corruption.
    const reply = (over: Record<string, unknown>) => ({
      verdict: 'equal', model_equal: true, status: 'Ok', bytes_identical: false, source_size: 8, rewritten_size: 8, first_difference: '', ...over,
    });
    const differs = render(<HkxInspectTools host={hostWith('RepackCheck', reply({
      verdict: 'differs', model_equal: false, first_difference: 'section 2 data differs' }))} initialPath={PATH} disabled={false} />);
    choose('repack');
    inspect();
    await waitFor(() => expect(screen.getByLabelText('Repack check')).toBeTruthy());
    expect(screen.getByLabelText('Repack check').textContent).toContain('section 2 data differs');
    differs.unmount();

    const refused = render(<HkxInspectTools host={hostWith('RepackCheck', reply({ verdict: 'refused', model_equal: false, status: 'UnsupportedLayout' }))}
      initialPath={PATH} disabled={false} />);
    choose('repack');
    inspect();
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('not a corruption'));
    expect(screen.queryByLabelText('Repack check')).toBeNull();
    refused.unmount();

    render(<HkxInspectTools host={hostWith('RepackCheck', reply({}))} initialPath={PATH} disabled={false} />);
    choose('repack');
    inspect();
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('same packfile'));
    expect(screen.queryByLabelText('Repack check')).toBeNull();
  });

  it('lists the files an audit could not pose with the reason instead of reporting them clean', async () => {
    // Constraint: detects a skipped clip counted as an audited clip with no undriven channels.
    const reply = {
      skeleton: 'rig.hkx', file_count: 2, audited: 1, with_offsets: 0, skipped: 1, truncated: false,
      files: [{ file: 'a.hkx', tracks: 2, offset_translation: 0, offset_rotation: 0, offset_scale: 0, max_translation_offset: 0,
                max_rotation_degrees: 0, offsets: [], offset_total: 0 }],
      skipped_files: [{ file: 'b.hkx', reason: 'view clip is not supported' }],
    };
    render(<HkxInspectTools host={hostWith('AuditChannels', reply)} initialPath={PATH} disabled={false} />);
    choose('channels');
    fireEvent.change(screen.getByLabelText('Animation file or folder'), { target: { value: 'C:\\mods\\anim' } });
    inspect();
    await waitFor(() => expect(screen.getByLabelText('Skipped clips')).toBeTruthy());
    expect(screen.getByLabelText('Skipped clips').textContent).toContain('b.hkx: view clip is not supported');
    expect(screen.getByRole('status').textContent).toContain('1 skipped');
  });

  it('says a usage report is partial so an event with no decoded site is not read as proven unused', async () => {
    // Constraint: detects the partial-decode note being dropped from the symbol usage view.
    const reply = {
      events: [
        { event_id: 0, name: 'TestEvent', in_range: true, no_decoded_site: true, site_total: 0, sites: [] },
        { event_id: 1, name: 'ModEvent', in_range: true, no_decoded_site: false, site_total: 1,
          sites: [{ site: 'object_member', node: -1, state: -1, owner: 'hkbEventDrivenModifier#7', object: 7, member: 'activateEvent' }] },
      ],
      variables: [], states: [], partial: true, note: 'modifier event sites are not decoded; run hkx_behavior_validate',
    };
    render(<HkxInspectTools host={hostWith('SymbolUsage', reply)} initialPath={PATH} disabled={false} />);
    choose('usage');
    inspect();
    await waitFor(() => expect(screen.getByLabelText('Usage note')).toBeTruthy());
    expect(screen.getByLabelText('Usage note').textContent).toContain('not decoded');
    expect(screen.getByLabelText('Event usage').textContent).toContain('TestEvent: no decoded site');
    expect(screen.getByLabelText('Event usage').textContent).toContain('ModEvent: object_member hkbEventDrivenModifier#7 activateEvent');
    expect(screen.getByRole('status').textContent).not.toContain('unused');
  });

  it('shows the engine refusal, a null reply and an older host as alerts instead of an empty result', async () => {
    // Constraint: detects an old host, a null reply or a refused file shown as an object with no references.
    const refused = { SearchHkx: vi.fn(async () => JSON.stringify({ error: 'HKX not found: C:\\mods\\asset.hkx' })) } as unknown as HkxHost;
    const first = render(<HkxInspectTools host={refused} initialPath={PATH} disabled={false} />);
    choose('search');
    fireEvent.change(screen.getByLabelText('Search text'), { target: { value: 'go' } });
    inspect();
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('HKX not found'));
    first.unmount();

    const second = render(<HkxInspectTools host={hostWith('PackfileDiagnostics', null)} initialPath={PATH} disabled={false} />);
    choose('packfile');
    inspect();
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('no inspection reply'));
    second.unmount();

    render(<HkxInspectTools host={{} as HkxHost} initialPath={PATH} disabled={false} />);
    choose('packfile');
    inspect();
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot diagnose an HKX packfile'));
  });

  it('clears the second argument when the mode changes so a search text is not sent as an object id', () => {
    // Constraint: detects stale text from one mode surviving into the next mode's argument.
    render(<HkxInspectTools host={{} as HkxHost} initialPath={PATH} disabled={false} />);
    fireEvent.change(screen.getByLabelText('Object id'), { target: { value: '7' } });
    choose('search');
    expect((screen.getByLabelText('Search text') as HTMLInputElement).value).toBe('');
  });
});
