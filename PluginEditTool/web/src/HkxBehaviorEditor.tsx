import { useEffect, useMemo, useReducer, useRef, useState } from 'react';
import type { HkxHost } from './backend';
import BlueprintCanvas from './Blueprint/BlueprintCanvas';
import { emptyDocument, type BpDiagnostic } from './Blueprint/graphModel';
import { graphReducer, initialState, type GraphAction } from './Blueprint/graphReducer';
import {
  applyAllLocally, behaviorToBlueprint, BLEND_CHILD_OWNER, BLENDER_OWNER, CLIP_OWNER, describeValidation, editBehavior,
  readBehaviorAuthoring, runPosition, SELECTED_EDGE, transitionOfWire, validateBehavior,
  type HkxAuthoringModel, type HkxBehaviorEdit, type HkxGraphValidation,
} from './behaviorAuthoringClient';
import { generatorOfNodeId, generatorToBlueprint } from './behaviorGeneratorCanvas';

const variableTypes = ['bool', 'int8', 'int16', 'int32', 'real'];

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

interface Props {
  host?: HkxHost;
  path: string;
  disabled: boolean;
}

interface Pending {
  state_machine: number;
  state: number;
  toState: number;
}

const ownerOf = (node: string): number | null => (/^[sw]\d+$/.test(node) ? (node[0] === 'w' ? -1 : Number(node.slice(1))) : null);

const fieldOwners: Record<number, string> = { 3: 'blender', 4: 'blend child', 5: 'clip' };

const describeEdit = (edit: HkxBehaviorEdit): string => {
  switch (edit.kind) {
    case 'add_transition': return `add transition on event ${edit.transition.event_id} to state id ${edit.transition.to_state_id}`;
    case 'set_transition': return `change transition ${edit.index} to event ${edit.transition.event_id}, state id ${edit.transition.to_state_id}, priority ${edit.transition.priority}`;
    case 'remove_transition': return `remove transition ${edit.index}`;
    case 'add_event': return `add event ${edit.name}`;
    case 'rename_event': return `rename event ${edit.index} to ${edit.name}`;
    case 'set_event_flags': return `set flags of event ${edit.index} to ${edit.flags}`;
    case 'set_clip_animation': return `set animation of clip ${edit.index} to ${edit.name}`;
    case 'add_variable': return `add ${variableTypes[edit.flags] ?? 'type ' + edit.flags} variable ${edit.name}`;
    case 'rename_variable': return `rename variable ${edit.index} to ${edit.name}`;
    case 'set_start_state': return `set start state of machine ${edit.state_machine} to state id ${edit.value}`;
    case 'add_state': return `add state ${edit.name} (id ${edit.value}, generator node ${edit.node}) to machine ${edit.state_machine}`;
    case 'remove_state': return `remove state ${edit.state} from machine ${edit.state_machine}`;
    case 'set_binding': return `bind row ${edit.index} to variable ${edit.value}`;
    case 'add_binding': return `bind ${edit.name} of owner ${edit.index} (kind ${edit.flags}) to variable ${edit.value}`;
    case 'remove_binding': return `remove binding row ${edit.index}`;
    case 'add_blend_child': return `add child (generator node ${edit.node}, weight ${edit.real}) to blender ${edit.index}`;
    case 'remove_blend_child': return `remove blend child row ${edit.index}`;
    case 'add_selector_child': return `add child (generator node ${edit.value}) to selector node ${edit.node}`;
    case 'remove_selector_child': return `remove selector child edge ${edit.index}`;
    case 'remove_subtree': return `remove unreferenced generator node ${edit.node}`;
    case 'set_generator_field': return `set ${edit.name} of ${fieldOwners[edit.flags] ?? 'owner ' + edit.flags} ${edit.index} to ${edit.real ?? edit.value}`;
  }
};

const MACHINE_OWNER = 1;
const STATE_OWNER = 2;

