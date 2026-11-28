import { describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import {
  applyAllLocally, behaviorToBlueprint, editBehavior, readBehaviorAuthoring, runPosition, transitionOfWire,
  type HkxAuthoringModel, type HkxAuthoringTransition,
} from './behaviorAuthoringClient';
import { generatorToBlueprint } from './behaviorGeneratorCanvas';

const interval = { enter_event_id: -1, exit_event_id: -1, enter_time: 0, exit_time: 0 };

const row = (index: number, state: number, event: number, to: number): HkxAuthoringTransition => ({
  index, state_machine: 0, state, event_id: event, to_state_id: to, from_nested_state_id: 0, to_nested_state_id: 0,
  priority: 3, flags: 0x0a00, trigger_interval: interval, initiate_interval: interval, effect_node: 2,
  condition_kind: 'string', condition: 'x > 1', condition_class: 'hkbStringCondition',
});

const model = (): HkxAuthoringModel => ({
  graph_name: 'Graph',
  root_node: 0,
  events: [{ name: 'Go', flags: 0 }, { name: 'Stop', flags: 0 }],
  variables: [],
  nodes: [],
  state_machines: [{ node: 0, start_state_id: 0, start_state_mode: 0, change_event_id: -1, first_state: 0, state_count: 2, first_wildcard: 1, wildcard_count: 1 }],
  states: [
    { state_machine: 0, name: 'Idle', state_id: 10, generator_node: 1, first_transition: 0, transition_count: 2 },
    { state_machine: 0, name: 'Run', state_id: 11, generator_node: 2, first_transition: 2, transition_count: 0 },
  ],
  transitions: [row(0, 0, 0, 11), row(1, 0, 1, 10), row(2, -1, 0, 11)],
  clips: [], blenders: [], blend_children: [], edges: [], bindings: [],
  validation: { errors: 1, warnings: 0, issue_total: 1, issues: [{ severity: 'error', code: 'state.generator_range', table: 'states', index: 1, message: 'bad generator' }] },
});

describe('behavior authoring client', () => {
  it('draws one exec pin and one wire per transition, wildcard rows from their own node, and flags the broken state', () => {
    // Constraint: detects a canvas whose pins or wires drift from the transitions in the file, which makes an edit hit the wrong row.
    const { doc, defs, diagnostics } = behaviorToBlueprint(model(), 0);
    expect(doc.nodes.map(n => n.id).sort()).toEqual(['s0', 's1', 'w0']);
    expect(defs.s0.pins.filter(p => p.dir === 'out').map(p => p.id)).toEqual(['t0', 't1', 'new']);
    expect(defs.w0.pins.some(p => p.dir === 'in')).toBe(false);
    expect(doc.wires.map(w => [w.id, w.from.node, w.from.pin, w.to.node]).sort()).toEqual([
      ['t0', 's0', 't0', 's1'], ['t1', 's0', 't1', 's0'], ['t2', 'w0', 't0', 's1'],
    ]);
    expect(diagnostics.map(d => [d.nodeId, d.code])).toEqual([['s1', 'state.generator_range']]);
    expect(transitionOfWire('t2')).toBe(2);
    expect(transitionOfWire('x2')).toBeNull();
  });

  it('keeps run positions valid while several edits are queued', () => {
    // Constraint: detects a queue that addresses rows by stale positions, so the second edit would change the wrong transition.
    const base = model();
    const added = applyAllLocally(base, [
      { kind: 'add_transition', state_machine: 0, state: 1, index: 0, transition: { event_id: 1, to_state_id: 10 } },
      { kind: 'add_transition', state_machine: 0, state: 0, index: 2, transition: { event_id: 0, to_state_id: 10 } },
      { kind: 'remove_transition', state_machine: 0, state: 0, index: 0 },
      { kind: 'add_event', name: 'Jump' },
      { kind: 'rename_event', index: 1, name: 'Halt' },
    ]);
    expect(added.transitions.map(t => [t.state, t.event_id, t.to_state_id])).toEqual([
      [0, 1, 10], [0, 0, 10], [-1, 0, 11], [1, 1, 10],
    ]);
    expect(added.transitions.map(t => t.index)).toEqual([0, 1, 2, 3]);
    expect(added.events.map(e => e.name)).toEqual(['Go', 'Halt', 'Jump']);
    expect(added.transitions[0].condition).toBe('x > 1');
    expect(runPosition(added, 1)).toEqual({ state_machine: 0, state: 0, index: 1 });
    expect(base.transitions).toHaveLength(3);
  });

  it('keeps the read fields of a changed row, so an edit cannot reset what the editor did not touch', () => {
    // Constraint: detects a set edit that rebuilds the row from a form and drops the effect, flags or condition it never showed.
    const base = model();
    const changed = applyAllLocally(base, [
      { kind: 'set_transition', state_machine: 0, state: 0, index: 1, transition: { ...base.transitions[1], event_id: 0 } },
    ]);
    expect(changed.transitions[1]).toMatchObject({ event_id: 0, flags: 0x0a00, effect_node: 2, condition: 'x > 1', state: 0 });
  });

  it('sends the edits as JSON text and refuses an empty list before calling the host', async () => {
    // Constraint: detects a client that calls the host with no edits or with an object the native parser cannot read.
    const EditBehaviorGraph = vi.fn(async () => JSON.stringify({ ok: true, applied: 1, written: false, size: 1, before: {}, after: {} }));
    const host = { EditBehaviorGraph } as unknown as HkxHost;
    await expect(editBehavior(host, 'a.hkx', [], '', false)).rejects.toThrow('no edits');
    expect(EditBehaviorGraph).not.toHaveBeenCalled();
    await editBehavior(host, 'a.hkx', [{ kind: 'add_event', name: 'Jump' }], '', false);
    expect(EditBehaviorGraph).toHaveBeenCalledWith('a.hkx', '[{"kind":"add_event","name":"Jump"}]', '', false);
  });

  it('shifts the state index of later machines and their transitions when a state is added or removed', () => {
    // Constraint: detects a queue that keeps stale state indexes after add_state or remove_state, so a later edit lands on another machine's state.
    const base = model();
    const two: HkxAuthoringModel = {
      ...base,
      state_machines: [base.state_machines[0], { ...base.state_machines[0], node: 4, first_state: 2, state_count: 1 }],
      states: [...base.states, { state_machine: 1, name: 'Other', state_id: 20, generator_node: 5, first_transition: 3, transition_count: 1 }],
      transitions: [...base.transitions, { ...row(3, 2, 1, 20), state_machine: 1 }],
    };
    const added = applyAllLocally(two, [{ kind: 'add_state', state_machine: 0, name: 'Walk', value: 12, node: 6 }]);
    expect(added.states.map(s => [s.state_machine, s.state_id])).toEqual([[0, 10], [0, 11], [0, 12], [1, 20]]);
    expect(added.state_machines.map(m => [m.first_state, m.state_count])).toEqual([[0, 3], [3, 1]]);
    expect(added.transitions[3]).toMatchObject({ state: 3, state_machine: 1 });
    expect(added.transitions[2].state).toBe(-1);

    const removed = applyAllLocally(added, [{ kind: 'remove_state', state_machine: 0, state: 0 }]);
    expect(removed.states.map(s => s.state_id)).toEqual([11, 12, 20]);
    expect(removed.state_machines.map(m => [m.first_state, m.state_count])).toEqual([[0, 2], [2, 1]]);
    expect(removed.transitions.map(t => [t.state, t.index])).toEqual([[-1, 0], [2, 1]]);
    expect(two.states).toHaveLength(3);
  });

  it('applies the clip, variable and start state edits to the local model without touching other rows', () => {
    // Constraint: detects a queued edit that changes the wrong clip, variable or machine, which would show a graph the host never writes.
    const base: HkxAuthoringModel = {
      ...model(),
      variables: [{ name: 'Speed', type: 4 }, { name: 'Dir', type: 4 }],
      clips: [{ node: 2, animation: 'a.hkx', mode: 0 }, { node: 3, animation: 'b.hkx', mode: 0 }],
    };
    const next = applyAllLocally(base, [
      { kind: 'set_clip_animation', index: 1, name: 'c.hkx' },
      { kind: 'rename_variable', index: 0, name: 'Velocity' },
      { kind: 'add_variable', name: 'Turn', flags: 0 },
      { kind: 'set_start_state', state_machine: 0, value: 11 },
    ]);
    expect(next.clips.map(c => c.animation)).toEqual(['a.hkx', 'c.hkx']);
    expect(next.variables).toEqual([{ name: 'Velocity', type: 4 }, { name: 'Dir', type: 4 }, { name: 'Turn', type: 0 }]);
    expect(next.state_machines[0].start_state_id).toBe(11);
    expect(base.variables.map(v => v.name)).toEqual(['Speed', 'Dir']);
  });

  it('applies a generator field edit to the addressed clip, blender or blend child row only', () => {
    // Constraint: detects a queued scalar edit that lands on another row or owner table, which would preview a value the host never writes.
    const base: HkxAuthoringModel = {
      ...model(),
      clips: [{ node: 2, animation: 'a.hkx', mode: 0 }, { node: 3, animation: 'b.hkx', mode: 0 }],
      blenders: [{ node: 1, first_child: 0, child_count: 2, blend_parameter: 0 }],
      blend_children: [{ blender: 0, generator_node: 2, weight: 1 }, { blender: 0, generator_node: 3, weight: 1 }],
    };
    const next = applyAllLocally(base, [
      { kind: 'set_generator_field', flags: 5, index: 1, name: 'playbackSpeed', real: 2.5 },
      { kind: 'set_generator_field', flags: 5, index: 0, name: 'mode', value: 3 },
      { kind: 'set_generator_field', flags: 4, index: 1, name: 'weight', real: 0.25 },
      { kind: 'set_generator_field', flags: 3, index: 0, name: 'blendParameter', real: 0.5 },
    ]);
    expect(next.clips.map(c => [c.mode, c.playback_speed])).toEqual([[3, undefined], [0, 2.5]]);
    expect(next.blend_children.map(c => c.weight)).toEqual([1, 0.25]);
    expect(next.blenders[0].blend_parameter).toBe(0.5);
    expect(base.clips[1].playback_speed).toBeUndefined();
  });

  it('moves child rows, the sync master and selector slots with a child add or remove, and leaves a subtree removal to the host', () => {
    // Constraint: detects a preview that leaves a later blender's first_child or a blend child binding on the wrong row, renumbers the sync master on an append, or shifts the wrong selector's slots, which would queue the next edit against a row the host does not have.
    const nodes = Array.from({ length: 8 }, (_, i) => ({ name: `N${i}`, class_name: i === 7 ? 'hkbManualSelectorGenerator' : 'hkbClipGenerator', kind: 0 }));
    const base: HkxAuthoringModel = {
      ...model(),
      nodes,
      blenders: [
        { node: 1, first_child: 0, child_count: 3, blend_parameter: 0, index_of_sync_master_child: 2 },
        { node: 2, first_child: 3, child_count: 1, blend_parameter: 0 },
      ],
      blend_children: [
        { blender: 0, generator_node: 3, weight: 1 }, { blender: 0, generator_node: 4, weight: 1 },
        { blender: 0, generator_node: 5, weight: 1 }, { blender: 1, generator_node: 3, weight: 1 },
      ],
      bindings: [{ owner_kind: 4, owner_index: 3, member_path: 'weight', variable_index: 0, binding_type: 0 }],
      edges: [
        { parent: 7, child: 3, role: 3, slot: 0, weight: 0 }, { parent: 7, child: 4, role: 3, slot: 1, weight: 0 },
        { parent: 1, child: 9, role: 1, slot: 0, weight: 0 },
      ],
    };
    const added = applyAllLocally(base, [{ kind: 'add_blend_child', index: 0, node: 6, real: 0.5 }]);
    expect(added.blend_children.map(c => [c.blender, c.generator_node])).toEqual([[0, 3], [0, 4], [0, 5], [0, 6], [1, 3]]);
    expect(added.blenders.map(b => [b.first_child, b.child_count, b.index_of_sync_master_child])).toEqual([[0, 4, 2], [4, 1, undefined]]);
    expect(added.bindings[0].owner_index).toBe(4);

    const removed = applyAllLocally(base, [{ kind: 'remove_blend_child', index: 0 }]);
    expect(removed.blend_children.map(c => c.generator_node)).toEqual([4, 5, 3]);
    expect(removed.blenders.map(b => [b.first_child, b.child_count, b.index_of_sync_master_child])).toEqual([[0, 2, 1], [2, 1, undefined]]);
    expect(removed.bindings[0].owner_index).toBe(2);
    expect(applyAllLocally(base, [{ kind: 'remove_blend_child', index: 99 }])).toBe(base);

    const selector = applyAllLocally(base, [{ kind: 'add_selector_child', node: 7, value: 5 }, { kind: 'remove_selector_child', index: 0 }]);
    expect(selector.edges.map(e => [e.parent, e.child, e.slot])).toEqual([[7, 4, 0], [7, 5, 1], [1, 9, 0]]);
    expect(applyAllLocally(base, [{ kind: 'add_selector_child', node: 1, value: 5 }])).toBe(base);
    expect(applyAllLocally(base, [{ kind: 'remove_subtree', node: 3 }])).toBe(base);
  });

  it('keeps binding rows in the host order and moves state-owned rows with their states', () => {
    // Constraint: detects a queue that addresses a binding row by a stale table position, or leaves a state's bindings on the wrong state after a state is added or removed.
    const bind = (kind: number, owner: number, path: string, variable: number) =>
      ({ owner_kind: kind, owner_index: owner, member_path: path, variable_index: variable, binding_type: 0 });
    const base: HkxAuthoringModel = {
      ...model(),
      variables: [{ name: 'Speed', type: 4 }, { name: 'Dir', type: 4 }],
      bindings: [bind(3, 0, 'blendParameter', 0), bind(2, 0, 'enable', 0), bind(2, 1, 'probability', 1), bind(3, 1, 'other', 1)],
    };
    const edited = applyAllLocally(base, [
      { kind: 'add_binding', index: 0, flags: 3, name: 'extra', value: 1 },
      { kind: 'set_binding', index: 2, value: 1 },
      { kind: 'remove_binding', index: 0 },
    ]);
    expect(edited.bindings.map(b => [b.owner_kind, b.owner_index, b.member_path, b.variable_index])).toEqual([
      [3, 0, 'extra', 1], [2, 0, 'enable', 1], [2, 1, 'probability', 1], [3, 1, 'other', 1],
    ]);

    const two: HkxAuthoringModel = {
      ...base,
      state_machines: [base.state_machines[0], { ...base.state_machines[0], node: 4, first_state: 2, state_count: 1 }],
      states: [...base.states, { state_machine: 1, name: 'Other', state_id: 20, generator_node: 5, first_transition: 3, transition_count: 0 }],
      bindings: [...base.bindings, bind(2, 2, 'weight', 0)],
    };
    const grown = applyAllLocally(two, [{ kind: 'add_state', state_machine: 0, name: 'Walk', value: 12, node: 6 }]);
    expect(grown.bindings.filter(b => b.owner_kind === 2).map(b => [b.owner_index, b.member_path])).toEqual([
      [0, 'enable'], [1, 'probability'], [3, 'weight'],
    ]);
    const shrunk = applyAllLocally(base, [{ kind: 'remove_state', state_machine: 0, state: 0 }]);
    expect(shrunk.bindings.filter(b => b.owner_kind === 2).map(b => [b.owner_index, b.member_path])).toEqual([[0, 'probability']]);
    expect(base.bindings).toHaveLength(4);
  });

  it('draws the generators below a node once each, follows state machines and edges, and survives a cycle', () => {
    // Constraint: detects a read-only generator view that loops on a shared or cyclic reference or drops a nested machine's states.
    const node = (name: string, kind: number) => ({ name, class_name: `hkb${name}`, kind });
    const base: HkxAuthoringModel = {
      ...model(),
      nodes: [node('Machine', 1), node('Blend', 2), node('ClipA', 3), node('ClipB', 3)],
      state_machines: [{ ...model().state_machines[0], node: 0 }],
      states: [
        { state_machine: 0, name: 'Idle', state_id: 10, generator_node: 1, first_transition: 0, transition_count: 0 },
        { state_machine: 0, name: 'Run', state_id: 11, generator_node: 2, first_transition: 0, transition_count: 0 },
      ],
      clips: [{ node: 2, animation: 'a.hkx', mode: 0 }, { node: 3, animation: 'b.hkx', mode: 0 }],
      variables: [{ name: 'Speed', type: 4 }],
      blenders: [{ node: 1, first_child: 0, child_count: 2, blend_parameter: 0 }],
      blend_children: [{ blender: 0, generator_node: 2, weight: 0.25 }, { blender: 0, generator_node: 3, weight: 0.75 }],
      bindings: [
        { owner_kind: 3, owner_index: 0, member_path: 'blendParameter', variable_index: 0, binding_type: 0 },
        { owner_kind: 4, owner_index: 1, member_path: 'weight', variable_index: 0, binding_type: 0 },
      ],
      edges: [{ parent: 3, child: 1, role: 1, slot: 0, weight: 0 }],
    };
    const { doc, defs, shown, truncated } = generatorToBlueprint(base, 0);
    expect(shown).toBe(4);
    expect(truncated).toBe(false);
    expect(doc.nodes.map(n => n.id).sort()).toEqual(['g0', 'g1', 'g2', 'g3']);
    expect(doc.wires.map(w => [w.from.node, w.to.node]).sort()).toEqual([
      ['g0', 'g1'], ['g0', 'g2'], ['g1', 'g2'], ['g1', 'g3'], ['g3', 'g1'],
    ]);
    expect(defs.g2.label).toBe('ClipA: a.hkx');
    expect(defs.g1.label).toBe('Blend (blend by Speed)');
    expect(defs.g1.pins.filter(p => p.dir === 'out').map(p => p.name)).toEqual(['blend child 0.25', 'blend child 0.75 (Speed)']);
  });

  it('draws the children of a plain blender from the blender tables and starts at the root when the file has no state machine', () => {
    // Constraint: detects a blend tree drawn with no children because the view read edges only, and a blender-rooted file that draws nothing.
    const node = (name: string, kind: number) => ({ name, class_name: `hkb${name}`, kind });
    const base: HkxAuthoringModel = {
      ...model(),
      root_node: 0,
      nodes: [node('Mix', 2), node('ClipA', 3), node('ClipB', 3)],
      state_machines: [], states: [], transitions: [],
      clips: [{ node: 1, animation: 'a.hkx', mode: 0 }, { node: 2, animation: 'b.hkx', mode: 0 }],
      blenders: [{ node: 0, first_child: 0, child_count: 2, blend_parameter: 0 }],
      blend_children: [{ blender: 0, generator_node: 1, weight: 1 }, { blender: 0, generator_node: 2, weight: 3 }],
    };
    const { doc, shown } = generatorToBlueprint(base, base.root_node);
    expect(shown).toBe(3);
    expect(doc.wires.map(w => [w.from.node, w.from.pin, w.to.node])).toEqual([['g0', 'c0', 'g1'], ['g0', 'c1', 'g2']]);
  });

  it('turns a host error into a thrown message and says when the host build lacks the RPC', async () => {
    // Constraint: detects a refusal reported as a graph, which would show an empty editor over a file that was never read.
    const refused = { ReadBehaviorAuthoring: vi.fn(async () => JSON.stringify({ error: 'packfile read failed' })) } as unknown as HkxHost;
    await expect(readBehaviorAuthoring(refused, 'a.hkx')).rejects.toThrow('packfile read failed');
    await expect(readBehaviorAuthoring({} as HkxHost, 'a.hkx')).rejects.toThrow('cannot author');
  });
});
