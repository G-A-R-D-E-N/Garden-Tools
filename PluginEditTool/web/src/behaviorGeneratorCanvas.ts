import type { BpDocument, BpNode, BpNodeDef, BpWire } from './Blueprint/graphModel';
import { autoLayout } from './Blueprint/layout';
import { BLEND_CHILD_OWNER, BLENDER_OWNER, type HkxAuthoringModel } from './behaviorAuthoringClient';

const MACHINE_KIND = 1;
const CLIP_KIND = 3;
const SHOWN_LIMIT = 300;
const roleNames = ['', 'wrapped', 'offset clip', 'selected', 'layer', 'bone switch', 'blend child'];

export const generatorNodeId = (node: number) => `g${node}`;
export const generatorOfNodeId = (id: string): number | null => (/^g\d+$/.test(id) ? Number(id.slice(1)) : null);

export interface GeneratorBlueprint {
  doc: BpDocument;
  defs: Record<string, BpNodeDef>;
  shown: number;
  truncated: boolean;
}

interface Child {
  node: number;
  label: string;
}

const BLENDER_KIND = 2;

const variableOf = (model: HkxAuthoringModel, owner: number, index: number, member: string): string | null => {
  const b = model.bindings.find(x => x.owner_kind === owner && x.owner_index === index && x.binding_type === 0 && x.member_path.endsWith(member));
  return b ? model.variables[b.variable_index]?.name ?? `variable ${b.variable_index}` : null;
};

function childrenOf(model: HkxAuthoringModel, node: number): Child[] {
  const out: Child[] = [];
  const blender = model.nodes[node]?.kind === BLENDER_KIND ? model.blenders.find(b => b.node === node) : undefined;
  if (blender) {
    for (let at = 0; at < blender.child_count; at++) {
      const row = blender.first_child + at;
      const child = model.blend_children[row];
      if (!child || child.generator_node < 0) continue;
      const driver = variableOf(model, BLEND_CHILD_OWNER, row, 'weight');
      out.push({ node: child.generator_node, label: `blend child ${Number(child.weight.toFixed(3))}${driver ? ` (${driver})` : ''}` });
    }
  }
  if (model.nodes[node]?.kind === MACHINE_KIND) {
    const machine = model.state_machines.findIndex(m => m.node === node);
    for (const s of model.states) {
      if (s.state_machine === machine && s.generator_node >= 0) out.push({ node: s.generator_node, label: `state ${s.state_id}` });
    }
  }
  for (const e of model.edges) {
    if (e.parent !== node || e.child < 0) continue;
    const role = roleNames[e.role] || `role ${e.role}`;
    out.push({ node: e.child, label: e.role === 4 || e.role === 6 ? `${role} ${Number(e.weight.toFixed(3))}` : role });
  }
  return out;
}

const titleOf = (model: HkxAuthoringModel, node: number): string => {
  const n = model.nodes[node];
  if (!n) return `node ${node}?`;
  const clip = n.kind === CLIP_KIND ? model.clips.find(c => c.node === node) : undefined;
  const blender = n.kind === BLENDER_KIND ? model.blenders.findIndex(b => b.node === node) : -1;
  const parameter = blender >= 0 ? variableOf(model, BLENDER_OWNER, blender, 'blendParameter') : null;
  return `${n.name || n.class_name || `node ${node}`}${clip ? `: ${clip.animation}` : ''}${parameter ? ` (blend by ${parameter})` : ''}`;
};

// Constraint: read-only view of the generators below one node; it draws the model tables and is never an edit surface.
export function generatorToBlueprint(model: HkxAuthoringModel, root: number): GeneratorBlueprint {
  const defs: Record<string, BpNodeDef> = {};
  const nodes: BpNode[] = [];
  const wires: BpWire[] = [];
  const seen = new Set<number>();
  const queue = [root];
  let truncated = false;
  while (queue.length > 0) {
    const node = queue.shift() as number;
    if (seen.has(node) || node < 0 || node >= model.nodes.length) continue;
    if (seen.size >= SHOWN_LIMIT) { truncated = true; break; }
    seen.add(node);
    const kids = childrenOf(model, node);
    const id = generatorNodeId(node);
    defs[id] = {
      type: id, label: titleOf(model, node), category: 'Behavior', kind: 'generator', isPure: false, isGlobal: false,
      pins: [
        { id: 'in', name: model.nodes[node].class_name, kind: 'exec', dir: 'in', dataType: 'exec', optional: false },
        ...kids.map((k, at) => ({ id: `c${at}`, name: k.label, kind: 'exec' as const, dir: 'out' as const, dataType: 'exec', optional: false })),
      ],
    };
    nodes.push({ id, def: id, kind: 'generator', x: 0, y: 0 });
    kids.forEach((k, at) => {
      if (k.node < 0 || k.node >= model.nodes.length) return;
      wires.push({ id: `e${node}_${at}`, from: { node: id, pin: `c${at}` }, to: { node: generatorNodeId(k.node), pin: 'in' } });
      queue.push(k.node);
    });
  }
  const shown = new Set(nodes.map(n => n.id));
  const doc: BpDocument = {
    schema: 1, id: `generators-${root}`, kind: 'behaviorGraph',
    header: { scriptName: model.graph_name || 'behavior', extends: null, flags: [], imports: [], docComment: null, autoState: null },
    variables: [], structs: [], nodes, wires: wires.filter(w => shown.has(w.to.node)),
  };
  const placed = autoLayout(doc, defs);
  doc.nodes = nodes.map(n => ({ ...n, x: placed[n.id]?.x ?? 0, y: placed[n.id]?.y ?? 0 }));
  return { doc, defs, shown: nodes.length, truncated };
}
