import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxBehaviorEditor from './HkxBehaviorEditor';

const canvas = vi.hoisted(() => ({
  dispatch: null as null | ((action: unknown) => void),
  generators: null as null | ((action: unknown) => void),
  generatorNodes: [] as string[],
}));
vi.mock('./Blueprint/BlueprintCanvas', () => ({
  default: (props: { doc: { id: string; nodes: Array<{ id: string }> }; dispatch: (action: unknown) => void }) => {
    if (props.doc.id.startsWith('generators-')) {
      canvas.generators = props.dispatch;
      canvas.generatorNodes = props.doc.nodes.map(n => n.id);
    } else {
      canvas.dispatch = props.dispatch;
    }
    return null;
  },
}));

const PATH = 'C:\\mods\\graph.hkx';
const interval = { enter_event_id: -1, exit_event_id: -1, enter_time: 0, exit_time: 0 };
const row = (index: number, state: number, event: number, to: number) => ({
  index, state_machine: 0, state, event_id: event, to_state_id: to, from_nested_state_id: 0, to_nested_state_id: 0,
  priority: 3, flags: 0x0a00, trigger_interval: interval, initiate_interval: interval, effect_node: 2,
  condition_kind: 'string', condition: 'x > 1', condition_class: 'hkbStringCondition',
});

const authoring = {
  graph_name: 'Graph',
  root_node: 0,
  events: [{ name: 'Go', flags: 0 }, { name: 'Stop', flags: 0 }],
  variables: [],
  nodes: [],
  state_machines: [{ node: 0, start_state_id: 10, start_state_mode: 0, change_event_id: -1, first_state: 0, state_count: 2, first_wildcard: 2, wildcard_count: 0 }],
  states: [
    { state_machine: 0, name: 'Idle', state_id: 10, generator_node: 1, first_transition: 0, transition_count: 2 },
    { state_machine: 0, name: 'Run', state_id: 11, generator_node: 2, first_transition: 2, transition_count: 0 },
  ],
  transitions: [row(0, 0, 0, 11), row(1, 0, 1, 10)],
  validation: { errors: 0, warnings: 0, issue_total: 0, issues: [] },
};

const dragNewTransition = () => act(() => {
  canvas.dispatch?.({ type: 'ADD_WIRE', wire: { from: { node: 's0', pin: 'new' }, to: { node: 's1', pin: 'in' } }, defs: {} });
});

async function openAndQueueAdd(host: HkxHost) {
  render(<HkxBehaviorEditor host={host} path={PATH} disabled={false} />);
  fireEvent.click(screen.getByText('Open graph editor'));
  await waitFor(() => expect(canvas.dispatch).not.toBeNull());
  dragNewTransition();
  fireEvent.click(screen.getByText('Queue transition'));
  fireEvent.change(screen.getByLabelText('Output path'), { target: { value: 'C:\\mods\\out.hkx' } });
}

const queued = () => screen.getByLabelText('Queued edits').children.length;

afterEach(() => {
  cleanup();
  canvas.dispatch = null;
  canvas.generators = null;
  canvas.generatorNodes = [];
});

const node = (name: string, kind: number) => ({ name, class_name: `hkb${name}`, kind });
const rich = {
  ...authoring,
  variables: [{ name: 'Speed', type: 4 }, { name: 'Dir', type: 4 }],
  bindings: [{ owner_kind: 3, owner_index: 0, member_path: 'blendParameter', variable_index: 0, binding_type: 0 }],
  nodes: [node('Machine', 1), node('Blend', 2), node('ClipA', 3), node('ClipB', 3)],
  clips: [{ node: 2, animation: 'a.hkx', mode: 0 }, { node: 3, animation: 'b.hkx', mode: 0 }],
  blenders: [{ node: 1, first_child: 0, child_count: 1, blend_parameter: 0 }],
  blend_children: [{ blender: 0, generator_node: 3, weight: 0.5 }],
  edges: [],
};
const previewHost = (model: unknown) => {
  const EditBehaviorGraph = vi.fn(async () => JSON.stringify({ ok: true, applied: 1, written: false, size: 1, before: authoring.validation, after: authoring.validation }));
  return { EditBehaviorGraph, host: { ReadBehaviorAuthoring: vi.fn(async () => JSON.stringify(model)), EditBehaviorGraph } as unknown as HkxHost };
};
const pick = (id: string) => act(() => { canvas.dispatch?.({ type: 'SELECT', ids: [id] }); });

