import type { HkxHost } from './backend';
import type { BpDiagnostic, BpDocument, BpNode, BpNodeDef, BpWire } from './Blueprint/graphModel';
import { autoLayout } from './Blueprint/layout';

export interface HkxAuthoringInterval {
  enter_event_id: number;
  exit_event_id: number;
  enter_time: number;
  exit_time: number;
}

export interface HkxAuthoringTransition {
  index: number;
  state_machine: number;
  state: number;
  event_id: number;
  to_state_id: number;
  from_nested_state_id: number;
  to_nested_state_id: number;
  priority: number;
  flags: number;
  trigger_interval: HkxAuthoringInterval;
  initiate_interval: HkxAuthoringInterval;
  effect_node: number;
  condition_kind: string;
  condition: string;
  condition_class: string;
}

export interface HkxAuthoringState {
  state_machine: number;
  name: string;
  state_id: number;
  generator_node: number;
  enable?: boolean;
  first_transition: number;
  transition_count: number;
}

export interface HkxAuthoringMachine {
  node: number;
  start_state_id: number;
  start_state_mode: number;
  change_event_id: number;
  sync_variable_index?: number;
  first_state: number;
  state_count: number;
  first_wildcard: number;
  wildcard_count: number;
}

export interface HkxGraphIssue {
  severity: 'error' | 'warning';
  code: string;
  table: string;
  index: number;
  message: string;
}

export interface HkxGraphValidation {
  errors: number;
  warnings: number;
  issue_total: number;
  issues: HkxGraphIssue[];
}

export interface HkxAuthoringClip {
  node: number; animation: string; mode: number; flags?: number; start_time?: number; playback_speed?: number;
  crop_start?: number; crop_end?: number; enforced_duration?: number;
}
export interface HkxAuthoringBlender {
  node: number; first_child: number; child_count: number; blend_parameter: number;
  min_cyclic_blend_parameter?: number; max_cyclic_blend_parameter?: number; index_of_sync_master_child?: number;
}
export interface HkxAuthoringBlendChild { blender: number; generator_node: number; weight: number; world_from_model_weight?: number }
export interface HkxAuthoringEdge { parent: number; child: number; role: number; slot: number; weight: number }
export interface HkxAuthoringBinding {
  owner_kind: number;
  owner_index: number;
  member_path: string;
  variable_index: number;
  binding_type: number;
}

export interface HkxAuthoringModel {
  graph_name: string;
  root_node: number;
  events: Array<{ name: string; flags: number }>;
  variables: Array<{ name: string; type: number }>;
  nodes: Array<{ name: string; class_name: string; kind: number }>;
  state_machines: HkxAuthoringMachine[];
  states: HkxAuthoringState[];
  transitions: HkxAuthoringTransition[];
  clips: HkxAuthoringClip[];
  blenders: HkxAuthoringBlender[];
  blend_children: HkxAuthoringBlendChild[];
  edges: HkxAuthoringEdge[];
  bindings: HkxAuthoringBinding[];
  validation: HkxGraphValidation;
}

export type HkxBehaviorEdit =
  | { kind: 'add_transition'; state_machine: number; state: number; index: number;
      transition: { event_id: number; to_state_id: number } & Partial<HkxAuthoringTransition> }
  | { kind: 'set_transition'; state_machine: number; state: number; index: number; transition: HkxAuthoringTransition }
  | { kind: 'remove_transition'; state_machine: number; state: number; index: number }
  | { kind: 'add_event'; name: string }
  | { kind: 'rename_event'; index: number; name: string }
  | { kind: 'set_event_flags'; index: number; flags: number }
  | { kind: 'set_clip_animation'; index: number; name: string }
  | { kind: 'add_variable'; name: string; flags: number }
  | { kind: 'rename_variable'; index: number; name: string }
  | { kind: 'set_start_state'; state_machine: number; value: number }
  | { kind: 'add_state'; state_machine: number; name: string; value: number; node: number }
  | { kind: 'remove_state'; state_machine: number; state: number }
  | { kind: 'set_binding'; index: number; value: number }
  | { kind: 'add_binding'; index: number; flags: number; name: string; value: number }
  | { kind: 'remove_binding'; index: number }
  | { kind: 'set_generator_field'; flags: number; index: number; name: string; real?: number; value?: number }
  | { kind: 'add_blend_child'; index: number; node: number; real: number }
  | { kind: 'remove_blend_child'; index: number }
  | { kind: 'add_selector_child'; node: number; value: number }
  | { kind: 'remove_selector_child'; index: number }
  | { kind: 'remove_subtree'; node: number };