// Invariant: owner kind and index are the Radicle binding owner values; the host refuses the bindings of a blend child, so it is never an owner here.
function bindingOwnerOf(model: HkxAuthoringModel, generator: number | null, state: number | null): { kind: number; index: number } | null {
  if (generator !== null) {
    const clip = model.clips.findIndex(c => c.node === generator);
    if (clip >= 0) return { kind: CLIP_OWNER, index: clip };
    const blender = model.blenders.findIndex(b => b.node === generator);
    if (blender >= 0) return { kind: BLENDER_OWNER, index: blender };
    const machine = model.state_machines.findIndex(m => m.node === generator);
    if (machine >= 0) return { kind: MACHINE_OWNER, index: machine };
  }
  return state !== null && state >= 0 && model.states[state] ? { kind: STATE_OWNER, index: state } : null;
}

interface FieldOption { label: string; flags: number; index: number; name: string; current: number | undefined; whole: boolean }

const fieldOption = (label: string, flags: number, index: number, name: string, current: number | undefined, whole = false): FieldOption =>
  ({ label, flags, index, name, current, whole });

// Invariant: the members and ranges are the ones set_generator_field accepts; the host still refuses a member a binding drives.
function fieldOptions(model: HkxAuthoringModel, generator: number | null): FieldOption[] {
  if (generator === null) return [];
  const clip = model.clips.findIndex(c => c.node === generator);
  if (clip >= 0) {
    const c = model.clips[clip];
    return [
      fieldOption('playback speed', CLIP_OWNER, clip, 'playbackSpeed', c.playback_speed),
      fieldOption('start time', CLIP_OWNER, clip, 'startTime', c.start_time),
      fieldOption('crop start', CLIP_OWNER, clip, 'cropStartAmountLocalTime', c.crop_start),
      fieldOption('crop end', CLIP_OWNER, clip, 'cropEndAmountLocalTime', c.crop_end),
      fieldOption('enforced duration', CLIP_OWNER, clip, 'enforcedDuration', c.enforced_duration),
      fieldOption('mode (0 single, 1 looping, 2 user controlled, 3 ping pong)', CLIP_OWNER, clip, 'mode', c.mode, true),
      fieldOption('flags (0 to 63)', CLIP_OWNER, clip, 'flags', c.flags, true),
    ];
  }
  const blender = model.blenders.findIndex(b => b.node === generator);
  if (blender < 0) return [];
  const b = model.blenders[blender];
  const children = model.blend_children.slice(b.first_child, b.first_child + b.child_count).flatMap((c, at) => [
    fieldOption(`child ${at} weight`, BLEND_CHILD_OWNER, b.first_child + at, 'weight', c.weight),
    fieldOption(`child ${at} world from model weight`, BLEND_CHILD_OWNER, b.first_child + at, 'worldFromModelWeight', c.world_from_model_weight),
  ]);
  return [
    fieldOption('blend parameter', BLENDER_OWNER, blender, 'blendParameter', b.blend_parameter),
    fieldOption('min cyclic blend parameter', BLENDER_OWNER, blender, 'minCyclicBlendParameter', b.min_cyclic_blend_parameter),
    fieldOption('max cyclic blend parameter', BLENDER_OWNER, blender, 'maxCyclicBlendParameter', b.max_cyclic_blend_parameter),
    ...children,
  ];
}

function FieldForm({ options, onQueue }: { options: FieldOption[]; onQueue: (edit: HkxBehaviorEdit) => void }) {
  const [at, setAt] = useState(0);
  const [text, setText] = useState('');
  const pick = options[Math.min(at, options.length - 1)];
  const number = Number(text);
  const valid = text.trim() !== '' && Number.isFinite(number) && (!pick.whole || Number.isInteger(number));
  return (
    <div className="nif-editor-actions">
      <select aria-label="Generator field" value={Math.min(at, options.length - 1)} onChange={e => setAt(Number(e.target.value))}>
        {options.map((o, i) => <option key={i} value={i}>{o.label}: {o.current ?? 'not sent by this host'}</option>)}
      </select>
      <input aria-label="Generator field value" value={text} onChange={e => setText(e.target.value)} />
      <button className="sidebar-action-btn" disabled={!valid}
              onClick={() => { onQueue({ kind: 'set_generator_field', flags: pick.flags, index: pick.index, name: pick.name, ...(pick.whole ? { value: number } : { real: number }) }); setText(''); }}>
        Queue field change
      </button>
    </div>
  );
}

