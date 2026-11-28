import type { PreviewReply, RagdollView } from './hkxRagdollClient';

export type Vec3 = [number, number, number];

export interface RagdollOverlays {
  skeleton: boolean;
  mesh: boolean;
  ragdoll: boolean;
}

export interface RagdollSelection {
  body: number | null;
  constraint: number | null;
}

export interface RagdollScene {
  overlays: RagdollOverlays;
  selection: RagdollSelection;
  bodies: { id: number; position: Vec3 }[];
  links: { id: number; a: number; b: number }[];
  frameKey: string;
}

export interface RagdollGeometry {
  normal: Vec3[];
  selected: Vec3[];
}

const finite3 = (value: unknown): value is Vec3 =>
  Array.isArray(value) && value.length >= 3 && value.slice(0, 3).every(n => typeof n === 'number' && Number.isFinite(n));

// Constraint: a body without a finite stored position is left out, so a malformed row never reaches the viewport as NaN.
export function buildRagdollScene(view: RagdollView | null, preview: PreviewReply | null, overlays: RagdollOverlays,
                                  selection: RagdollSelection, path: string): RagdollScene {
  const moved = new Map<number, Vec3>();
  const last = preview?.frames[preview.frames.length - 1];
  for (const state of last?.bodies ?? []) if (finite3(state.position)) moved.set(state.body, [state.position[0], state.position[1], state.position[2]]);
  const bodies: RagdollScene['bodies'] = [];
  for (const body of view?.bodies ?? []) {
    const position = moved.get(body.id) ?? (finite3(body.position) ? [body.position[0], body.position[1], body.position[2]] as Vec3 : null);
    if (position) bodies.push({ id: body.id, position });
  }
  return {
    overlays, selection, bodies,
    links: (view?.constraints ?? []).map(c => ({ id: c.id, a: c.body_a, b: c.body_b })),
    frameKey: `ragdoll|${path}`,
  };
}

const cross = (at: Vec3, size: number): Vec3[] => [
  [at[0] - size, at[1], at[2]], [at[0] + size, at[1], at[2]],
  [at[0], at[1] - size, at[2]], [at[0], at[1] + size, at[2]],
  [at[0], at[1], at[2] - size], [at[0], at[1], at[2] + size],
];

// Constraint: line points come in pairs; the selected body and constraint go to their own list so the viewport can colour them.
export function ragdollGeometry(scene: RagdollScene): RagdollGeometry {
  const out: RagdollGeometry = { normal: [], selected: [] };
  if (!scene.overlays.ragdoll || scene.bodies.length === 0) return out;
  const lows = [Infinity, Infinity, Infinity];
  const highs = [-Infinity, -Infinity, -Infinity];
  for (const body of scene.bodies) {
    for (let axis = 0; axis < 3; axis += 1) {
      lows[axis] = Math.min(lows[axis], body.position[axis]);
      highs[axis] = Math.max(highs[axis], body.position[axis]);
    }
  }
  const size = Math.max(highs[0] - lows[0], highs[1] - lows[1], highs[2] - lows[2], 1e-3) * 0.03;
  const at = new Map(scene.bodies.map(body => [body.id, body.position] as const));
  for (const body of scene.bodies) {
    (scene.selection.body === body.id ? out.selected : out.normal).push(...cross(body.position, size));
  }
  for (const link of scene.links) {
    const a = at.get(link.a);
    const b = at.get(link.b);
    if (a && b) (scene.selection.constraint === link.id ? out.selected : out.normal).push(a, b);
  }
  return out;
}
