import type { HkxHost } from './backend';
import './hkxHost';

export interface HkxObjectField {
  name: string;
  offset: number;
  size: number;
  hex: string;
}

export interface HkxObjectReference {
  source: number;
  member: string;
  target_section: number;
  target_offset: number;
  target_object: number;
  target_class: string;
}

export interface HkxObjectDetail {
  index: number;
  offset: number;
  signature: number;
  class_name: string;
  layout_size: number;
  size: number;
  layout_known: boolean;
  fields: HkxObjectField[];
  references: HkxObjectReference[];
}

export interface HkxObjectView {
  object_count: number;
  matched: number;
  classes: string[];
  objects: HkxObjectDetail[];
  diagnostics: string[];
}

const parsed = <T extends { error?: string }>(raw: string | object): T => {
  const value = (typeof raw === 'string' ? JSON.parse(raw) : raw) as T;
  if (value.error) throw new Error(value.error);
  return value;
};

// Requires the host to carry the packfile RPC; an older host build reports why instead of showing an empty list.
export async function readObjects(host: HkxHost, path: string, classPrefix: string, knownLayoutOnly: boolean,
                                 limit: number): Promise<HkxObjectView> {
  if (!host.InspectObjects) throw new Error('this host build cannot inspect HKX objects');
  const view = parsed<HkxObjectView & { error?: string }>(
    await host.InspectObjects(path, classPrefix, knownLayoutOnly, limit));
  if (!Array.isArray(view.objects)) throw new Error('object view has no object list');
  return view;
}

// Constraint: the summary always states the totals, so a filter that hides every object does not read as an empty file.
export const describeObjectView = (view: HkxObjectView): string =>
  `${view.matched} of ${view.object_count} objects, ${view.classes.length} class` +
  `${view.classes.length === 1 ? '' : 'es'}` +
  (view.diagnostics.length > 0 ? `, ${view.diagnostics.length} without a generated layout` : '');

// Invariant: a reference reads as the member name and the class it points at, never as a bare offset.
export const describeReference = (reference: HkxObjectReference): string => {
  const from = reference.member || `0x${reference.source.toString(16)}`;
  if (reference.target_object < 0) return `${from} -> section ${reference.target_section} 0x${reference.target_offset.toString(16)}`;
  return `${from} -> object ${reference.target_object} ${reference.target_class}`;
};

export interface HkxRootMotionSample {
  frame: number;
  time: number;
  translation: number[];
  delta: number[];
}

export interface HkxRootMotion {
  bone: number;
  bone_name: string;
  track: number;
  frame_count: number;
  duration: number;
  frame_duration: number;
  total: number[];
  distance: number;
  yaw_degrees: number;
  velocity: number[];
  loops: boolean;
  loop_gap: number;
  samples: HkxRootMotionSample[];
}

// Requires the host to carry the root motion RPC; an older host build reports why instead of showing zero travel.
export async function readRootMotion(host: HkxHost, skeletonPath: string, animationPath: string, bone: number,
                                     limit: number): Promise<HkxRootMotion> {
  if (!host.RootMotion) throw new Error('this host build cannot report root motion');
  const motion = parsed<HkxRootMotion & { error?: string }>(
    await host.RootMotion(skeletonPath, animationPath, bone, limit));
  if (!Array.isArray(motion.samples) || !Array.isArray(motion.total)) throw new Error('root motion report has no samples');
  return motion;
}

// Constraint: the line always states net travel and the loop decision, so a still clip cannot read as a walk.
export const describeRootMotion = (motion: HkxRootMotion): string =>
  `${motion.bone_name}: ${motion.distance.toFixed(3)} travelled, ` +
  `${motion.total.map(value => value.toFixed(3)).join(', ')} net, ` +
  `yaw ${motion.yaw_degrees.toFixed(1)}deg, ` +
  (motion.loops ? 'loops back to the start' : `ends ${motion.loop_gap.toFixed(3)} away`);

export interface HkxBoneInfo {
  index: number;
  name: string;
  parent: number;
  depth: number;
  children: number[];
  opposite: string;
  lock?: boolean;
  translation?: number[];
  rotation?: number[];
  scale?: number[];
}

