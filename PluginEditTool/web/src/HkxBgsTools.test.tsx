import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxBgsTools from './HkxBgsTools';

const PATH = 'C:\\mods\\rig.hkx';
// Constraint: the field and the pointed-at object sit at known offsets so the assertions stay stable.
const BONES_AT = 16;
const BONE_OBJECT_AT = 176;

const view = {
  object_count: 2,
  matched: 2,
  classes: ['hkaSkeleton', 'hkaBone'],
  objects: [
    {
      index: 0, offset: 0, signature: 0xfec1cedb, class_name: 'hkaSkeleton', layout_size: 0x58, size: 0x58,
      layout_known: true,
      fields: [{ name: 'bones', offset: BONES_AT, size: 12, hex: '00000000 00000000' }],
      references: [{
        source: BONES_AT, member: 'bones', target_section: 2, target_offset: BONE_OBJECT_AT, target_object: 1,
        target_class: 'hkaBone',
      }],
    },
    {
      index: 1, offset: BONE_OBJECT_AT, signature: 1, class_name: 'hkaBone', layout_size: 0, size: 8,
      layout_known: false, fields: [], references: [],
    },
  ],
  diagnostics: ['no generated layout for hkaBone (signature 0x00000001)'],
};

const hostWith = (result: unknown) =>
  ({ InspectObjects: vi.fn(async () => JSON.stringify(result)) } as unknown as HkxHost);

afterEach(() => cleanup());