export interface HkxBehaviorEditReply {
  ok: boolean;
  applied: number;
  written: boolean;
  size: number;
  before: HkxGraphValidation;
  after: HkxGraphValidation;
  path?: string;
}

function parsed<T>(text: string): T {
  const value = JSON.parse(text) as { error?: unknown } | null;
  if (value && typeof value.error === 'string') throw new Error(value.error);
  return value as T;
}

// Requires the host to carry the authoring RPCs; an older host build reports why instead of an empty graph.
export async function readBehaviorAuthoring(host: HkxHost, path: string): Promise<HkxAuthoringModel> {
  if (!host.ReadBehaviorAuthoring) throw new Error('this host build cannot author a behavior graph');
  const model = parsed<HkxAuthoringModel>(await host.ReadBehaviorAuthoring(path));
  if (!Array.isArray(model.states) || !Array.isArray(model.transitions) || !Array.isArray(model.state_machines)) {
    throw new Error('behavior authoring reply has no states');
  }
  return { ...model, clips: model.clips ?? [], blenders: model.blenders ?? [], blend_children: model.blend_children ?? [],
           edges: model.edges ?? [], bindings: model.bindings ?? [] };
}

export async function validateBehavior(host: HkxHost, path: string): Promise<HkxGraphValidation> {
  if (!host.ValidateBehaviorGraph) throw new Error('this host build cannot validate a behavior graph');
  return parsed<HkxGraphValidation>(await host.ValidateBehaviorGraph(path));
}

// Constraint: an empty outPath previews; the host writes only after the Radicle editor accepted every edit.
export async function editBehavior(host: HkxHost, path: string, edits: HkxBehaviorEdit[], outPath: string,
                                   overwrite: boolean): Promise<HkxBehaviorEditReply> {
  if (!host.EditBehaviorGraph) throw new Error('this host build cannot edit a behavior graph');
  if (edits.length === 0) throw new Error('there are no edits to apply');
  return parsed<HkxBehaviorEditReply>(await host.EditBehaviorGraph(path, JSON.stringify(edits), outPath, overwrite));
}

export const describeValidation = (v: HkxGraphValidation): string =>
  `${v.errors} errors, ${v.warnings} warnings` + (v.issue_total > v.issues.length ? ` (${v.issues.length} of ${v.issue_total} listed)` : '');

const runOf = (model: HkxAuthoringModel, machine: number, state: number): HkxAuthoringTransition[] =>
  model.transitions.filter(t => t.state_machine === machine && t.state === state);

const emptyInterval: HkxAuthoringInterval = { enter_event_id: -1, exit_event_id: -1, enter_time: 0, exit_time: 0 };

const reindexed = (model: HkxAuthoringModel, transitions: HkxAuthoringTransition[]): HkxAuthoringModel =>
  ({ ...model, transitions: transitions.map((t, index) => ({ ...t, index })) });

const STATE_OWNER = 2;