export interface HkxSkeletonDocument {
  name: string;
  bone_count: number;
  has_reference_pose: boolean;
  roots: string[];
  bones: HkxBoneInfo[];
  diagnostics: string[];
  changed?: boolean;
  source_hash?: string;
  saveable?: boolean;
  save_refusal?: string | null;
  saved?: boolean;
  path?: string;
}

// Requires the host to carry the skeleton document RPC; an older host build reports why instead of an empty rig.
export async function readSkeletonDocument(host: HkxHost, skeletonPath: string): Promise<HkxSkeletonDocument> {
  if (!host.SkeletonDocument) throw new Error('this host build cannot read a skeleton document');
  const document = parsed<HkxSkeletonDocument & { error?: string }>(await host.SkeletonDocument(skeletonPath));
  if (!Array.isArray(document.bones)) throw new Error('skeleton document has no bone list');
  return document;
}

export type HkxSkeletonEditKind = 'rename' | 'reparent' | 'remove' | 'set_pose' | 'set_lock';

// Constraint: a refused edit is thrown with the engine's own wording, so a panel never reports a silent no-op.
export async function editSkeleton(host: HkxHost, skeletonPath: string, kind: HkxSkeletonEditKind, bone: number,
                                  name: string, parent: number): Promise<HkxSkeletonDocument> {
  if (!host.SkeletonEdit) throw new Error('this host build cannot edit a skeleton');
  const document = parsed<HkxSkeletonDocument & { error?: string }>(
    await host.SkeletonEdit(skeletonPath, kind, bone, name, parent));
  if (!Array.isArray(document.bones)) throw new Error('edited skeleton has no bone list');
  return document;
}

export interface HkxPendingEdit {
  kind: HkxSkeletonEditKind;
  bone: number;
  name?: string;
  parent?: number;
  translation?: number[];
  rotation?: number[];
  scale?: number[];
  locked?: boolean;
}

export const describePendingEdit = (edit: HkxPendingEdit): string =>
  edit.kind === 'rename' ? `rename bone ${edit.bone} to ${edit.name ?? ''}`
    : edit.kind === 'reparent' ? `reparent bone ${edit.bone} under ${(edit.parent ?? -1) < 0 ? 'the root' : `bone ${edit.parent}`}`
      : edit.kind === 'set_pose' ? `set reference pose of bone ${edit.bone} to translation ${(edit.translation ?? []).join(', ')}, rotation ${(edit.rotation ?? []).join(', ')}, scale ${(edit.scale ?? []).join(', ')}`
        : edit.kind === 'set_lock' ? `${edit.locked ? 'lock' : 'unlock'} translation of bone ${edit.bone}`
          : `remove bone ${edit.bone}`;

// Requires exactly `size` finite numbers: a typo is thrown in the panel instead of sent to the host as a different pose.
export const parseVector = (text: string, size: number, label: string): number[] => {
  const values = text.split(/[\s,]+/).filter(part => part.length > 0).map(Number);
  if (values.length !== size || !values.every(Number.isFinite)) throw new Error(`${label} needs ${size} numbers`);
  return values;
};

// Constraint: this replays the whole pending list on a fresh read and never writes; saveable says whether Save would succeed.
export async function previewSkeletonEdits(host: HkxHost, skeletonPath: string,
                                          edits: HkxPendingEdit[]): Promise<HkxSkeletonDocument> {
  if (!host.SkeletonPreview) throw new Error('this host build cannot preview skeleton edits');
  const document = parsed<HkxSkeletonDocument & { error?: string }>(
    await host.SkeletonPreview(skeletonPath, JSON.stringify(edits)));
  if (!Array.isArray(document.bones)) throw new Error('edited skeleton has no bone list');
  return document;
}

// Invariant: saved is true only when the host wrote the file and read it back; a refusal is thrown with the host's wording.
export async function saveSkeleton(host: HkxHost, skeletonPath: string, edits: HkxPendingEdit[],
                                  outputPath: string, expectedHash: string): Promise<HkxSkeletonDocument> {
  if (!host.SkeletonSave) throw new Error('this host build cannot save a skeleton');
  const document = parsed<HkxSkeletonDocument & { error?: string }>(
    await host.SkeletonSave(skeletonPath, JSON.stringify(edits), outputPath, expectedHash));
  if (document.saved === false) return document;
  if (document.saved !== true || !Array.isArray(document.bones)) throw new Error('the host did not confirm the save');
  return document;
}