const SELECTOR_CLASS = 'hkbManualSelectorGenerator';
const wholeNode = (text: string) => text.trim() !== '' && Number.isInteger(Number(text)) && Number(text) >= 0;

// Invariant: removals address the blend_children or edges table row, the host refuses the sync master, selected child and last child with its reason, and remove_subtree is applied by the host on save.
function ChildForm({ model, generator, onQueue }: { model: HkxAuthoringModel; generator: number; onQueue: (edit: HkxBehaviorEdit) => void }) {
  const [text, setText] = useState('');
  const [weight, setWeight] = useState('0.5');
  const [at, setAt] = useState(0);
  const blenderAt = model.blenders.findIndex(b => b.node === generator);
  const blender = model.blenders[blenderAt];
  const selector = model.nodes[generator]?.class_name === SELECTOR_CLASS;
  const rows: Array<{ row: number; label: string }> = blender
    ? model.blend_children.slice(blender.first_child, blender.first_child + blender.child_count)
        .map((c, pos) => ({ row: blender.first_child + pos, label: `child ${pos}: generator node ${c.generator_node}` }))
    : selector
      ? model.edges.flatMap((e, row) => (e.parent === generator && e.role === SELECTED_EDGE ? [{ row, label: `slot ${e.slot}: generator node ${e.child}` }] : []))
      : [];
  const pick = Math.min(at, rows.length - 1);
  const weightValue = Number(weight);
  const canAdd = wholeNode(text) && (!blender || (weight.trim() !== '' && Number.isFinite(weightValue)));
  const noun = selector ? 'selector' : 'blend';
  return (
    <div className="nif-editor-actions">
      {blender || selector ? (
        <>
          <input aria-label={`New ${noun} child node`} value={text} onChange={e => setText(e.target.value)} />
          {blender ? <input aria-label="New blend child weight" value={weight} onChange={e => setWeight(e.target.value)} /> : null}
          <button className="sidebar-action-btn" disabled={!canAdd}
                  onClick={() => {
                    onQueue(blender ? { kind: 'add_blend_child', index: blenderAt, node: Number(text), real: weightValue }
                                    : { kind: 'add_selector_child', node: generator, value: Number(text) });
                    setText('');
                  }}>
            Add {noun} child
          </button>
          <select aria-label={`${selector ? 'Selector' : 'Blend'} child to remove`} value={Math.max(pick, 0)} onChange={e => setAt(Number(e.target.value))}>
            {rows.map((r, i) => <option key={r.row} value={i}>{r.label}</option>)}
          </select>
          <button className="sidebar-action-btn" disabled={rows.length === 0}
                  onClick={() => onQueue(blender ? { kind: 'remove_blend_child', index: rows[pick].row } : { kind: 'remove_selector_child', index: rows[pick].row })}>
            Remove {noun} child
          </button>
        </>
      ) : null}
      {generator !== model.root_node ? (
        <>
          <button className="sidebar-action-btn" onClick={() => onQueue({ kind: 'remove_subtree', node: generator })}>Remove subtree</button>
          <span>Applies on save and renumbers nodes, so edits queued after it need a fresh read; refused while anything still points at the node.</span>
        </>
      ) : null}
    </div>
  );
}

function ClipForm({ animation, onQueue }: { animation: string; onQueue: (name: string) => void }) {
  const [text, setText] = useState(animation);
  return (
    <div className="nif-editor-actions">
      <input aria-label="Clip animation" value={text} onChange={e => setText(e.target.value)} />
      <button className="sidebar-action-btn" disabled={!text.trim() || text === animation} onClick={() => onQueue(text.trim())}>
        Queue animation change
      </button>
    </div>
  );
}