// Invariant: states are listed per machine in file order, so adding or removing one shifts the state index of every later machine and of the transitions they own.
function withStateAdded(model: HkxAuthoringModel, edit: Extract<HkxBehaviorEdit, { kind: 'add_state' }>): HkxAuthoringModel {
  const machine = model.state_machines[edit.state_machine];
  if (!machine) return model;
  const at = machine.first_state + machine.state_count;
  const row: HkxAuthoringState = {
    state_machine: edit.state_machine, name: edit.name, state_id: edit.value, generator_node: edit.node, enable: true,
    first_transition: 0, transition_count: 0,
  };
  return {
    ...model,
    states: [...model.states.slice(0, at), row, ...model.states.slice(at)],
    state_machines: model.state_machines.map((m, i) =>
      i === edit.state_machine ? { ...m, state_count: m.state_count + 1 } : m.first_state >= at ? { ...m, first_state: m.first_state + 1 } : m),
    transitions: model.transitions.map(t => (t.state >= at ? { ...t, state: t.state + 1 } : t)),
    bindings: model.bindings.map(b => (b.owner_kind === STATE_OWNER && b.owner_index >= at ? { ...b, owner_index: b.owner_index + 1 } : b)),
  };
}

function withStateRemoved(model: HkxAuthoringModel, machineAt: number, state: number): HkxAuthoringModel {
  const machine = model.state_machines[machineAt];
  if (!machine || state < machine.first_state || state >= machine.first_state + machine.state_count) return model;
  return {
    ...model,
    states: model.states.filter((_, i) => i !== state),
    state_machines: model.state_machines.map((m, i) =>
      i === machineAt ? { ...m, state_count: m.state_count - 1 } : m.first_state > state ? { ...m, first_state: m.first_state - 1 } : m),
    transitions: model.transitions.filter(t => t.state !== state).map((t, index) => ({ ...t, index, state: t.state > state ? t.state - 1 : t.state })),
    bindings: model.bindings
      .filter(b => !(b.owner_kind === STATE_OWNER && b.owner_index === state))
      .map(b => (b.owner_kind === STATE_OWNER && b.owner_index > state ? { ...b, owner_index: b.owner_index - 1 } : b)),
  };
}

// Invariant: the rows of one owner are contiguous and a new row goes after the owner's last one; an owner with no row is appended at the end, where the host's position is not known, so the editor offers add_binding only for owners that already have a row.
function withBindingAdded(model: HkxAuthoringModel, edit: Extract<HkxBehaviorEdit, { kind: 'add_binding' }>): HkxAuthoringModel {
  const row: HkxAuthoringBinding = {
    owner_kind: edit.flags, owner_index: edit.index, member_path: edit.name, variable_index: edit.value, binding_type: 0,
  };
  let last = -1;
  model.bindings.forEach((b, i) => { if (b.owner_kind === edit.flags && b.owner_index === edit.index) last = i; });
  const at = last < 0 ? model.bindings.length : last + 1;
  return { ...model, bindings: [...model.bindings.slice(0, at), row, ...model.bindings.slice(at)] };
}

export const CLIP_OWNER = 5;
export const BLENDER_OWNER = 3;
export const BLEND_CHILD_OWNER = 4;

// Invariant: the keys are the host's member names for set_generator_field and the model fields read_behavior_model sends for them.
const FIELD_KEYS: Record<number, Record<string, string>> = {
  [CLIP_OWNER]: {
    playbackSpeed: 'playback_speed', startTime: 'start_time', cropStartAmountLocalTime: 'crop_start',
    cropEndAmountLocalTime: 'crop_end', enforcedDuration: 'enforced_duration', mode: 'mode', flags: 'flags',
  },
  [BLENDER_OWNER]: { blendParameter: 'blend_parameter', minCyclicBlendParameter: 'min_cyclic_blend_parameter', maxCyclicBlendParameter: 'max_cyclic_blend_parameter' },
  [BLEND_CHILD_OWNER]: { weight: 'weight', worldFromModelWeight: 'world_from_model_weight' },
};

function withGeneratorField(model: HkxAuthoringModel, edit: Extract<HkxBehaviorEdit, { kind: 'set_generator_field' }>): HkxAuthoringModel {
  const key = FIELD_KEYS[edit.flags]?.[edit.name];
  if (!key) return model;
  const to = edit.real ?? edit.value ?? 0;
  const set = <T extends object>(rows: T[]): T[] => rows.map((r, i) => (i === edit.index ? { ...r, [key]: to } : r));
  if (edit.flags === CLIP_OWNER) return { ...model, clips: set(model.clips) };
  if (edit.flags === BLENDER_OWNER) return { ...model, blenders: set(model.blenders) };
  return { ...model, blend_children: set(model.blend_children) };
}