// Invariant: a bone line states its depth and its left/right partner, so a flat list cannot hide a broken chain.
export const describeBone = (bone: HkxBoneInfo): string =>
  `${'  '.repeat(bone.depth)}${bone.index}: ${bone.name}` +
  (bone.parent >= 0 ? ` (parent ${bone.parent})` : ' (root)') +
  (bone.opposite ? ` pairs with ${bone.opposite}` : '') +
  (bone.lock ? ' translation locked' : '') +
  (bone.translation ? ` at ${bone.translation.map(value => fixed(value)).join(', ')}` : '');

export interface HkxBehaviorNode {
  object: number;
  class_name: string;
  offset: number;
  size: number;
  layout_known: boolean;
  references_out: number;
  references_in: number;
}

export interface HkxBehaviorLink {
  from: number;
  to: number;
  member: string;
}

export interface HkxBehaviorGraph {
  object_count: number;
  matched: number;
  classes: string[];
  nodes: HkxBehaviorNode[];
  links: HkxBehaviorLink[];
  diagnostics: string[];
}

// Requires the host to carry the behavior RPC; an older host build reports why instead of an empty graph.
export async function readBehavior(host: HkxHost, path: string, limit: number): Promise<HkxBehaviorGraph> {
  if (!host.InspectBehavior) throw new Error('this host build cannot read a behavior graph');
  const graph = parsed<HkxBehaviorGraph & { error?: string }>(await host.InspectBehavior(path, limit));
  if (!Array.isArray(graph.nodes) || !Array.isArray(graph.links)) throw new Error('behavior graph has no nodes');
  return graph;
}

// Constraint: the summary always states node, link and class counts, so a file without behavior objects is never silent.
export const describeBehavior = (graph: HkxBehaviorGraph): string =>
  `${graph.matched} of ${graph.object_count} objects are behavior nodes, ${graph.links.length} links, ` +
  `${graph.classes.length} classes` + (graph.diagnostics.length > 0 ? `, ${graph.diagnostics.length} notes` : '');

export interface HkxModelEvent {
  name: string;
  flags: number;
}

export interface HkxModelVariable {
  name: string;
  type: number;
  role: number;
  word: number;
  int_value: number;
  real_value: number | null;
  vector?: Array<number | null>;
}

export interface HkxModelNode {
  object: number;
  class_name: string;
  name: string;
  kind: string;
  kind_index: number;
}

export interface HkxModelEdge {
  parent: number;
  child: number;
  role: string;
  slot: number;
  weight: number | null;
  has_bone_weights: boolean;
}

export interface HkxModelState {
  state_machine: number;
  name: string;
  state_id: number;
  generator_node: number;
}

export interface HkxBehaviorModel {
  graph_name: string;
  root_node: number;
  events: HkxModelEvent[];
  variables: HkxModelVariable[];
  nodes: HkxModelNode[];
  edges: HkxModelEdge[];
  states: HkxModelState[];
  event_total: number;
  variable_total: number;
  node_total: number;
  edge_total: number;
  state_total: number;
  diagnostics: string[];
}

// Requires the host to carry the typed behavior RPC.
export async function readBehaviorModel(host: HkxHost, path: string, limit: number): Promise<HkxBehaviorModel> {
  if (!host.ReadBehaviorModel) throw new Error('this host build cannot read the typed behavior graph');
  const model = parsed<HkxBehaviorModel & { error?: string }>(await host.ReadBehaviorModel(path, limit));
  if (!Array.isArray(model.nodes) || !Array.isArray(model.edges) || !Array.isArray(model.states)) {
    throw new Error('typed behavior graph has no nodes');
  }
  return model;
}

// Invariant: the summary states shown and total counts.
export const describeBehaviorModel = (model: HkxBehaviorModel): string =>
  `${model.graph_name || '(unnamed graph)'}: ${model.nodes.length} of ${model.node_total} nodes, ` +
  `${model.edges.length} of ${model.edge_total} child edges, ${model.event_total} events, ${model.variable_total} variables` +
  (model.diagnostics.length > 0 ? `, ${model.diagnostics.length} notes` : '');