describe('HkxBgsTools packfile inspector', () => {
  it('shows a resolved bone pointer instead of a raw offset', async () => {
    // Constraint: detects a panel that lists objects but leaves every pointer as a number, so a rig cannot be followed.
    render(<HkxBgsTools host={hostWith(view)} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Inspect objects'));
    await waitFor(() => expect(screen.getByLabelText('Object references')).toBeTruthy());
    expect(screen.getByText('bones -> object 1 hkaBone')).toBeTruthy();
    expect(screen.getByText(/bones at 16 \(12 bytes\)/)).toBeTruthy();
    expect(screen.getByRole('status').textContent).toContain('2 of 2 objects');
  });

  it('marks an object whose class has no generated layout', async () => {
    // Constraint: detects an unknown class rendered as a normal object with no fields, which reads as an empty object.
    render(<HkxBgsTools host={hostWith(view)} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Inspect objects'));
    await waitFor(() => expect(screen.getByLabelText('Packfile objects')).toBeTruthy());
    fireEvent.click(screen.getByText(/^1: hkaBone/));
    expect(screen.getByLabelText('Packfile diagnostics').textContent).toContain('no generated layout');
    expect(screen.getByLabelText('Object detail').textContent).toContain('0 fields');
  });

  it('says the host build cannot inspect objects instead of showing an empty list', async () => {
    // Constraint: detects an older host without the RPC showing an empty list, which reads as an empty packfile.
    render(<HkxBgsTools host={{} as HkxHost} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Inspect objects'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot inspect HKX objects'));
  });
});

describe('HkxBgsTools root motion', () => {
  const motion = {
    bone: 2, bone_name: 'Head', track: 1, frame_count: 2, duration: 1, frame_duration: 1,
    total: [1, 0, 0], distance: 1, yaw_degrees: 0, velocity: [1, 0, 0], loops: false, loop_gap: 1,
    samples: [
      { frame: 0, time: 0, translation: [2, 0, 0], delta: [0, 0, 0] },
      { frame: 1, time: 1, translation: [3, 0, 0], delta: [1, 0, 0] },
    ],
  };

  it('reports travel and loop closure for the chosen bone', async () => {
    // Constraint: detects a tab that shows samples but never states whether the clip loops.
    const host = { RootMotion: vi.fn(async () => JSON.stringify(motion)) } as unknown as HkxHost;
    render(<HkxBgsTools host={host} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Root motion'));
    fireEvent.change(screen.getByLabelText('Root motion bone'), { target: { value: '2' } });
    fireEvent.click(screen.getByText('Read root motion'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('1.000 travelled'));
    expect(screen.getByLabelText('Root motion samples').textContent).toContain('+1.000, 0.000, 0.000');
  });

  it('says the host build cannot report root motion instead of showing zero travel', async () => {
    // Constraint: detects an older host shown as a clip that never moves.
    render(<HkxBgsTools host={{} as HkxHost} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Root motion'));
    fireEvent.click(screen.getByText('Read root motion'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot report root motion'));
  });
});

describe('HkxBgsTools skeleton tab', () => {
  const document = {
    name: 'Rig', bone_count: 3, has_reference_pose: true, roots: ['Root'],
    bones: [
      { index: 0, name: 'Root', parent: -1, depth: 0, children: [1, 2], opposite: '' },
      { index: 1, name: 'LArm', parent: 0, depth: 1, children: [], opposite: 'RArm' },
      { index: 2, name: 'RArm', parent: 0, depth: 1, children: [], opposite: 'LArm' },
    ],
    diagnostics: [],
  };

  it('lists bones with their partner and shows a refused edit instead of a silent no-op', async () => {
    // Constraint: detects a bone list without depth or partner, which hides a broken chain and an unpaired limb.
    const host = {
      SkeletonDocument: vi.fn(async () => JSON.stringify(document)),
      SkeletonEdit: vi.fn(async () => JSON.stringify({ error: 'another bone is already named RArm' })),
    } as unknown as HkxHost;
    render(<HkxBgsTools host={host} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Skeleton'));
    fireEvent.click(screen.getByText('Read skeleton'));
    await waitFor(() => expect(screen.getByLabelText('Skeleton bones').textContent).toContain('pairs with RArm'));
    expect(screen.getByLabelText('Skeleton bones').textContent).toContain('(root)');
    fireEvent.click(screen.getByText('Apply edit'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('already named'));
  });
});

describe('HkxBgsTools behavior tab', () => {
  it('shows the links between behavior objects and notes a file without any', async () => {
    // Constraint: detects a behavior view that lists nodes but drops the links, which hides the graph shape.
    const graph = {
      object_count: 3, matched: 2, classes: ['hkbAssetBundle', 'hkbAnimatedSkeletonGenerator'],
      nodes: [
        { object: 0, class_name: 'hkbAssetBundle', offset: 0, size: 32, layout_known: true, references_out: 1, references_in: 0 },
        { object: 1, class_name: 'hkbAnimatedSkeletonGenerator', offset: 32, size: 24, layout_known: true, references_out: 0, references_in: 1 },
      ],
      links: [{ from: 0, to: 1, member: 'animations' }],
      diagnostics: [],
    };
    const host = { InspectBehavior: vi.fn(async () => JSON.stringify(graph)) } as unknown as HkxHost;
    render(<HkxBgsTools host={host} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Behavior'));
    fireEvent.click(screen.getByText('Read behavior'));
    await waitFor(() => expect(screen.getByLabelText('Behavior links').textContent).toContain('0 to 1 via animations'));
    expect(screen.getByRole('status').textContent).toContain('2 of 3 objects are behavior nodes');
  });

  it('lists the typed nodes with their child edges and state generators', async () => {
    // Constraint: detects a typed view that drops the child edges or the state generators, which leaves a machine looking childless.
    const model = {
      graph_name: 'TestGraph', root_node: 0,
      events: [{ name: 'Go', flags: 0 }],
      variables: [{ name: 'fSpeed', type: 4, role: 0, word: 0, int_value: 0, real_value: 2.5 }],
      nodes: [
        { object: 3, class_name: 'hkbStateMachine', name: 'Root', kind: 'state_machine', kind_index: 0 },
        { object: 4, class_name: 'hkbClipGenerator', name: 'ClipA', kind: 'clip', kind_index: 0 },
        { object: 5, class_name: 'BSiStateTaggingGenerator', name: 'Tag', kind: 'state_tagging', kind_index: 0 },
      ],
      edges: [{ parent: 2, child: 1, role: 'wrapped', slot: 0, weight: 0, has_bone_weights: false }],
      states: [{ state_machine: 0, name: 'Idle', state_id: 7, generator_node: 1 }],
      event_total: 1, variable_total: 1, node_total: 3, edge_total: 1, state_total: 1,
      diagnostics: [],
    };
    const host = { ReadBehaviorModel: vi.fn(async () => JSON.stringify(model)) } as unknown as HkxHost;
    render(<HkxBgsTools host={host} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Behavior'));
    fireEvent.click(screen.getByText('Read typed graph'));
    await waitFor(() => expect(screen.getByLabelText('Typed graph nodes').textContent).toContain('state Idle 1 ClipA'));
    expect(screen.getByLabelText('Typed graph nodes').textContent).toContain('2: Tag [BSiStateTaggingGenerator, state_tagging] -> wrapped 1 ClipA');
    expect(screen.getByLabelText('Typed graph nodes').textContent).toContain('(root)');
    expect(screen.getByRole('status').textContent).toContain('3 of 3 nodes, 1 of 1 child edges');
    expect(host.ReadBehaviorModel).toHaveBeenCalledWith(PATH, 2000);
  });

  it('says the host build cannot read the typed behavior graph', async () => {
    // Constraint: detects an older host shown as a graph with no nodes.
    render(<HkxBgsTools host={{} as HkxHost} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Behavior'));
    fireEvent.click(screen.getByText('Read typed graph'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot read the typed behavior graph'));
  });

  it('says the host build cannot read a behavior graph', async () => {
    // Constraint: detects an older host shown as a file with no behavior objects.
    render(<HkxBgsTools host={{} as HkxHost} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Behavior'));
    fireEvent.click(screen.getByText('Read behavior'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot read a behavior graph'));
  });
});

describe('HkxBgsTools ragdoll tab', () => {
  it('lists the bodies and constraint limits of a loaded ragdoll', async () => {
    // Constraint: detects a ragdoll view that lists bodies but drops a constraint's twist and cone limits.
    const ragdoll = {
      loaded: true, status: 'ok', status_label: 'OK', headline: 'ok', hint: '', findings: [],
      body_count: 2, usable_bodies: 2, shape_count: 2, constraint_count: 1, mappings: 2,
      body_total: 2, constraint_total: 1,
      bodies: [
        { id: 0, name: 'Root Body', shape_id: 1, bone_index: -1, mass: 5, center_of_mass: [0, 0, 0], position: [1, 2, 3], rotation: [0, 0, 0, 1] },
        { id: 1, name: 'Child Body', shape_id: 2, bone_index: 1, mass: 2, center_of_mass: [0, 0, 0], position: [4, 5, 6], rotation: [0, 0, 0, 1] },
      ],
      constraints: [{
        id: 0, kind: 'ragdoll', body_a: 1, body_b: 0, min_angle: 0, max_angle: 0,
        twist_min_angle: -0.5, twist_max_angle: 0.75, cone_max_angle: 0.625, has_preview_limits: true,
      }],
      skeleton: { name: 'Physics', bone_count: 2 }, animation_skeleton: { name: 'Animation', bone_count: 3 },
      validation: [{ level: 'warning', where: 'body 1', message: 'mass is zero' }],
    };
    const host = { ReadRagdoll: vi.fn(async () => JSON.stringify(ragdoll)) } as unknown as HkxHost;
    render(<HkxBgsTools host={host} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Ragdoll'));
    fireEvent.click(screen.getByText('Read ragdoll'));
    await waitFor(() => expect(screen.getByLabelText('Ragdoll bodies').textContent).toContain('0: Root Body shape 1'));
    expect(screen.getByLabelText('Ragdoll constraints').textContent).toContain('twist -0.500 to 0.750, cone 0.625');
    expect(screen.getByLabelText('Ragdoll skeletons').textContent).toContain('physics skeleton Physics (2 bones)');
    expect(screen.getByLabelText('Ragdoll validation').textContent).toContain('warning body 1: mass is zero');
    expect(screen.getByRole('status').textContent).toContain('2 of 2 bodies, 1 of 1 constraints');
    expect(host.ReadRagdoll).toHaveBeenCalledWith(PATH, 2000);
  });

  it('shows the classifier verdict for a file with no usable ragdoll', async () => {
    // Constraint: detects a skeleton-only file shown as an empty ragdoll instead of the reason it has no bodies.
    const verdict = {
      loaded: false, status: 'skeleton-only', status_label: 'SKELETON ONLY',
      headline: 'The file holds a skeleton but no ragdoll data.', hint: 'Open the character physics file instead.',
      findings: [{ severity: 'warning', message: 'The file holds a skeleton but no ragdoll data.', hint: 'Open the character physics file instead.' }],
      body_count: 0, usable_bodies: 0, shape_count: 0, constraint_count: 0, mappings: 0,
    };
    const host = { ReadRagdoll: vi.fn(async () => JSON.stringify(verdict)) } as unknown as HkxHost;
    render(<HkxBgsTools host={host} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Ragdoll'));
    fireEvent.click(screen.getByText('Read ragdoll'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('SKELETON ONLY: The file holds a skeleton but no ragdoll data.'));
    expect(screen.getByLabelText('Ragdoll load findings').textContent).toContain('Open the character physics file instead.');
    expect(screen.queryByLabelText('Ragdoll bodies')).toBeNull();
  });

  it('says the host build cannot read a ragdoll', async () => {
    // Constraint: detects an older host shown as a file with no ragdoll.
    render(<HkxBgsTools host={{} as HkxHost} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Ragdoll'));
    fireEvent.click(screen.getByText('Read ragdoll'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot read a ragdoll'));
  });
});

describe('HkxBgsTools mapper tab', () => {
  const pose = (x: number, y: number, z: number) => ({ translation: [x, y, z], rotation: [0, 0, 0, 1], scale: [1, 1, 1] });
  const mapperFile = {
    mapper_count: 1, recompute: true,
    mappers: [{
      object_index: 2, skeleton_a: { name: 'RigA', bone_count: 3 }, skeleton_b: { name: 'RigB', bone_count: 3 },
      mapping_type: 1, keep_unmapped_local: true, unmapped_bones: [2],
      rows: [
        { bone_a: 0, bone_b: 0, bone_a_name: 'Root', bone_b_name: 'Root', transform: pose(0, 0, 0), recomputed: pose(0, 0, 0), translation_delta: 0, rotation_delta: 0 },
        { bone_a: 1, bone_b: 1, bone_a_name: 'Child', bone_b_name: 'Child', transform: pose(0, 5, 0), recomputed: pose(0, 1, 0), translation_delta: 4, rotation_delta: 0 },
        { bone_a: 9, bone_b: 1, bone_a_name: '', bone_b_name: 'Child', transform: pose(0, 0, 0), recompute_error: 'bone 9 is not a bone of skeleton A' },
      ],
      chains: [{ start_bone_a: 1, end_bone_a: 2, start_bone_b: 1, end_bone_b: 2 }],
      row_total: 3, chain_total: 1,
    }],
  };

  it('lists rows with bone names, the recompute gap and a refused recompute', async () => {
    // Constraint: detects a mapper view that hides a drifted row or a row that could not be recomputed.
    const host = { ReadMappers: vi.fn(async () => JSON.stringify(mapperFile)) } as unknown as HkxHost;
    render(<HkxBgsTools host={host} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Mapper'));
    fireEvent.click(screen.getByLabelText(/recompute rows/));
    fireEvent.click(screen.getByText('Read mappers'));
    await waitFor(() => expect(screen.getByLabelText('Mapper rows').textContent).toContain('1 Child from 1 Child'));
    expect(screen.getByLabelText('Mapper rows').textContent).toContain('recomputed gap 4.0000 units');
    expect(screen.getByLabelText('Mapper rows').textContent).toContain('cannot recompute: bone 9 is not a bone of skeleton A');
    expect(screen.getByLabelText('Mapper summary').textContent).toContain('RigA (3 bones) to RigB (3 bones), type 1');
    expect(screen.getByLabelText('Mapper chains').textContent).toContain('chain A 1 to 2, B 1 to 2');
    expect(screen.getByRole('status').textContent).toContain('1 mapper: RigA to RigB, 3 of 3 rows, 1 of 1 chains');
    expect(host.ReadMappers).toHaveBeenCalledWith(PATH, true, 2000);
  });

  it('prints a dash for a NaN translation and gap instead of throwing', async () => {
    // Constraint: detects a mapper row with a null (NaN) number that unmounts the tab.
    const nan = { ...mapperFile, mappers: [{ ...mapperFile.mappers[0], rows: [{ bone_a: 0, bone_b: 0, bone_a_name: 'Root', bone_b_name: 'Root', transform: { translation: [null, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] }, translation_delta: null, rotation_delta: null }], row_total: 1 }] };
    const host = { ReadMappers: vi.fn(async () => JSON.stringify(nan)) } as unknown as HkxHost;
    render(<HkxBgsTools host={host} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Mapper'));
    fireEvent.click(screen.getByLabelText(/recompute rows/));
    fireEvent.click(screen.getByText('Read mappers'));
    await waitFor(() => expect(screen.getByLabelText('Mapper rows').textContent).toContain('-, 0.000, 0.000'));
    expect(screen.getByLabelText('Mapper rows').textContent).toContain('recomputed gap - units, - rad');
  });


  it('shows the host error for a file with no mapper', async () => {
    // Constraint: detects a mapper-less file shown as an empty mapper list.
    const host = { ReadMappers: vi.fn(async () => JSON.stringify({ error: 'skeleton mapper read failed: MissingObject' })) } as unknown as HkxHost;
    render(<HkxBgsTools host={host} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Mapper'));
    fireEvent.click(screen.getByText('Read mappers'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('MissingObject'));
    expect(screen.queryByLabelText('Mapper rows')).toBeNull();
  });

  it('says the host build cannot read skeleton mappers', async () => {
    // Constraint: detects an older host shown as a file with no mappers.
    render(<HkxBgsTools host={{} as HkxHost} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Mapper'));
    fireEvent.click(screen.getByText('Read mappers'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot read skeleton mappers'));
  });
});

describe('HkxBgsTools project tab', () => {
  it('lists the scanned files, their counts and the duplicate graph groups', async () => {
    // Constraint: detects an index view that shows file names but hides a duplicate graph pair.
    const index = {
      root: 'C:\\mods', query: '', file_count: 2, read_count: 2, matches: ['C:\\mods\\a\\graph.hkx'],
      files: [
        { name: 'C:\\mods\\a\\graph.hkx', object_count: 2, behavior_objects: 2, animation_objects: 0, skeleton_objects: 0, behavior_hash: '17', classes: ['hkbAssetBundle'] },
        { name: 'C:\\mods\\rig.hkx', object_count: 3, behavior_objects: 0, animation_objects: 0, skeleton_objects: 1, behavior_hash: '0', classes: ['hkaSkeleton'] },
      ],
      unreadable: [{ name: 'C:\\mods\\broken.hkx', error: 'packfile read failed' }],
      duplicates: [['C:\\mods\\a\\graph.hkx', 'C:\\mods\\b\\graph.hkx']],
      diagnostics: [],
    };
    const host = { ProjectIndex: vi.fn(async () => JSON.stringify(index)) } as unknown as HkxHost;
    render(<HkxBgsTools host={host} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Project'));
    fireEvent.change(screen.getByLabelText('Project root'), { target: { value: 'C:\\mods' } });
    fireEvent.click(screen.getByText('Read index'));
    await waitFor(() => expect(screen.getByLabelText('Project files').textContent).toContain('2 behavior'));
    expect(screen.getByLabelText('Project duplicates').textContent).toContain('same behavior bytes');
    expect(screen.getByLabelText('Project unreadable').textContent).toContain('packfile read failed');
    expect(screen.getByRole('status').textContent).toContain('2 of 2 files read');
  });

  it('says the host build cannot index a project', async () => {
    // Constraint: detects an older host shown as an empty project folder.
    render(<HkxBgsTools host={{} as HkxHost} initialPath={PATH} skeletonPath={PATH} animationPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Project'));
    fireEvent.change(screen.getByLabelText('Project root'), { target: { value: 'C:\\mods' } });
    fireEvent.click(screen.getByText('Read index'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot index a project'));
  });
});