export const SELECTED_EDGE = 3;

// Invariant: child rows of one blender are contiguous from first_child, so an append or removal shifts first_child of every later blender and the owner index of every later blend child binding; the sync master index follows the host rule and moves only for a removal before it.
function withBlendChildAdded(model: HkxAuthoringModel, edit: Extract<HkxBehaviorEdit, { kind: 'add_blend_child' }>): HkxAuthoringModel {
  const blender = model.blenders[edit.index];
  if (!blender) return model;
  const at = blender.first_child + blender.child_count;
  const row: HkxAuthoringBlendChild = { blender: edit.index, generator_node: edit.node, weight: edit.real, world_from_model_weight: 0 };
  return {
    ...model,
    blend_children: [...model.blend_children.slice(0, at), row, ...model.blend_children.slice(at)],
    blenders: model.blenders.map((b, i) => (i === edit.index ? { ...b, child_count: b.child_count + 1 } : i > edit.index ? { ...b, first_child: b.first_child + 1 } : b)),
    bindings: model.bindings.map(b => (b.owner_kind === BLEND_CHILD_OWNER && b.owner_index >= at ? { ...b, owner_index: b.owner_index + 1 } : b)),
  };
}

function withBlendChildRemoved(model: HkxAuthoringModel, at: number): HkxAuthoringModel {
  const row = model.blend_children[at];
  const blender = row ? model.blenders[row.blender] : undefined;
  if (!row || !blender) return model;
  const position = at - blender.first_child;
  const sync = blender.index_of_sync_master_child;
  return {
    ...model,
    blend_children: model.blend_children.filter((_, i) => i !== at),
    blenders: model.blenders.map((b, i) => (i === row.blender
      ? { ...b, child_count: b.child_count - 1, ...(sync !== undefined && sync > position ? { index_of_sync_master_child: sync - 1 } : {}) }
      : i > row.blender ? { ...b, first_child: b.first_child - 1 } : b)),
    bindings: model.bindings
      .filter(b => !(b.owner_kind === BLEND_CHILD_OWNER && b.owner_index === at))
      .map(b => (b.owner_kind === BLEND_CHILD_OWNER && b.owner_index > at ? { ...b, owner_index: b.owner_index - 1 } : b)),
  };
}

function withSelectorChildAdded(model: HkxAuthoringModel, edit: Extract<HkxBehaviorEdit, { kind: 'add_selector_child' }>): HkxAuthoringModel {
  const own = model.edges.filter(e => e.parent === edit.node && e.role === SELECTED_EDGE);
  if (model.nodes[edit.node]?.class_name !== 'hkbManualSelectorGenerator') return model;
  const row: HkxAuthoringEdge = { parent: edit.node, child: edit.value, role: SELECTED_EDGE, slot: own.length, weight: 0 };
  let last = -1;
  model.edges.forEach((e, i) => { if (e.parent === edit.node && e.role === SELECTED_EDGE) last = i; });
  const at = last < 0 ? model.edges.length : last + 1;
  return { ...model, edges: [...model.edges.slice(0, at), row, ...model.edges.slice(at)] };
}

function withSelectorChildRemoved(model: HkxAuthoringModel, at: number): HkxAuthoringModel {
  const row = model.edges[at];
  if (!row || row.role !== SELECTED_EDGE) return model;
  return {
    ...model,
    edges: model.edges.filter((_, i) => i !== at).map(e => (e.parent === row.parent && e.role === SELECTED_EDGE && e.slot > row.slot ? { ...e, slot: e.slot - 1 } : e)),
  };
}