// Invariant: a node line lists its edge children, then a state machine's state generators.
export const describeModelNode = (model: HkxBehaviorModel, node: HkxModelNode, index: number): string => {
  const label = (at: number) => model.nodes[at] ? `${at} ${model.nodes[at].name || model.nodes[at].class_name}` : `${at} (not shown)`;
  const edges = model.edges.filter(edge => edge.parent === index && edge.child >= 0)
    .map(edge => `${edge.role} ${label(edge.child)}`);
  const states = node.kind === 'state_machine'
    ? model.states.filter(state => state.state_machine === node.kind_index && state.generator_node >= 0)
        .map(state => `state ${state.name} ${label(state.generator_node)}`)
    : [];
  const children = [...edges, ...states];
  return `${index}: ${node.name || '(unnamed)'} [${node.class_name}, ${node.kind}]` +
    (index === model.root_node ? ' (root)' : '') + (children.length > 0 ? ` -> ${children.join('; ')}` : '');
};

export interface HkxRagdollFinding {
  severity: string;
  message: string;
  hint: string;
}

export interface HkxRagdollBody {
  id: number;
  name: string;
  shape_id: number;
  bone_index: number;
  mass: number | null;
  center_of_mass: number[];
  position: number[];
  rotation: number[];
}

export interface HkxRagdollShape {
  id: number;
  kind: string;
  radius: number;
  half_extents: number[];
  children: number[];
  vertex_count: number;
  face_count: number;
}

export interface HkxRagdollConstraint {
  id: number;
  kind: string;
  body_a: number;
  body_b: number;
  min_angle: number | null;
  max_angle: number | null;
  twist_min_angle: number | null;
  twist_max_angle: number | null;
  cone_max_angle: number | null;
  has_preview_limits: boolean;
}

export interface HkxRagdoll {
  loaded: boolean;
  status: string;
  status_label: string;
  headline: string;
  hint: string;
  findings: HkxRagdollFinding[];
  body_count: number;
  usable_bodies: number;
  shape_count: number;
  constraint_count: number;
  mappings: number;
  shapes?: HkxRagdollShape[];
  bodies?: HkxRagdollBody[];
  constraints?: HkxRagdollConstraint[];
  body_total?: number;
  constraint_total?: number;
  skeleton?: { name: string; bone_count: number } | null;
  animation_skeleton?: { name: string; bone_count: number } | null;
  validation?: { level: string; where: string; message: string }[];
}

// Requires the host to carry the ragdoll RPC; a file with no usable ragdoll still replies, with loaded false and the verdict.
export async function readRagdoll(host: HkxHost, path: string, limit: number): Promise<HkxRagdoll> {
  if (!host.ReadRagdoll) throw new Error('this host build cannot read a ragdoll');
  const ragdoll = parsed<HkxRagdoll & { error?: string }>(await host.ReadRagdoll(path, limit));
  if (typeof ragdoll.loaded !== 'boolean') throw new Error('ragdoll reply has no load verdict');
  return ragdoll;
}

// Invariant: an unloaded file shows the classifier headline and hint, so a skeleton-only file is never an empty ragdoll.
export const describeRagdoll = (ragdoll: HkxRagdoll): string =>
  ragdoll.loaded
    ? `${ragdoll.bodies?.length ?? 0} of ${ragdoll.body_total ?? 0} bodies, ` +
      `${ragdoll.constraints?.length ?? 0} of ${ragdoll.constraint_total ?? 0} constraints, ` +
      `${ragdoll.shape_count} shapes, ${ragdoll.mappings} mapper rows`
    : `${ragdoll.status_label}: ${ragdoll.headline}${ragdoll.hint ? ` ${ragdoll.hint}` : ''}`;

export const fixed = (value: number | null) => (value === null ? '-' : value.toFixed(3));

export const describeConstraint = (constraint: HkxRagdollConstraint): string =>
  `${constraint.id}: ${constraint.kind} bodies ${constraint.body_a} and ${constraint.body_b}` +
  (constraint.kind === 'ragdoll'
    ? `, twist ${fixed(constraint.twist_min_angle)} to ${fixed(constraint.twist_max_angle)}, cone ${fixed(constraint.cone_max_angle)}`
    : `, angle ${fixed(constraint.min_angle)} to ${fixed(constraint.max_angle)}`);

export interface HkxMapperPose {
  translation: Array<number | null>;
  rotation: Array<number | null>;
  scale: Array<number | null>;
}

