import type { NifConnectBlock, NifConnectChildrenBlock, NifConnectPoint } from './backend';

export const PARENTS = 'BSConnectPoint::Parents';
export const CHILDREN = 'BSConnectPoint::Children';

function samePoint(a: NifConnectPoint, b: NifConnectPoint): boolean {
  return a.parent === b.parent && a.name === b.name && a.scale === b.scale
    && a.rotation.every((v, k) => v === b.rotation[k]) && a.translation.every((v, k) => v === b.translation[k]);
}

function parentOps(block: number, before: NifConnectPoint[], after: NifConnectPoint[]): Record<string, unknown>[] {
  const ops: Record<string, unknown>[] = [];
  const shared = Math.min(before.length, after.length);
  for (let i = 0; i < shared; i++) {
    const a = before[i];
    const b = after[i];
    if (samePoint(a, b)) continue;
    const set = (field: string, value: unknown) => ops.push({ op: 'set', block, index: i, field, value });
    if (a.parent !== b.parent) set('parent', b.parent);
    if (a.name !== b.name) set('name', b.name);
    if (!a.rotation.every((v, k) => v === b.rotation[k])) set('rotation', b.rotation);
    if (!a.translation.every((v, k) => v === b.translation[k])) set('translation', b.translation);
    if (a.scale !== b.scale) set('scale', b.scale);
  }
  for (let i = before.length - 1; i >= after.length; i--) ops.push({ op: 'remove', block, index: i });
  for (let i = before.length; i < after.length; i++) ops.push({ op: 'add', block, value: after[i] });
  return ops;
}

function childOps(block: number, before: NifConnectChildrenBlock, after: NifConnectChildrenBlock): Record<string, unknown>[] {
  const ops: Record<string, unknown>[] = [];
  if (before.skinned !== after.skinned) ops.push({ op: 'set', block, field: 'skinned', value: after.skinned });
  const shared = Math.min(before.names.length, after.names.length);
  for (let i = 0; i < shared; i++) {
    if (before.names[i] !== after.names[i]) ops.push({ op: 'set', block, index: i, field: 'name', value: after.names[i] });
  }
  for (let i = before.names.length - 1; i >= after.names.length; i--) ops.push({ op: 'remove', block, index: i });
  for (let i = before.names.length; i < after.names.length; i++) ops.push({ op: 'add', block, value: after.names[i] });
  return ops;
}

export function connectOps(original: NifConnectBlock, draft: NifConnectBlock): Record<string, unknown>[] {
  if (original.type === PARENTS && draft.type === PARENTS) return parentOps(original.block, original.points, draft.points);
  if (original.type === CHILDREN && draft.type === CHILDREN) return childOps(original.block, original, draft);
  return [];
}