// Invariant: this mirrors the host's addressing (run position, appended rows at the run end), so queued edits keep valid indexes.
export function applyLocally(model: HkxAuthoringModel, edit: HkxBehaviorEdit): HkxAuthoringModel {
  switch (edit.kind) {
    case 'add_event':
      return { ...model, events: [...model.events, { name: edit.name, flags: 0 }] };
    case 'rename_event':
      return { ...model, events: model.events.map((e, i) => (i === edit.index ? { ...e, name: edit.name } : e)) };
    case 'set_event_flags':
      return { ...model, events: model.events.map((e, i) => (i === edit.index ? { ...e, flags: edit.flags } : e)) };
    case 'set_clip_animation':
      return { ...model, clips: model.clips.map((c, i) => (i === edit.index ? { ...c, animation: edit.name } : c)) };
    case 'add_variable':
      return { ...model, variables: [...model.variables, { name: edit.name, type: edit.flags }] };
    case 'rename_variable':
      return { ...model, variables: model.variables.map((v, i) => (i === edit.index ? { ...v, name: edit.name } : v)) };
    case 'set_start_state':
      return { ...model, state_machines: model.state_machines.map((m, i) => (i === edit.state_machine ? { ...m, start_state_id: edit.value } : m)) };
    case 'set_binding':
      return { ...model, bindings: model.bindings.map((b, i) => (i === edit.index ? { ...b, variable_index: edit.value } : b)) };
    case 'remove_binding':
      return { ...model, bindings: model.bindings.filter((_, i) => i !== edit.index) };
    case 'add_binding':
      return withBindingAdded(model, edit);
    case 'set_generator_field':
      return withGeneratorField(model, edit);
    case 'add_blend_child':
      return withBlendChildAdded(model, edit);
    case 'remove_blend_child':
      return withBlendChildRemoved(model, edit.index);
    case 'add_selector_child':
      return withSelectorChildAdded(model, edit);
    case 'remove_selector_child':
      return withSelectorChildRemoved(model, edit.index);
    case 'remove_subtree':
      return model;
    case 'add_state':
      return withStateAdded(model, edit);
    case 'remove_state':
      return withStateRemoved(model, edit.state_machine, edit.state);
    case 'add_transition': {
      const run = runOf(model, edit.state_machine, edit.state);
      const row: HkxAuthoringTransition = {
        index: -1, state_machine: edit.state_machine, state: edit.state,
        from_nested_state_id: 0, to_nested_state_id: 0, priority: 0, flags: 0,
        trigger_interval: emptyInterval, initiate_interval: emptyInterval, effect_node: -1,
        condition_kind: 'none', condition: '', condition_class: '', ...edit.transition,
      };
      const rows = [...model.transitions];
      const before = edit.index < run.length ? rows.indexOf(run[edit.index]) : (run.length > 0 ? rows.indexOf(run[run.length - 1]) + 1 : rows.length);
      rows.splice(before, 0, row);
      return reindexed(model, rows);
    }
    case 'set_transition': {
      const target = runOf(model, edit.state_machine, edit.state)[edit.index];
      return reindexed(model, model.transitions.map(t => (t === target ? { ...edit.transition, index: t.index, state: t.state, state_machine: t.state_machine } : t)));
    }
    case 'remove_transition': {
      const target = runOf(model, edit.state_machine, edit.state)[edit.index];
      return reindexed(model, model.transitions.filter(t => t !== target));
    }
  }
}

export function runPosition(model: HkxAuthoringModel, transition: number):
    { state_machine: number; state: number; index: number } | null {
  const row = model.transitions[transition];
  if (!row) return null;
  return { state_machine: row.state_machine, state: row.state, index: runOf(model, row.state_machine, row.state).indexOf(row) };
}

export const applyAllLocally =(model: HkxAuthoringModel, edits: HkxBehaviorEdit[]): HkxAuthoringModel =>
  edits.reduce(applyLocally, model);