export interface HkxMapperRow {
  bone_a: number;
  bone_b: number;
  bone_a_name: string;
  bone_b_name: string;
  transform: HkxMapperPose;
  recomputed?: HkxMapperPose;
  translation_delta?: number | null;
  rotation_delta?: number | null;
  recompute_error?: string;
}

export interface HkxMapperChain {
  start_bone_a: number;
  end_bone_a: number;
  start_bone_b: number;
  end_bone_b: number;
}

export interface HkxSkeletonMapperView {
  object_index: number;
  skeleton_a: { name: string; bone_count: number };
  skeleton_b: { name: string; bone_count: number };
  mapping_type: number;
  keep_unmapped_local: boolean;
  unmapped_bones: number[];
  rows: HkxMapperRow[];
  chains: HkxMapperChain[];
  row_total: number;
  chain_total: number;
}

export interface HkxMapperFile {
  mapper_count: number;
  recompute: boolean;
  source_sha256?: string;
  mappers: HkxSkeletonMapperView[];
}

// Requires the host to carry the mapper RPC; a file with no mapper is an error from the host, never an empty list.
export async function readMappers(host: HkxHost, path: string, recompute: boolean, limit: number): Promise<HkxMapperFile> {
  if (!host.ReadMappers) throw new Error('this host build cannot read skeleton mappers');
  const file = parsed<HkxMapperFile & { error?: string }>(await host.ReadMappers(path, recompute, limit));
  if (!Array.isArray(file.mappers)) throw new Error('mapper reply has no mapper list');
  return file;
}

// Invariant: the line states the mapper count and the total rows, so a truncated row list is never read as the whole mapper.
export const describeMappers = (file: HkxMapperFile): string =>
  `${file.mapper_count} mapper${file.mapper_count === 1 ? '' : 's'}: ` +
  file.mappers.map(mapper => `${mapper.skeleton_a.name} to ${mapper.skeleton_b.name}, ` +
    `${mapper.rows.length} of ${mapper.row_total} rows, ${mapper.chains.length} of ${mapper.chain_total} chains`).join('; ');

// Invariant: a recomputed row states its gap in units and radians, and a refused recompute states the reason.
export const describeMapperRow = (row: HkxMapperRow): string =>
  `${row.bone_a} ${row.bone_a_name || '(no name)'} from ${row.bone_b} ${row.bone_b_name || '(no name)'}` +
  `: ${row.transform.translation.map(fixed).join(', ')}` +
  (row.recompute_error ? ` (cannot recompute: ${row.recompute_error})`
    : row.translation_delta !== undefined && row.rotation_delta !== undefined
      ? ` (recomputed gap ${row.translation_delta === null ? '-' : row.translation_delta.toFixed(4)} units, ${row.rotation_delta === null ? '-' : row.rotation_delta.toFixed(4)} rad)` : '');

export interface HkxProjectFileEntry {
  name: string;
  object_count: number;
  behavior_objects: number;
  animation_objects: number;
  skeleton_objects: number;
  behavior_hash: string;
  classes: string[];
}

export interface HkxProjectIndex {
  root: string;
  query: string;
  file_count: number;
  read_count: number;
  matches: string[];
  files: HkxProjectFileEntry[];
  unreadable: { name: string; error: string }[];
  duplicates: string[][];
  diagnostics: string[];
}

// Requires the host to carry the project RPC; an older host build reports why instead of an empty project.
export async function readProjectIndex(host: HkxHost, root: string, query: string,
                                       limit: number): Promise<HkxProjectIndex> {
  if (!host.ProjectIndex) throw new Error('this host build cannot index a project');
  const index = parsed<HkxProjectIndex & { error?: string }>(await host.ProjectIndex(root, query, limit));
  if (!Array.isArray(index.files)) throw new Error('project index has no file list');
  return index;
}

// Constraint: the line states scanned, read and duplicate counts, so a folder of broken files cannot read as empty.
export const describeProject = (index: HkxProjectIndex): string =>
  `${index.read_count} of ${index.file_count} files read, ${index.matches.length} match, ` +
  `${index.duplicates.length} duplicate graph groups` +
  (index.unreadable.length > 0 ? `, ${index.unreadable.length} unreadable` : '');