describe('HkxBehaviorEditor', () => {
  it('queues the state and variable forms as edits addressed at the machine and the selected state', async () => {
    // Constraint: detects a form that sends the wrong machine, state position or type code, which would edit another state or write a variable of the wrong type.
    const { EditBehaviorGraph, host } = previewHost(rich);
    render(<HkxBehaviorEditor host={host} path={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Open graph editor'));
    await waitFor(() => expect(canvas.dispatch).not.toBeNull());

    fireEvent.change(screen.getByLabelText('New state name'), { target: { value: 'Walk' } });
    fireEvent.change(screen.getByLabelText('New state generator node'), { target: { value: '2' } });
    fireEvent.click(screen.getByText('Add state'));
    fireEvent.change(screen.getByLabelText('New variable name'), { target: { value: 'Turn' } });
    fireEvent.change(screen.getByLabelText('New variable type'), { target: { value: '4' } });
    fireEvent.click(screen.getByText('Add variable'));
    fireEvent.change(screen.getByLabelText('Variable new name'), { target: { value: 'Velocity' } });
    fireEvent.click(screen.getByText('Rename variable'));
    pick('s1');
    fireEvent.click(screen.getByText('Make state 11 the start state'));
    pick('s1');
    fireEvent.click(screen.getByText('Remove state 11'));

    fireEvent.click(screen.getByText('Preview edits'));
    await waitFor(() => expect(EditBehaviorGraph).toHaveBeenCalled());
    expect(JSON.parse((EditBehaviorGraph.mock.calls[0] as unknown[])[1] as string)).toEqual([
      { kind: 'add_state', state_machine: 0, name: 'Walk', value: 12, node: 2 },
      { kind: 'add_variable', name: 'Turn', flags: 4 },
      { kind: 'rename_variable', index: 0, name: 'Velocity' },
      { kind: 'set_start_state', state_machine: 0, value: 11 },
      { kind: 'remove_state', state_machine: 0, state: 1 },
    ]);
  });

  it('edits the bindings of the selected blender by table row and keeps the selection while edits queue', async () => {
    // Constraint: detects a binding form that addresses the owner and not the table row, or one whose selection is lost after the first queued edit so the list vanishes.
    const { EditBehaviorGraph, host } = previewHost(rich);
    render(<HkxBehaviorEditor host={host} path={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Open graph editor'));
    await waitFor(() => expect(canvas.generators).not.toBeNull());
    expect(screen.queryByLabelText('Bindings of the selection')).toBeNull();

    act(() => { canvas.generators?.({ type: 'SELECT', ids: ['g1'] }); });
    fireEvent.change(screen.getByLabelText('Variable of binding 0'), { target: { value: '1' } });
    fireEvent.change(screen.getByLabelText('New binding path'), { target: { value: 'minCyclicBlendParameter' } });
    fireEvent.change(screen.getByLabelText('New binding variable'), { target: { value: '1' } });
    fireEvent.click(screen.getByText('Add binding'));
    expect((screen.getByLabelText('Variable of binding 0') as HTMLSelectElement).value).toBe('1');
    expect(screen.getByLabelText('Variable of binding 1')).toBeTruthy();
    fireEvent.click(screen.getByText('Remove binding 0'));

    fireEvent.click(screen.getByText('Preview edits'));
    await waitFor(() => expect(EditBehaviorGraph).toHaveBeenCalled());
    expect(JSON.parse((EditBehaviorGraph.mock.calls[0] as unknown[])[1] as string)).toEqual([
      { kind: 'set_binding', index: 0, value: 1 },
      { kind: 'add_binding', index: 0, flags: 3, name: 'minCyclicBlendParameter', value: 1 },
      { kind: 'remove_binding', index: 0 },
    ]);
  });

  it('draws the generators read only and queues only the clip animation from them', async () => {
    // Constraint: detects a generator canvas that accepts wire or delete edits, or a clip form that addresses the node and not the clip table position.
    const { EditBehaviorGraph, host } = previewHost(rich);
    render(<HkxBehaviorEditor host={host} path={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Open graph editor'));
    await waitFor(() => expect(canvas.generatorNodes.slice().sort()).toEqual(['g0', 'g1', 'g2', 'g3']));

    act(() => {
      canvas.generators?.({ type: 'ADD_WIRE', wire: { from: { node: 'g1', pin: 'c0' }, to: { node: 'g2', pin: 'in' } }, defs: {} });
      canvas.generators?.({ type: 'DELETE_SELECTION' });
    });
    expect(queued()).toBe(0);

    act(() => { canvas.generators?.({ type: 'SELECT', ids: ['g3'] }); });
    fireEvent.change(screen.getByLabelText('Clip animation'), { target: { value: 'c.hkx' } });
    fireEvent.click(screen.getByText('Queue animation change'));
    expect(screen.getByLabelText('Queued edits').textContent).toContain('set animation of clip 1 to c.hkx');

    fireEvent.click(screen.getByText('Preview edits'));
    await waitFor(() => expect(EditBehaviorGraph).toHaveBeenCalled());
    expect(JSON.parse((EditBehaviorGraph.mock.calls[0] as unknown[])[1] as string)).toEqual([
      { kind: 'set_clip_animation', index: 1, name: 'c.hkx' },
    ]);
  });

  it('queues a clip scalar as a real and a blend child weight by table row, and refuses a fractional whole number', async () => {
    // Constraint: detects a field form that sends a real as a whole number or the node index as the row, which would set another member or row.
    const { EditBehaviorGraph, host } = previewHost(rich);
    render(<HkxBehaviorEditor host={host} path={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Open graph editor'));
    await waitFor(() => expect(canvas.generatorNodes.slice().sort()).toEqual(['g0', 'g1', 'g2', 'g3']));

    act(() => { canvas.generators?.({ type: 'SELECT', ids: ['g3'] }); });
    fireEvent.change(screen.getByLabelText('Generator field'), { target: { value: '0' } });
    fireEvent.change(screen.getByLabelText('Generator field value'), { target: { value: '2.5' } });
    fireEvent.click(screen.getByText('Queue field change'));
    fireEvent.change(screen.getByLabelText('Generator field'), { target: { value: '5' } });
    fireEvent.change(screen.getByLabelText('Generator field value'), { target: { value: '1.5' } });
    expect((screen.getByText('Queue field change') as HTMLButtonElement).disabled).toBe(true);
    fireEvent.change(screen.getByLabelText('Generator field value'), { target: { value: '3' } });
    fireEvent.click(screen.getByText('Queue field change'));

    act(() => { canvas.generators?.({ type: 'SELECT', ids: ['g1'] }); });
    fireEvent.change(screen.getByLabelText('Generator field'), { target: { value: '3' } });
    fireEvent.change(screen.getByLabelText('Generator field value'), { target: { value: '0.75' } });
    fireEvent.click(screen.getByText('Queue field change'));
    expect(screen.getByLabelText('Queued edits').textContent).toContain('set weight of blend child 0 to 0.75');

    fireEvent.click(screen.getByText('Preview edits'));
    await waitFor(() => expect(EditBehaviorGraph).toHaveBeenCalled());
    expect(JSON.parse((EditBehaviorGraph.mock.calls[0] as unknown[])[1] as string)).toEqual([
      { kind: 'set_generator_field', flags: 5, index: 1, name: 'playbackSpeed', real: 2.5 },
      { kind: 'set_generator_field', flags: 5, index: 1, name: 'mode', value: 3 },
      { kind: 'set_generator_field', flags: 4, index: 0, name: 'weight', real: 0.75 },
    ]);
  });

  it('queues blend child and selector child edits by table row and node, and blocks more edits after a subtree removal', async () => {
    // Constraint: detects a child form that sends the blender node instead of its table index, a removal that addresses the position and not the table row, or an edit queued after remove_subtree against node indexes the host has renumbered.
    const { EditBehaviorGraph, host } = previewHost(rich);
    render(<HkxBehaviorEditor host={host} path={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Open graph editor'));
    await waitFor(() => expect(canvas.generators).not.toBeNull());

    act(() => { canvas.generators?.({ type: 'SELECT', ids: ['g1'] }); });
    expect((screen.getByText('Add blend child') as HTMLButtonElement).disabled).toBe(true);
    fireEvent.change(screen.getByLabelText('New blend child node'), { target: { value: '2' } });
    fireEvent.change(screen.getByLabelText('New blend child weight'), { target: { value: '0.25' } });
    fireEvent.click(screen.getByText('Add blend child'));
    fireEvent.change(screen.getByLabelText('Blend child to remove'), { target: { value: '1' } });
    fireEvent.click(screen.getByText('Remove blend child'));
    act(() => { canvas.generators?.({ type: 'SELECT', ids: ['g3'] }); });
    expect(screen.getByText(/Applies on save and renumbers nodes/)).toBeTruthy();
    fireEvent.click(screen.getByText('Remove subtree'));
    expect(queued()).toBe(3);
    fireEvent.click(screen.getByText('Remove subtree'));
    expect(queued()).toBe(3);
    expect(screen.getByRole('alert').textContent).toContain('renumbers nodes');

    fireEvent.click(screen.getByText('Preview edits'));
    await waitFor(() => expect(EditBehaviorGraph).toHaveBeenCalled());
    expect(JSON.parse((EditBehaviorGraph.mock.calls[0] as unknown[])[1] as string)).toEqual([
      { kind: 'add_blend_child', index: 0, node: 2, real: 0.25 },
      { kind: 'remove_blend_child', index: 1 },
      { kind: 'remove_subtree', node: 3 },
    ]);
  });

  it('queues a selector child add and a removal addressed by the edges table row', async () => {
    // Constraint: detects a selector form that sends the child position as the edge row, which would remove another selector's child when two selectors share a file.
    const selector = {
      ...rich,
      nodes: [node('Machine', 1), node('ManualSelectorGenerator', 4), node('ClipA', 3), node('ClipB', 3)],
      blenders: [], blend_children: [], bindings: [],
      edges: [{ parent: 1, child: 2, role: 3, slot: 0, weight: 0 }, { parent: 1, child: 3, role: 3, slot: 1, weight: 0 }],
    };
    const { EditBehaviorGraph, host } = previewHost(selector);
    render(<HkxBehaviorEditor host={host} path={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Open graph editor'));
    await waitFor(() => expect(canvas.generatorNodes.slice().sort()).toEqual(['g0', 'g1', 'g2', 'g3']));

    act(() => { canvas.generators?.({ type: 'SELECT', ids: ['g1'] }); });
    fireEvent.change(screen.getByLabelText('New selector child node'), { target: { value: '3' } });
    fireEvent.click(screen.getByText('Add selector child'));
    fireEvent.change(screen.getByLabelText('Selector child to remove'), { target: { value: '1' } });
    fireEvent.click(screen.getByText('Remove selector child'));

    fireEvent.click(screen.getByText('Preview edits'));
    await waitFor(() => expect(EditBehaviorGraph).toHaveBeenCalled());
    expect(JSON.parse((EditBehaviorGraph.mock.calls[0] as unknown[])[1] as string)).toEqual([
      { kind: 'add_selector_child', node: 1, value: 3 },
      { kind: 'remove_selector_child', index: 1 },
    ]);
  });

  it('draws the generators from the root when the file has no state machine', async () => {
    // Constraint: detects a generator view that stays empty for a file whose root generator is a blender, because it looked for a state machine only.
    const blendOnly = {
      ...authoring, root_node: 0, state_machines: [], states: [], transitions: [],
      nodes: [node('Mix', 2), node('ClipA', 3), node('ClipB', 3)],
      clips: [{ node: 1, animation: 'a.hkx', mode: 0 }, { node: 2, animation: 'b.hkx', mode: 0 }],
      blenders: [{ node: 0, first_child: 0, child_count: 2, blend_parameter: 0 }],
      blend_children: [{ blender: 0, generator_node: 1, weight: 1 }, { blender: 0, generator_node: 2, weight: 3 }],
    };
    const { host } = previewHost(blendOnly);
    render(<HkxBehaviorEditor host={host} path={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Open graph editor'));
    await waitFor(() => expect(canvas.generatorNodes.slice().sort()).toEqual(['g0', 'g1', 'g2']));
  });

  it('queues an add only after the new-transition form is confirmed, addressed at the end of the state run', async () => {
    // Constraint: detects a wire drag that edits at once or addresses the wrong run position, which would write a row the user never confirmed.
    const EditBehaviorGraph = vi.fn(async () => JSON.stringify({ ok: true, applied: 1, written: false, size: 1, before: authoring.validation, after: authoring.validation }));
    const host = { ReadBehaviorAuthoring: vi.fn(async () => JSON.stringify(authoring)), EditBehaviorGraph } as unknown as HkxHost;
    render(<HkxBehaviorEditor host={host} path={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Open graph editor'));
    await waitFor(() => expect(canvas.dispatch).not.toBeNull());

    dragNewTransition();
    expect(queued()).toBe(0);
    expect(screen.getByLabelText('New transition')).toBeTruthy();
    fireEvent.click(screen.getByText('Queue transition'));
    expect(queued()).toBe(1);
    expect(screen.getByLabelText('Queued edits').textContent).toContain('add transition on event 0 to state id 11');

    fireEvent.click(screen.getByText('Preview edits'));
    await waitFor(() => expect(EditBehaviorGraph).toHaveBeenCalled());
    expect(JSON.parse((EditBehaviorGraph.mock.calls[0] as unknown[])[1] as string)).toEqual([
      { kind: 'add_transition', state_machine: 0, state: 0, index: 2, transition: { event_id: 0, to_state_id: 11 } },
    ]);
    expect((EditBehaviorGraph.mock.calls[0] as unknown[])[2]).toBe('');
  });

  it('keeps the queue and shows the reason when the host refuses the save', async () => {
    // Constraint: detects a save that drops the queued edits on a refusal, which loses the user's work and hides why.
    const EditBehaviorGraph = vi.fn(async () => JSON.stringify({ error: 'edit 0: the edit adds a transition.event_range error' }));
    const host = { ReadBehaviorAuthoring: vi.fn(async () => JSON.stringify(authoring)), EditBehaviorGraph } as unknown as HkxHost;
    await openAndQueueAdd(host);

    fireEvent.click(screen.getByText('Save edited graph'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('event_range'));
    expect(queued()).toBe(1);
    expect((host.ReadBehaviorAuthoring as ReturnType<typeof vi.fn>).mock.calls).toHaveLength(1);
  });

  it('sends later edits against the saved file, not the file that was first opened', async () => {
    // Constraint: detects an editor that keeps addressing the original path after a save, so a second edit is applied to stale bytes and overwrites the first save.
    const reply = (written: boolean) => JSON.stringify({ ok: true, applied: 1, written, size: 1, before: authoring.validation, after: authoring.validation });
    const EditBehaviorGraph = vi.fn().mockResolvedValueOnce(reply(true)).mockResolvedValueOnce(reply(false));
    const host = { ReadBehaviorAuthoring: vi.fn(async () => JSON.stringify(authoring)), EditBehaviorGraph } as unknown as HkxHost;
    await openAndQueueAdd(host);

    fireEvent.click(screen.getByText('Save edited graph'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('saved 1 edits'));
    expect(EditBehaviorGraph.mock.calls[0][0]).toBe(PATH);

    dragNewTransition();
    fireEvent.click(screen.getByText('Queue transition'));
    fireEvent.click(screen.getByText('Preview edits'));
    await waitFor(() => expect(EditBehaviorGraph).toHaveBeenCalledTimes(2));
    expect(EditBehaviorGraph.mock.calls[1][0]).toBe('C:\\mods\\out.hkx');
  });

  it('drops the loaded graph and the queue when the path changes', async () => {
    // Constraint: detects a path change that keeps the old graph and queue, so edits made for one file are saved against another.
    const host = { ReadBehaviorAuthoring: vi.fn(async () => JSON.stringify(authoring)), EditBehaviorGraph: vi.fn() } as unknown as HkxHost;
    const view = render(<HkxBehaviorEditor host={host} path={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Open graph editor'));
    await waitFor(() => expect(canvas.dispatch).not.toBeNull());
    dragNewTransition();
    fireEvent.click(screen.getByText('Queue transition'));
    expect(queued()).toBe(1);

    view.rerender(<HkxBehaviorEditor host={host} path="C:\\mods\\other.hkx" disabled={false} />);
    await waitFor(() => expect(screen.queryByLabelText('Queued edits')).toBeNull());
    expect(screen.queryByText('Preview edits')?.hasAttribute('disabled')).toBe(true);
    expect(screen.queryByText('Save edited graph')).toBeNull();
  });

  it('clears the queue only after the host reports written and reopens the written file', async () => {
    // Constraint: detects a save that clears the queue on a preview-style reply (written false), so edits vanish though no file was written.
    const reply = (written: boolean) => JSON.stringify({ ok: true, applied: 1, written, size: 1, before: authoring.validation, after: authoring.validation });
    const EditBehaviorGraph = vi.fn().mockResolvedValueOnce(reply(false)).mockResolvedValueOnce(reply(true));
    const ReadBehaviorAuthoring = vi.fn(async () => JSON.stringify(authoring));
    const host = { ReadBehaviorAuthoring, EditBehaviorGraph } as unknown as HkxHost;
    await openAndQueueAdd(host);

    fireEvent.click(screen.getByText('Save edited graph'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('did not write'));
    expect(queued()).toBe(1);

    fireEvent.click(screen.getByText('Save edited graph'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('saved 1 edits to C:\\mods\\out.hkx'));
    expect(queued()).toBe(0);
    expect(ReadBehaviorAuthoring).toHaveBeenLastCalledWith('C:\\mods\\out.hkx');
    expect(EditBehaviorGraph.mock.calls[1]).toEqual([PATH, expect.any(String), 'C:\\mods\\out.hkx', false]);
  });
});