export const stateNodeId = (state: number) => `s${state}`;
export const wildcardNodeId = (machine: number) => `w${machine}`;
export const wireIdOf = (transition: number) => `t${transition}`;
export const transitionOfWire = (wireId: string): number | null => (/^t\d+$/.test(wireId) ? Number(wireId.slice(1)) : null);

const eventName = (model: HkxAuthoringModel, id: number) =>
  id >= 0 && id < model.events.length ? model.events[id].name || `event ${id}` : id < 0 ? 'no event' : `event ${id}?`;

export const targetState = (model: HkxAuthoringModel, machine: number, stateId: number): number =>
  model.states.findIndex(s => s.state_machine === machine && s.state_id === stateId);

export interface BehaviorBlueprint {
  doc: BpDocument;
  defs: Record<string, BpNodeDef>;
  diagnostics: BpDiagnostic[];
}

function ownerNode(machine: number, state: number): string {
  return state < 0 ? wildcardNodeId(machine) : stateNodeId(state);
}

// Constraint: the canvas is a view; every pin and wire id here maps back to one transition, so the editor never needs the reducer to edit.
export function behaviorToBlueprint(model: HkxAuthoringModel, machine: number): BehaviorBlueprint {
  const defs: Record<string, BpNodeDef> = {};
  const nodes: BpNode[] = [];
  const wires: BpWire[] = [];
  const owners: Array<{ node: string; label: string; state: number }> = model.states.flatMap((s, index) =>
    s.state_machine === machine ? [{ node: stateNodeId(index), label: `${s.name || 'state'} (${s.state_id})`, state: index }] : []);
  if (runOf(model, machine, -1).length > 0) {
    owners.unshift({ node: wildcardNodeId(machine), label: 'Wildcard (any state)', state: -1 });
  }

  for (const owner of owners) {
    const run = runOf(model, machine, owner.state);
    const pins = [
      ...(owner.state < 0 ? [] : [{ id: 'in', name: 'enter', kind: 'exec' as const, dir: 'in' as const, dataType: 'exec', optional: false }]),
      ...run.map((t, at) => ({
        id: `t${at}`, name: `${eventName(model, t.event_id)} -> ${t.to_state_id}`, kind: 'exec' as const, dir: 'out' as const,
        dataType: 'exec', optional: false,
      })),
      { id: 'new', name: '+ new transition', kind: 'exec' as const, dir: 'out' as const, dataType: 'exec', optional: true },
    ];
    defs[owner.node] = { type: owner.node, label: owner.label, category: 'Behavior', kind: 'state', isPure: false, isGlobal: false, pins };
    nodes.push({ id: owner.node, def: owner.node, kind: 'state', x: 0, y: 0 });
    run.forEach((t, at) => {
      const to = targetState(model, machine, t.to_state_id);
      if (to < 0) return;
      wires.push({ id: wireIdOf(t.index), from: { node: owner.node, pin: `t${at}` }, to: { node: stateNodeId(to), pin: 'in' } });
    });
  }

  const doc: BpDocument = {
    schema: 1, id: `behavior-${machine}`, kind: 'behaviorGraph',
    header: { scriptName: model.graph_name || 'behavior', extends: null, flags: [], imports: [], docComment: null, autoState: null },
    variables: [], structs: [], nodes, wires,
  };
  const placed = autoLayout(doc, defs);
  doc.nodes = nodes.map(n => ({ ...n, x: placed[n.id]?.x ?? 0, y: placed[n.id]?.y ?? 0 }));

  const diagnostics: BpDiagnostic[] = [];
  for (const issue of model.validation.issues) {
    let nodeId: string | null = null;
    if (issue.table === 'states' && model.states[issue.index]?.state_machine === machine) nodeId = stateNodeId(issue.index);
    if (issue.table === 'transitions') {
      const t = model.transitions[issue.index];
      if (t && t.state_machine === machine) nodeId = ownerNode(machine, t.state);
    }
    if (nodeId && defs[nodeId]) diagnostics.push({ code: issue.code, severity: issue.severity, message: issue.message, nodeId });
  }
  return { doc, defs, diagnostics };
}