// Invariant: the queue is cleared only after the host reports written:true, so a refusal never loses the user's edits.
export default function HkxBehaviorEditor({ host, path, disabled }: Props) {
  const [source, setSource] = useState('');
  const [base, setBase] = useState<HkxAuthoringModel | null>(null);
  const [edits, setEdits] = useState<HkxBehaviorEdit[]>([]);
  const [machine, setMachine] = useState(0);
  const [pending, setPending] = useState<Pending | null>(null);
  const [pendingEvent, setPendingEvent] = useState(0);
  const [outPath, setOutPath] = useState('');
  const [overwrite, setOverwrite] = useState(false);
  const [newEvent, setNewEvent] = useState('');
  const [check, setCheck] = useState<HkxGraphValidation | null>(null);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [graph, dispatch] = useReducer(graphReducer, undefined, () => initialState({ ...emptyDocument('behavior'), id: 'behavior-empty' }));
  const positions = useRef<Record<string, { x: number; y: number }>>({});
  const [newState, setNewState] = useState({ name: '', id: '', node: '' });
  const [newVariable, setNewVariable] = useState('');
  const [newVariableType, setNewVariableType] = useState(0);
  const [renameAt, setRenameAt] = useState(0);
  const [renameTo, setRenameTo] = useState('');
  const [newBindingPath, setNewBindingPath] = useState('');
  const [newBindingVariable, setNewBindingVariable] = useState(0);
  const [generators, genDispatch] = useReducer(graphReducer, undefined, () => initialState({ ...emptyDocument('behavior'), id: 'generators-empty' }));
  const genPositions = useRef<{ id: string; at: Record<string, { x: number; y: number }> }>({ id: '', at: {} });
  const genSelected = useRef<string[]>([]);
  const stateSelected = useRef<string[]>([]);

  const model = useMemo(() => (base ? applyAllLocally(base, edits) : null), [base, edits]);
  const blueprint = useMemo(() => (model ? behaviorToBlueprint(model, machine) : null), [model, machine]);
  const pickedState = graph.selection.nodes[0] ? ownerOf(graph.selection.nodes[0]) : null;
  const generatorRoot = model
    ? (pickedState !== null && pickedState >= 0 ? model.states[pickedState]?.generator_node : model.state_machines[machine]?.node ?? model.root_node) ?? -1
    : -1;
  const generatorBlueprint = useMemo(
    () => (model && generatorRoot >= 0 ? generatorToBlueprint(model, generatorRoot) : null), [model, generatorRoot]);

  useEffect(() => {
    genPositions.current = { id: generators.doc.id, at: Object.fromEntries(generators.doc.nodes.map(n => [n.id, { x: n.x, y: n.y }])) };
  }, [generators.doc]);

  useEffect(() => {
    genSelected.current = generators.selection.nodes;
  }, [generators.selection]);

  useEffect(() => {
    if (!generatorBlueprint) return;
    const keep = genPositions.current.id === generatorBlueprint.doc.id ? genPositions.current.at : {};
    genDispatch({
      type: 'LOAD',
      doc: { ...generatorBlueprint.doc, nodes: generatorBlueprint.doc.nodes.map(n => ({ ...n, ...(keep[n.id] ?? {}) })) },
    });
    const still = genSelected.current.filter(id => generatorBlueprint.doc.nodes.some(n => n.id === id));
    if (still.length > 0) genDispatch({ type: 'SELECT', ids: still });
  }, [generatorBlueprint]);

  useEffect(() => {
    positions.current = Object.fromEntries(graph.doc.nodes.map(n => [n.id, { x: n.x, y: n.y }]));
  }, [graph.doc]);

  useEffect(() => {
    stateSelected.current = graph.selection.nodes;
  }, [graph.selection]);

  useEffect(() => {
    if (!blueprint) return;
    dispatch({
      type: 'LOAD',
      doc: { ...blueprint.doc, nodes: blueprint.doc.nodes.map(n => ({ ...n, ...(positions.current[n.id] ?? {}) })) },
    });
    const still = stateSelected.current.filter(id => blueprint.doc.nodes.some(n => n.id === id));
    if (still.length > 0) dispatch({ type: 'SELECT', ids: still });
  }, [blueprint]);

  const [seenPath, setSeenPath] = useState(path);
  if (seenPath !== path) {
    setSeenPath(path);
    setSource('');
    setBase(null);
    setEdits([]);
    setPending(null);
    setCheck(null);
    setNotice('');
    setError('');
  }

  useEffect(() => {
    positions.current = {};
    genPositions.current = { id: '', at: {} };
    genSelected.current = [];
    stateSelected.current = [];
  }, [path]);

  const diagByNode = useMemo(() => {
    const map = new Map<string, BpDiagnostic[]>();
    for (const d of blueprint?.diagnostics ?? []) map.set(d.nodeId ?? '', [...(map.get(d.nodeId ?? '') ?? []), d]);
    return map;
  }, [blueprint]);
  const noPins = useMemo(() => new Map<string, BpDiagnostic[]>(), []);

  const guard = async (work: () => Promise<void>) => {
    setBusy(true);
    setError('');
    try {
      await work();
    } catch (e) {
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  const open = (file: string, keepNotice = '') => guard(async () => {
    if (!host) throw new Error('no host');
    const found = await readBehaviorAuthoring(host, file);
    positions.current = {};
    setSource(file);
    setBase(found);
    setEdits([]);
    setMachine(0);
    setPending(null);
    setCheck(found.validation);
    setNotice(keepNotice || `${found.graph_name || 'behavior'}: ${found.states.length} states, ${found.transitions.length} transitions, ${describeValidation(found.validation)}`);
  });

  const queue = (edit: HkxBehaviorEdit) => {
    if (edits.some(e => e.kind === 'remove_subtree')) {
      setError('a queued subtree removal renumbers nodes: save or discard the edits and reopen before queuing more');
      return;
    }
    setError('');
    setEdits(list => [...list, edit]);
    setNotice('');
  };

  const stateIdOf = (index: number) => model?.states[index]?.state_id ?? 0;

  const connect = (from: { node: string; pin: string }, to: { node: string }) => {
    if (!model) return;
    const owner = ownerOf(from.node);
    const target = ownerOf(to.node);
    if (owner === null || target === null || target < 0) return;
    if (from.pin === 'new') {
      setPending({ state_machine: machine, state: owner, toState: target });
      return;
    }
    const at = Number(from.pin.slice(1));
    const run = model.transitions.filter(t => t.state_machine === machine && t.state === owner);
    const row = run[at];
    if (!row || !/^t\d+$/.test(from.pin)) return;
    queue({ kind: 'set_transition', state_machine: machine, state: owner, index: at,
            transition: { ...row, to_state_id: stateIdOf(target) } });
  };

  const removeSelected = () => {
    if (!model) return;
    const picks = graph.selection.wires.map(transitionOfWire).filter((n): n is number => n !== null);
    const found = picks.map(n => runPosition(model, n)).filter((p): p is NonNullable<typeof p> => p !== null);
    // Constraint: removals go last-first, so earlier run positions stay valid while the queue is applied in order.
    found.sort((a, b) => b.index - a.index);
    for (const p of found) queue({ kind: 'remove_transition', ...p });
  };

  const intercept = (action: GraphAction) => {
    switch (action.type) {
      case 'SELECT': case 'SELECT_NONE': case 'MOVE_NODES': case 'SET_POSITIONS':
        dispatch(action);
        return;
      case 'ADD_WIRE':
        connect(action.wire.from, action.wire.to);
        return;
      case 'DELETE_SELECTION':
        removeSelected();
        return;
      default:
    }
  };

  // Constraint: the generator canvas is read-only, so only selection and moves pass; wires and deletes are dropped.
  const generatorIntercept = (action: GraphAction) => {
    switch (action.type) {
      case 'SELECT': case 'SELECT_NONE': case 'MOVE_NODES': case 'SET_POSITIONS':
        genDispatch(action);
        return;
      default:
    }
  };

  const queueState = () => {
    if (!model) return;
    const run = model.states.filter(s => s.state_machine === machine);
    const id = newState.id.trim() === '' ? Math.max(-1, ...run.map(s => s.state_id)) + 1 : Number(newState.id);
    const node = Number(newState.node);
    if (!newState.name.trim() || !Number.isInteger(id) || newState.node.trim() === '' || !Number.isInteger(node) || node < 0) {
      setError('a new state needs a name, a whole state id and a generator node index');
      return;
    }
    setError('');
    queue({ kind: 'add_state', state_machine: machine, name: newState.name.trim(), value: id, node });
    setNewState({ name: '', id: '', node: '' });
  };

  const confirmPending = () => {
    if (!pending || !model) return;
    queue({ kind: 'add_transition', state_machine: pending.state_machine, state: pending.state,
            index: model.transitions.filter(t => t.state_machine === pending.state_machine && t.state === pending.state).length,
            transition: { event_id: pendingEvent, to_state_id: stateIdOf(pending.toState) } });
    setPending(null);
  };

  const preview = () => guard(async () => {
    if (!host) throw new Error('no host');
    const reply = await editBehavior(host, source, edits, '', false);
    setCheck(reply.after);
    setNotice(`preview: ${reply.applied} edits accepted, result has ${describeValidation(reply.after)}; nothing is written`);
  });

  const save = () => guard(async () => {
    if (!host) throw new Error('no host');
    const target = outPath.trim();
    if (!target) throw new Error('choose an output path before saving');
    const reply = await editBehavior(host, source, edits, target, overwrite);
    if (!reply.written) throw new Error('the host did not write the file; the edits are kept');
    const found = await readBehaviorAuthoring(host, target);
    positions.current = {};
    setSource(target);
    setBase(found);
    setEdits([]);
    setCheck(found.validation);
    setNotice(`saved ${reply.applied} edits to ${target} and reopened it: ${describeValidation(found.validation)}`);
  });

  const validate = () => guard(async () => {
    if (!host) throw new Error('no host');
    const found = await validateBehavior(host, source);
    setCheck(found);
    setNotice(`file: ${describeValidation(found)}`);
  });

  const selected = graph.selection.nodes[0];
  const owner = selected ? ownerOf(selected) : null;
  const run = model && owner !== null ? model.transitions.filter(t => t.state_machine === machine && t.state === owner) : [];
  const pickedGenerator = generators.selection.nodes[0] ? generatorOfNodeId(generators.selection.nodes[0]) : null;
  const clipAt = model && pickedGenerator !== null ? model.clips.findIndex(c => c.node === pickedGenerator) : -1;
  const blender = model && pickedGenerator !== null ? model.blenders.find(b => b.node === pickedGenerator) : undefined;
  const fields = useMemo(() => (model ? fieldOptions(model, pickedGenerator) : []), [model, pickedGenerator]);
  const bindingOwner = model ? bindingOwnerOf(model, pickedGenerator, owner) : null;
  const ownerRows = model && bindingOwner
    ? model.bindings.flatMap((b, at) => (b.owner_kind === bindingOwner.kind && b.owner_index === bindingOwner.index ? [{ b, at }] : []))
    : [];
  const blenderWeights = model && blender
    ? model.blend_children.slice(blender.first_child, blender.first_child + blender.child_count).map(c => Number(c.weight.toFixed(3))).join(', ')
    : '';

  return (
    <section aria-label="Behavior graph editor">
      <div className="nif-editor-actions">
        <button className="sidebar-action-btn" disabled={disabled || busy || !host || !path.trim()}
                onClick={() => void open(path.trim())}>
          Open graph editor
        </button>
        <button className="sidebar-action-btn" disabled={disabled || busy || !base}
                onClick={() => void validate()}>
          Validate file
        </button>
        <button className="sidebar-action-btn" disabled={disabled || busy || edits.length === 0}
                onClick={() => void preview()}>
          Preview edits
        </button>
      </div>
      {model && blueprint ? (
        <>
          <label>
            State machine{' '}
            <select aria-label="State machine" value={machine} onChange={e => { setMachine(Number(e.target.value)); setPending(null); }}>
              {model.state_machines.map((_m, at) => <option key={at} value={at}>machine {at}</option>)}
            </select>
          </label>
          <div style={{ height: 360, position: 'relative' }}>
            <BlueprintCanvas doc={graph.doc} defs={blueprint.defs} selection={graph.selection} diagByNode={diagByNode}
                             diagByPin={noPins} dispatch={intercept} onRequestNode={() => undefined} />
          </div>
          {pending ? (
            <form aria-label="New transition" onSubmit={e => { e.preventDefault(); confirmPending(); }}>
              <span>New transition to {model.states[pending.toState]?.name || `state ${pending.toState}`}: </span>
              <select aria-label="Transition event" value={pendingEvent} onChange={e => setPendingEvent(Number(e.target.value))}>
                {model.events.map((ev, at) => <option key={at} value={at}>{at}: {ev.name}</option>)}
              </select>
              <button type="submit" className="sidebar-action-btn">Queue transition</button>
              <button type="button" className="sidebar-action-btn" onClick={() => setPending(null)}>Cancel</button>
            </form>
          ) : null}
          {run.length > 0 ? (
            <ul className="nif-flabel" role="list" aria-label="Transitions of the selected state">
              {run.map((t, at) => (
                <li key={t.index}>
                  <select aria-label={`Event of transition ${at}`} value={t.event_id}
                          onChange={e => queue({ kind: 'set_transition', state_machine: machine, state: t.state, index: at,
                                                 transition: { ...t, event_id: Number(e.target.value) } })}>
                    {model.events.map((ev, i) => <option key={i} value={i}>{i}: {ev.name}</option>)}
                  </select>
                  {' '}to state id {t.to_state_id}, priority {t.priority}
                  <button className="sidebar-action-btn" onClick={() => queue({ kind: 'remove_transition', state_machine: machine, state: t.state, index: at })}>
                    Remove transition {at}
                  </button>
                </li>
              ))}
            </ul>
          ) : null}
          {owner !== null && owner >= 0 && model.states[owner] ? (
            <div className="nif-editor-actions">
              <button className="sidebar-action-btn"
                      onClick={() => queue({ kind: 'set_start_state', state_machine: machine, value: model.states[owner].state_id })}>
                Make state {model.states[owner].state_id} the start state
              </button>
              <button className="sidebar-action-btn"
                      onClick={() => queue({ kind: 'remove_state', state_machine: machine, state: owner })}>
                Remove state {model.states[owner].state_id}
              </button>
            </div>
          ) : null}
          <div className="nif-editor-actions">
            <input aria-label="New state name" value={newState.name} onChange={e => setNewState({ ...newState, name: e.target.value })} />
            <input aria-label="New state id" placeholder="next free" value={newState.id}
                   onChange={e => setNewState({ ...newState, id: e.target.value })} />
            <input aria-label="New state generator node" value={newState.node}
                   onChange={e => setNewState({ ...newState, node: e.target.value })} />
            <button className="sidebar-action-btn" onClick={queueState}>Add state</button>
          </div>
          {generatorBlueprint && generatorBlueprint.shown > 0 ? (
            <>
              <p>
                Generators below {model.nodes[generatorRoot]?.name || model.nodes[generatorRoot]?.class_name || `node ${generatorRoot}`}
                {' '}({generatorBlueprint.shown} shown{generatorBlueprint.truncated ? ', more not drawn' : ''}); read only
              </p>
              <div style={{ height: 300, position: 'relative' }}>
                <BlueprintCanvas doc={generators.doc} defs={generatorBlueprint.defs} selection={generators.selection}
                                 diagByNode={noPins} diagByPin={noPins} dispatch={generatorIntercept} onRequestNode={() => undefined} />
              </div>
              {clipAt >= 0 ? (
                <ClipForm key={`${clipAt}:${model.clips[clipAt].animation}`} animation={model.clips[clipAt].animation}
                          onQueue={name => queue({ kind: 'set_clip_animation', index: clipAt, name })} />
              ) : null}
              {blenderWeights ? <p>Blend child weights: {blenderWeights}</p> : null}
              {fields.length > 0 ? <FieldForm key={pickedGenerator} options={fields} onQueue={queue} /> : null}
              {pickedGenerator !== null ? <ChildForm key={`child:${pickedGenerator}`} model={model} generator={pickedGenerator} onQueue={queue} /> : null}
            </>
          ) : null}
          {bindingOwner ? (
            <ul className="nif-flabel" role="list" aria-label="Bindings of the selection">
              {ownerRows.map(({ b, at }) => (
                <li key={at}>
                  {b.member_path}{' '}
                  {b.binding_type === 0 ? (
                    <select aria-label={`Variable of binding ${at}`} value={b.variable_index}
                            onChange={e => queue({ kind: 'set_binding', index: at, value: Number(e.target.value) })}>
                      {b.variable_index < 0 || b.variable_index >= model.variables.length
                        ? <option value={b.variable_index}>{b.variable_index}: outside the table</option> : null}
                      {model.variables.map((v, i) => <option key={i} value={i}>{i}: {v.name}</option>)}
                    </select>
                  ) : <span>character property {b.variable_index}</span>}
                  <button className="sidebar-action-btn" onClick={() => queue({ kind: 'remove_binding', index: at })}>
                    Remove binding {at}
                  </button>
                </li>
              ))}
            </ul>
          ) : null}
          {bindingOwner && ownerRows.length > 0 && model.variables.length > 0 ? (
            <div className="nif-editor-actions">
              <input aria-label="New binding path" value={newBindingPath} onChange={e => setNewBindingPath(e.target.value)} />
              <select aria-label="New binding variable" value={newBindingVariable} onChange={e => setNewBindingVariable(Number(e.target.value))}>
                {model.variables.map((v, i) => <option key={i} value={i}>{i}: {v.name}</option>)}
              </select>
              <button className="sidebar-action-btn" disabled={!newBindingPath.trim()}
                      onClick={() => {
                        queue({ kind: 'add_binding', index: bindingOwner.index, flags: bindingOwner.kind, name: newBindingPath.trim(), value: newBindingVariable });
                        setNewBindingPath('');
                      }}>
                Add binding
              </button>
            </div>
          ) : null}
          <div className="nif-editor-actions">
            <input aria-label="New event name" value={newEvent} onChange={e => setNewEvent(e.target.value)} />
            <button className="sidebar-action-btn" disabled={!newEvent.trim()}
                    onClick={() => { queue({ kind: 'add_event', name: newEvent.trim() }); setNewEvent(''); }}>
              Add event
            </button>
          </div>
          <div className="nif-editor-actions">
            <input aria-label="New variable name" value={newVariable} onChange={e => setNewVariable(e.target.value)} />
            <select aria-label="New variable type" value={newVariableType} onChange={e => setNewVariableType(Number(e.target.value))}>
              {variableTypes.map((t, at) => <option key={at} value={at}>{t}</option>)}
            </select>
            <button className="sidebar-action-btn" disabled={!newVariable.trim()}
                    onClick={() => { queue({ kind: 'add_variable', name: newVariable.trim(), flags: newVariableType }); setNewVariable(''); }}>
              Add variable
            </button>
          </div>
          {model.variables.length > 0 ? (
            <div className="nif-editor-actions">
              <select aria-label="Variable to rename" value={renameAt} onChange={e => setRenameAt(Number(e.target.value))}>
                {model.variables.map((v, at) => <option key={at} value={at}>{at}: {v.name}</option>)}
              </select>
              <input aria-label="Variable new name" value={renameTo} onChange={e => setRenameTo(e.target.value)} />
              <button className="sidebar-action-btn" disabled={!renameTo.trim() || !model.variables[renameAt]}
                      onClick={() => { queue({ kind: 'rename_variable', index: renameAt, name: renameTo.trim() }); setRenameTo(''); }}>
                Rename variable
              </button>
            </div>
          ) : null}
          <ul className="nif-flabel" role="list" aria-label="Queued edits">
            {edits.map((edit, at) => <li key={at}>{describeEdit(edit)}</li>)}
          </ul>
          <div className="nif-editor-actions">
            <button className="sidebar-action-btn" disabled={edits.length === 0} onClick={() => setEdits([])}>Discard edits</button>
            <input aria-label="Output path" value={outPath} onChange={e => setOutPath(e.target.value)} />
            <label>
              <input type="checkbox" checked={overwrite} onChange={e => setOverwrite(e.target.checked)} /> Overwrite
            </label>
            <button className="sidebar-action-btn" disabled={disabled || busy || edits.length === 0} onClick={() => void save()}>
              Save edited graph
            </button>
          </div>
          {check && check.issues.length > 0 ? (
            <ul className="papyrus-warn" role="list" aria-label="Graph validation">
              {check.issues.map((issue, at) => <li key={at}>{issue.severity}: {issue.code} - {issue.message}</li>)}
            </ul>
          ) : null}
        </>
      ) : null}
      {notice ? <p role="status">{notice}</p> : null}
      {error ? <p role="alert">{error}</p> : null}
    </section>
  );
}
