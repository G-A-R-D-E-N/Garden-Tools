import type { CellOwnershipChange, CellPrimitiveChange } from '../backend';

export interface OwnershipState {
  owner: string | null;
  noCrime: boolean;
  rank: number | null;
}

export function parseFiniteNumber(text: string): number | null {
  const trimmed = text.trim();
  if (trimmed === '') return null;
  const value = Number(trimmed);
  return Number.isFinite(value) ? value : null;
}

export function parseFactionRank(text: string): number | null | undefined {
  const trimmed = text.trim();
  if (trimmed === '') return null;
  if (!/^-?\d+$/.test(trimmed)) return undefined;
  const value = Number(trimmed);
  return value >= -2147483648 && value <= 2147483647 ? value : undefined;
}

export function ownershipChange(current: OwnershipState, draft: OwnershipState): CellOwnershipChange | null | undefined {
  if (draft.owner === null) return current.owner === null ? undefined : null;
  const same = current.owner === draft.owner && current.noCrime === draft.noCrime && current.rank === draft.rank;
  if (same) return undefined;
  return { owner: draft.owner, noCrime: draft.noCrime, rank: draft.rank };
}

export function primitiveChange(
  current: { bounds: number[]; color: number[] },
  draft: { bounds: number[]; color: number[] },
): CellPrimitiveChange | undefined {
  const boundsChanged = current.bounds.some((value, index) => value !== draft.bounds[index]);
  const colorChanged = current.color.some((value, index) => value !== draft.color[index]);
  if (!boundsChanged && !colorChanged) return undefined;
  const change: CellPrimitiveChange = {};
  if (boundsChanged) change.bounds = { x: draft.bounds[0], y: draft.bounds[1], z: draft.bounds[2] };
  if (colorChanged) change.color = [...draft.color];
  return change;
}
