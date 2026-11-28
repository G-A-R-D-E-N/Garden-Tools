import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxRagdollTools from './HkxRagdollTools';
import { buildPreviewOptions, buildSaveEdits } from './hkxRagdollClient';
import { buildRagdollScene, ragdollGeometry, type RagdollScene } from './hkxRagdollScene';

const PATH = 'C:\\Data\\Meshes\\Actors\\Character\\Ragdoll.hkx';

const ragdoll = {
  loaded: true,
  bodies: [{ id: 0, name: ' Root Body ', position: [0, 0, 0] }, { id: 1, name: 'Child Body', position: [0, 0, 1] }],
  constraints: [{
    id: 0, kind: 'ragdoll', body_a: 1, body_b: 0, min_angle: 0, max_angle: 0,
    twist_min_angle: -0.5, twist_max_angle: 0.75, cone_max_angle: 0.625,
  }],
};

const previewReply = {
  steps: 120,
  frames: [{ step: 0, bodies: [] }],
  bodies: [],
  constraints: [{
    constraint: 0, kind: 'ragdoll', anchor_error: 0.001, twist_angle: 0.31, cone_angle: 0.2, hinge_angle: null,
    violation: 0, overridden: true,
  }],
};

const hostWith = (extra: Record<string, unknown> = {}) => ({
  ReadRagdoll: vi.fn(async () => JSON.stringify(ragdoll)),
  PreviewRagdoll: vi.fn(async () => JSON.stringify(previewReply)),
  SaveRagdoll: vi.fn(async () => JSON.stringify({ saved: true, path: 'out.hkx', sha256: 'x' })),
  ...extra,
}) as unknown as HkxHost & Record<'ReadRagdoll' | 'PreviewRagdoll' | 'SaveRagdoll', ReturnType<typeof vi.fn>>;

async function loaded(host: HkxHost) {
  render(<HkxRagdollTools host={host} disabled={false} />);
  fireEvent.change(screen.getByLabelText('Ragdoll path'), { target: { value: PATH } });
  fireEvent.click(screen.getByText('Load ragdoll'));
  await waitFor(() => expect(screen.getByLabelText('Ragdoll constraints').textContent).toContain('constraint 0'));
}

const pick = () => fireEvent.click(screen.getByText(/constraint 0 \(ragdoll\)/));
const setLimit = (field: string, value: string) => fireEvent.change(screen.getByLabelText(field), { target: { value } });

afterEach(() => cleanup());

describe('HkxRagdollTools', () => {
  it('keeps a preview limit override out of the save payload', async () => {
    // Constraint: detects a preview-only limit that is written to the saved file, which would persist an experiment.
    const host = hostWith();
    await loaded(host);
    pick();
    setLimit('twist_max', '0.2');
    fireEvent.click(screen.getByText('Preview limit'));
    fireEvent.click(screen.getByText('Run preview'));
    await waitFor(() => expect(screen.getByLabelText('Preview joints').textContent).toContain('preview override'));

    const options = JSON.parse(host.PreviewRagdoll.mock.calls[0][1] as string);
    expect(options.limit_overrides).toEqual([{ constraint: 0, twist_min: -0.5, twist_max: 0.2, cone_max: 0.625 }]);
    expect(screen.getByLabelText('Edit counts').textContent).toContain('1 preview override(s), 0 staged edit(s)');

    fireEvent.change(screen.getByLabelText('Save as path'), { target: { value: 'C:\\out\\edited.hkx' } });
    expect((screen.getByText('Save as') as HTMLButtonElement).disabled).toBe(true);

    setLimit('twist_max', '0.3');
    fireEvent.click(screen.getByText('Stage edit'));
    fireEvent.click(screen.getByText('Save as'));
    await waitFor(() => expect(host.SaveRagdoll).toHaveBeenCalledTimes(1));
    const [source, out, editsText, overwrite] = host.SaveRagdoll.mock.calls[0];
    expect(source).toBe(PATH);
    expect(out).toBe('C:\\out\\edited.hkx');
    expect(overwrite).toBe(false);
    const edits = JSON.parse(editsText as string);
    expect(edits.constraints).toEqual([{ constraint: 0, twist_max: 0.3 }]);
    expect(editsText).not.toContain('0.2');
  });

  it('builds the preview options and the save edits from separate lists', () => {
    // Constraint: detects builders that merge the two lists, so staging an edit would change a preview and the reverse.
    const overrides = { 0: { twist_max: 0.2 } };
    const staged = { 1: { hinge_max: 0.4 } };
    expect(buildPreviewOptions(overrides, 60, 12).limit_overrides).toEqual([{ constraint: 0, twist_max: 0.2 }]);
    expect(buildSaveEdits(staged).constraints).toEqual([{ constraint: 1, hinge_max: 0.4 }]);
    expect(buildSaveEdits({})).toEqual({ constraints: [] });
  });

  it('stages a capsule resize along its stored axis and a material edit into the save payload', async () => {
    // Constraint: detects a resize that moves the capsule centre or turns its axis, and a staged shape or material edit that never reaches the save.
    const withShapes = {
      ...ragdoll,
      capsules: [
        { shape: 3, a: [0, 0, -1], b: [0, 0, 1], radius: 0.25, editable: true, reason: '' },
        { shape: 4, a: [0, 0, 0], b: [0, 1, 0], radius: 0.1, editable: false, reason: 'the stored volume differs' },
      ],
      materials: [{ material: 2, dynamic_friction: 0.59765625, static_friction: 0.59765625, restitution: 0.19921875, shared: true }],
    };
    const host = hostWith({ ReadRagdoll: vi.fn(async () => JSON.stringify(withShapes)) });
    await loaded(host);
    expect(screen.getByLabelText('Ragdoll capsules').textContent).toContain('not editable: the stored volume differs');

    fireEvent.click(screen.getByText(/capsule 3 length 2.000/));
    fireEvent.change(screen.getByLabelText('capsule length'), { target: { value: '1' } });
    fireEvent.change(screen.getByLabelText('capsule radius'), { target: { value: '0.5' } });
    fireEvent.click(screen.getByText('Stage capsule'));

    fireEvent.click(screen.getByText(/material 2 \(shared\)/));
    fireEvent.change(screen.getByLabelText('restitution'), { target: { value: '0.05' } });
    fireEvent.click(screen.getByLabelText('Change every body that uses this material'));
    fireEvent.click(screen.getByText('Stage material'));
    expect(screen.getByLabelText('Edit counts').textContent).toContain('2 staged edit(s)');

    fireEvent.change(screen.getByLabelText('Save as path'), { target: { value: 'out.hkx' } });
    fireEvent.click(screen.getByText('Save as'));
    await waitFor(() => expect(host.SaveRagdoll).toHaveBeenCalledTimes(1));
    const edits = JSON.parse(host.SaveRagdoll.mock.calls[0][2] as string);
    expect(edits.shapes).toEqual([{ shape: 3, a: [0, 0, -0.5], b: [0, 0, 0.5], radius: 0.5 }]);
    expect(edits.materials).toEqual([{ material: 2, restitution: 0.05, allow_shared: true }]);
    expect(edits.constraints).toEqual([]);
  });

  it('drops the staged shape edits and the open capsule and material controls when another file loads', async () => {
    // Constraint: detects a capsule or material draft from the previous file that is staged against the next file.
    const first = {
      ...ragdoll,
      capsules: [{ shape: 3, a: [0, 0, -1], b: [0, 0, 1], radius: 0.25, editable: true, reason: '' }],
      materials: [{ material: 2, dynamic_friction: 0.5, static_friction: 0.5, restitution: 0.2, shared: false }],
    };
    const host = hostWith({ ReadRagdoll: vi.fn(async () => JSON.stringify(first)) });
    await loaded(host);
    fireEvent.click(screen.getByText(/capsule 3 length 2.000/));
    fireEvent.change(screen.getByLabelText('capsule radius'), { target: { value: '0.5' } });
    fireEvent.click(screen.getByText('Stage capsule'));
    fireEvent.click(screen.getByText(/material 2/));
    expect(screen.getByLabelText('Edit counts').textContent).toContain('1 staged edit(s)');

    host.ReadRagdoll.mockImplementation(async () => JSON.stringify({ ...first, capsules: [{ ...first.capsules[0], radius: 0.3 }] }));
    fireEvent.click(screen.getByText('Load ragdoll'));
    await waitFor(() => expect(screen.getByLabelText('Edit counts').textContent).toContain('0 staged edit(s)'));
    expect(screen.queryByLabelText('capsule radius')).toBeNull();
    expect(screen.queryByLabelText('Material editing')).toBeNull();
  });

  it('shows no capsule or material controls for a reply without them and omits empty shape lists from the save', async () => {
    // Constraint: detects an older host reply that crashes the panel, or a save payload that carries empty shape rows.
    await loaded(hostWith());
    expect(screen.queryByLabelText('Shape and material editing')).toBeNull();
    expect(buildSaveEdits({}, { capsules: {}, materials: {} })).toEqual({ constraints: [] });
  });

  it('names a joint that ran past its limit instead of showing a clean preview', async () => {
    // Constraint: detects a preview summary that hides a limit violation reported by the engine.
    const broken = { ...previewReply, constraints: [{ ...previewReply.constraints[0], violation: 0.12 }] };
    const host = hostWith({ PreviewRagdoll: vi.fn(async () => JSON.stringify(broken)) });
    await loaded(host);
    fireEvent.click(screen.getByText('Run preview'));
    await waitFor(() => expect(screen.getByLabelText('Preview joints').textContent).toContain('over its limit by 0.120 rad'));
    expect(screen.getByRole('status').textContent).toContain('nothing was saved');
  });

  it('shows the engine refusal for a save and a preview', async () => {
    // Constraint: detects a refused save or preview that is reported as done.
    const host = hostWith({
      SaveRagdoll: vi.fn(async () => JSON.stringify({ error: 'RagdollRefused: shape_geometry_unsupported; nothing was written' })),
      PreviewRagdoll: vi.fn(async () => JSON.stringify({ error: 'PreviewRefused: BadMass: body 0 needs a mass' })),
    });
    await loaded(host);
    fireEvent.click(screen.getByText('Run preview'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('BadMass'));
    pick();
    setLimit('twist_max', '0.5');
    fireEvent.click(screen.getByText('Stage edit'));
    fireEvent.change(screen.getByLabelText('Save as path'), { target: { value: 'out.hkx' } });
    fireEvent.click(screen.getByText('Save as'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('shape_geometry_unsupported'));
  });

  it('stages plane and friction edits for the save only and makes a field in another angle mode read only', async () => {
    // Constraint: detects a plane or friction edit that leaks into the preview overrides, a cone or plane field editable while its atom stores another angle measurement mode, and a blocked field that is still sent to the save.
    const withPlane = {
      ...ragdoll,
      constraints: [{ ...ragdoll.constraints[0], plane_min_angle: -0.3, plane_max_angle: 0.3, friction: 1,
                      cone_angle_mode: 0, plane_angle_mode: 1 }],
    };
    const host = hostWith({ ReadRagdoll: vi.fn(async () => JSON.stringify(withPlane)) });
    await loaded(host);
    pick();
    expect((screen.getByLabelText('plane_min') as HTMLInputElement).value).toBe('-0.3');
    expect((screen.getByLabelText('friction') as HTMLInputElement).value).toBe('1');
    setLimit('plane_max', '0.1');
    setLimit('friction', '2.5');
    fireEvent.click(screen.getByText('Preview limit'));
    fireEvent.click(screen.getByText('Run preview'));
    await waitFor(() => expect(host.PreviewRagdoll).toHaveBeenCalledTimes(1));
    const options = JSON.parse(host.PreviewRagdoll.mock.calls[0][1] as string);
    expect(options.limit_overrides).toEqual([{ constraint: 0, twist_min: -0.5, twist_max: 0.75, cone_max: 0.625 }]);

    fireEvent.click(screen.getByText('Stage edit'));
    fireEvent.change(screen.getByLabelText('Save as path'), { target: { value: 'out.hkx' } });
    fireEvent.click(screen.getByText('Save as'));
    await waitFor(() => expect(host.SaveRagdoll).toHaveBeenCalledTimes(1));
    const edits = JSON.parse(host.SaveRagdoll.mock.calls[0][2] as string);
    expect(edits.constraints).toEqual([{ constraint: 0, plane_max: 0.1, friction: 2.5 }]);
    expect(buildPreviewOptions({ 0: { plane_max: 0.1, friction: 2 } }, 60, 12).limit_overrides).toEqual([]);

    cleanup();
    const foreign = { ...withPlane, constraints: [{ ...withPlane.constraints[0], cone_angle_mode: 3, plane_angle_mode: 0 }] };
    const other = hostWith({ ReadRagdoll: vi.fn(async () => JSON.stringify(foreign)) });
    await loaded(other);
    pick();
    for (const field of ['cone_max', 'plane_min', 'plane_max']) {
      expect((screen.getByLabelText(field) as HTMLInputElement).disabled).toBe(true);
    }
    expect(screen.getByLabelText('cone_max read only').textContent).toContain('angle measurement mode 3');
    expect((screen.getByLabelText('twist_max') as HTMLInputElement).disabled).toBe(false);
    expect((screen.getByLabelText('friction') as HTMLInputElement).disabled).toBe(false);
    setLimit('twist_max', '0.4');
    fireEvent.click(screen.getByText('Stage edit'));
    fireEvent.change(screen.getByLabelText('Save as path'), { target: { value: 'out.hkx' } });
    fireEvent.click(screen.getByText('Save as'));
    await waitFor(() => expect(other.SaveRagdoll).toHaveBeenCalledTimes(1));
    expect(JSON.parse(other.SaveRagdoll.mock.calls[0][2] as string).constraints)
      .toEqual([{ constraint: 0, twist_max: 0.4 }]);
  });

  it('says the host build cannot load or preview instead of showing an empty ragdoll', async () => {
    // Constraint: detects an older host shown as a ragdoll with no joints.
    render(<HkxRagdollTools host={{} as HkxHost} disabled={false} />);
    fireEvent.change(screen.getByLabelText('Ragdoll path'), { target: { value: PATH } });
    fireEvent.click(screen.getByText('Load ragdoll'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot read a ragdoll'));
  });

  it('reports a file with no ragdoll with the engine headline', async () => {
    // Constraint: detects a skeleton-only file shown as a loaded ragdoll.
    const host = hostWith({ ReadRagdoll: vi.fn(async () => JSON.stringify({ loaded: false, headline: 'This file holds a skeleton but no ragdoll' })) });
    render(<HkxRagdollTools host={host} disabled={false} />);
    fireEvent.change(screen.getByLabelText('Ragdoll path'), { target: { value: PATH } });
    fireEvent.click(screen.getByText('Load ragdoll'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('skeleton but no ragdoll'));
  });

  it('reports overlay toggles, selection and the loaded bodies to the viewport through onScene', async () => {
    // Constraint: detects overlay or selection controls that change local state without telling the viewport.
    const onScene = vi.fn();
    render(<HkxRagdollTools host={hostWith()} disabled={false} onScene={onScene} />);
    const last = () => onScene.mock.calls[onScene.mock.calls.length - 1][0];
    fireEvent.click(screen.getByLabelText('skeleton overlay'));
    expect(last().overlays).toEqual({ skeleton: false, mesh: false, ragdoll: true });
    fireEvent.click(screen.getByLabelText('ragdoll overlay'));
    expect(last().overlays.ragdoll).toBe(false);
    fireEvent.click(screen.getByLabelText('ragdoll overlay'));
    expect((screen.getByLabelText('mesh overlay') as HTMLInputElement).disabled).toBe(true);

    fireEvent.change(screen.getByLabelText('Ragdoll path'), { target: { value: PATH } });
    fireEvent.click(screen.getByText('Load ragdoll'));
    await waitFor(() => expect(screen.getByLabelText('Ragdoll bodies').textContent).toContain('body 1 Child Body'));
    expect(last().bodies).toEqual([{ id: 0, position: [0, 0, 0] }, { id: 1, position: [0, 0, 1] }]);
    expect(last().links).toEqual([{ id: 0, a: 1, b: 0 }]);
    expect(last().frameKey).toBe(`ragdoll|${PATH}`);
    fireEvent.click(screen.getByText(/body 1 Child Body/));
    expect(last().selection).toEqual({ body: 1, constraint: null });
    pick();
    expect(last().selection).toEqual({ body: null, constraint: 0 });
  });

  it('moves the reported bodies to the last preview frame and back on recover', async () => {
    // Constraint: detects a preview run that is shown only in the list while the viewport keeps the authored pose.
    const moved = { ...previewReply, frames: [{ step: 0, bodies: [] }, { step: 120, bodies: [{ body: 1, position: [5, 0, 1], rotation: [0, 0, 0, 1] }] }] };
    const onScene = vi.fn();
    const host = hostWith({ PreviewRagdoll: vi.fn(async () => JSON.stringify(moved)) });
    render(<HkxRagdollTools host={host} disabled={false} onScene={onScene} />);
    const last = () => onScene.mock.calls[onScene.mock.calls.length - 1][0];
    fireEvent.change(screen.getByLabelText('Ragdoll path'), { target: { value: PATH } });
    fireEvent.click(screen.getByText('Load ragdoll'));
    await waitFor(() => expect(screen.getByLabelText('Ragdoll constraints').textContent).toContain('constraint 0'));
    fireEvent.click(screen.getByText('Run preview'));
    await waitFor(() => expect(last().bodies[1].position).toEqual([5, 0, 1]));
    expect(last().bodies[0].position).toEqual([0, 0, 0]);
    fireEvent.click(screen.getByText('Recover'));
    expect(last().bodies[1].position).toEqual([0, 0, 1]);
  });

  it('sends the loaded clip pose to the preview only when the pose box is ticked', async () => {
    // Constraint: detects a pose control that is not connected to the preview call, or one that always sends the pose.
    const poseBones = [{ translation: [1, 2, 3], rotation: [0, 0, 0, 1] }];
    const host = hostWith();
    render(<HkxRagdollTools host={host} disabled={false} poseBones={poseBones} />);
    fireEvent.change(screen.getByLabelText('Ragdoll path'), { target: { value: PATH } });
    fireEvent.click(screen.getByText('Load ragdoll'));
    await waitFor(() => expect(screen.getByLabelText('Ragdoll constraints').textContent).toContain('constraint 0'));
    fireEvent.click(screen.getByText('Run preview'));
    await waitFor(() => expect(host.PreviewRagdoll).toHaveBeenCalledTimes(1));
    expect(JSON.parse(host.PreviewRagdoll.mock.calls[0][1] as string).pose).toBeUndefined();

    fireEvent.click(screen.getByLabelText('Pose from loaded clip'));
    fireEvent.click(screen.getByText('Run preview'));
    await waitFor(() => expect(host.PreviewRagdoll).toHaveBeenCalledTimes(2));
    expect(JSON.parse(host.PreviewRagdoll.mock.calls[1][1] as string).pose).toEqual([{ position: [1, 2, 3], rotation: [0, 0, 0, 1] }]);
  });

  it('does not stage a preview override when the constraint is selected again', async () => {
    // Constraint: detects a re-selected constraint whose draft was prefilled from a preview override, so Stage edit would save the experiment.
    const host = hostWith();
    await loaded(host);
    pick();
    setLimit('twist_max', '0.2');
    fireEvent.click(screen.getByText('Preview limit'));
    pick();
    expect((screen.getByLabelText('twist_max') as HTMLInputElement).value).toBe('0.75');
    expect(screen.getByLabelText('Preview override values').textContent).toContain('twist_max 0.2');

    fireEvent.click(screen.getByText('Stage edit'));
    expect(screen.getByLabelText('Edit counts').textContent).toContain('1 preview override(s), 0 staged edit(s)');
    fireEvent.change(screen.getByLabelText('Save as path'), { target: { value: 'C:\\out\\edited.hkx' } });
    expect((screen.getByText('Save as') as HTMLButtonElement).disabled).toBe(true);
  });

  it('stages only the fields the user changed, so an untouched out-of-range stored plane or friction is never sent', async () => {
    // Constraint: detects a twist-only stage that re-sends an untouched stored plane limit or friction, which the native range check refuses on a non-vanilla file.
    const odd = {
      ...ragdoll,
      constraints: [{ ...ragdoll.constraints[0], plane_min_angle: -4, plane_max_angle: 0.3, friction: -1,
                      cone_angle_mode: 0, plane_angle_mode: 1 }],
    };
    const host = hostWith({ ReadRagdoll: vi.fn(async () => JSON.stringify(odd)) });
    await loaded(host);
    pick();
    setLimit('twist_max', '0.4');
    fireEvent.click(screen.getByText('Stage edit'));
    fireEvent.change(screen.getByLabelText('Save as path'), { target: { value: 'out.hkx' } });
    fireEvent.click(screen.getByText('Save as'));
    await waitFor(() => expect(host.SaveRagdoll).toHaveBeenCalledTimes(1));
    expect(JSON.parse(host.SaveRagdoll.mock.calls[0][2] as string).constraints).toEqual([{ constraint: 0, twist_max: 0.4 }]);
    await waitFor(() => expect((screen.getByText('Save as') as HTMLButtonElement).disabled).toBe(false));

    pick();
    setLimit('plane_max', '0.2');
    fireEvent.click(screen.getByText('Stage edit'));
    pick();
    setLimit('plane_max', '0.3');
    fireEvent.click(screen.getByText('Stage edit'));
    fireEvent.click(screen.getByText('Save as'));
    await waitFor(() => expect(host.SaveRagdoll).toHaveBeenCalledTimes(2));
    expect(JSON.parse(host.SaveRagdoll.mock.calls[1][2] as string).constraints)
      .toEqual([{ constraint: 0, twist_max: 0.4, plane_max: 0.3 }]);
  });

  it('does not store a preview override or a staged edit that holds no field', async () => {
    // Constraint: detects an empty override or staged entry that is counted although it carries nothing.
    const host = hostWith();
    await loaded(host);
    pick();
    for (const field of ['twist_min', 'twist_max', 'cone_max']) setLimit(field, '');
    fireEvent.click(screen.getByText('Preview limit'));
    fireEvent.click(screen.getByText('Stage edit'));
    expect(screen.getByLabelText('Edit counts').textContent).toContain('0 preview override(s), 0 staged edit(s)');
    expect(screen.queryByLabelText('Preview override values')).toBeNull();
  });

  it('refuses a clip pose with a different bone count than the ragdoll and says so', async () => {
    // Constraint: detects a pose from another rig being sent to the preview, which would pose bones that do not match.
    const host = hostWith({ ReadRagdoll: vi.fn(async () => JSON.stringify({ ...ragdoll, animation_skeleton: { name: 'Rig', bone_count: 2 } })) });
    const poseBones = [{ translation: [1, 2, 3], rotation: [0, 0, 0, 1] }];
    render(<HkxRagdollTools host={host} disabled={false} poseBones={poseBones} />);
    fireEvent.change(screen.getByLabelText('Ragdoll path'), { target: { value: PATH } });
    fireEvent.click(screen.getByText('Load ragdoll'));
    await waitFor(() => expect(screen.getByLabelText('Pose mismatch').textContent).toContain('1 bones but this ragdoll expects 2'));
    expect((screen.getByLabelText('Pose from loaded clip') as HTMLInputElement).disabled).toBe(true);
  });

  it('offers no pose option when no clip pose is loaded', async () => {
    // Constraint: detects a pose box that can be ticked with nothing to send.
    render(<HkxRagdollTools host={hostWith()} disabled={false} poseBones={null} />);
    fireEvent.change(screen.getByLabelText('Ragdoll path'), { target: { value: PATH } });
    fireEvent.click(screen.getByText('Load ragdoll'));
    await waitFor(() => expect(screen.getByLabelText('Ragdoll constraints').textContent).toContain('constraint 0'));
    expect((screen.getByLabelText('Pose from loaded clip') as HTMLInputElement).disabled).toBe(true);
  });
});

describe('ragdollGeometry', () => {
  const scene = (patch: Partial<RagdollScene> = {}): RagdollScene => ({
    overlays: { skeleton: true, mesh: false, ragdoll: true },
    selection: { body: null, constraint: null },
    bodies: [{ id: 0, position: [0, 0, 0] }, { id: 1, position: [0, 0, 1] }],
    links: [{ id: 7, a: 0, b: 1 }],
    frameKey: 'k',
    ...patch,
  });

  it('draws bodies and links, and nothing when the ragdoll overlay is off', () => {
    // Constraint: detects an overlay flag that the geometry ignores, so toggling it would not change the view.
    expect(ragdollGeometry(scene()).normal).toHaveLength(6 + 6 + 2);
    expect(ragdollGeometry(scene({ overlays: { skeleton: true, mesh: false, ragdoll: false } }))).toEqual({ normal: [], selected: [] });
  });

  it('moves a selected body or constraint into the highlight list', () => {
    // Constraint: detects a selection that is stored but never drawn differently.
    const body = ragdollGeometry(scene({ selection: { body: 1, constraint: null } }));
    expect(body.selected).toHaveLength(6);
    expect(body.normal).toHaveLength(6 + 2);
    const link = ragdollGeometry(scene({ selection: { body: null, constraint: 7 } }));
    expect(link.selected).toEqual([[0, 0, 0], [0, 0, 1]]);
    expect(link.normal).toHaveLength(12);
  });

  it('skips a link to a missing body and a body without a finite position', () => {
    // Constraint: detects a malformed row that reaches the viewport as NaN or an undefined point.
    const view = { bodies: [{ id: 0, name: 'a', position: [0, 0, 0] }, { id: 1, name: 'b', position: [Number.NaN, 0, 0] }, { id: 2, name: 'c' }], constraints: [], poseBoneCount: null };
    const built = buildRagdollScene(view, null, { skeleton: true, mesh: false, ragdoll: true }, { body: null, constraint: null }, 'p');
    expect(built.bodies).toEqual([{ id: 0, position: [0, 0, 0] }]);
    expect(ragdollGeometry(scene({ links: [{ id: 1, a: 0, b: 9 }] })).normal).toHaveLength(12);
  });
});
