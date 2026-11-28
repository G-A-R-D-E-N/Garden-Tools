import type { CellPlacedReference } from './backend';

export type PrimitiveBounds = { x: number; y: number; z: number };

const PRIMITIVE_TYPE_NAMES: Record<number, string> = {
  1: 'Box',
  2: 'Sphere',
  3: 'Plane',
  4: 'Line',
  5: 'Ellipsoid',
};

const ROOM_BASE_IDS = new Set(['RoomMarker', 'PortalMarker']);

export type PrimitiveShape = 'box' | 'sphere' | 'plane' | 'line' | 'ellipsoid';

export type PrimitiveVolume = { shape: PrimitiveShape; size: [number, number, number] };

const PRIMITIVE_SHAPES: Record<number, PrimitiveShape> = { 2: 'sphere', 3: 'plane', 4: 'line', 5: 'ellipsoid' };

// Invariant: mirrors native PreviewVolumes.cpp primitiveSize; a plane is flat in local Z and null means the bounds cannot describe the shape.
export function primitiveVolume(bounds: PrimitiveBounds | null | undefined, type: number): PrimitiveVolume | null {
  if (!bounds) return null;
  const { x, y, z } = bounds;
  const shape = PRIMITIVE_SHAPES[type] ?? 'box';
  if (shape === 'sphere') {
    const diameter = Math.max(x, y, z) * 2;
    return diameter > 0 ? { shape, size: [diameter, diameter, diameter] } : null;
  }
  if (shape === 'plane') return x > 0 && y > 0 ? { shape, size: [x * 2, y * 2, 1] } : null;
  if (shape === 'line') {
    const size: [number, number, number] = [Math.max(x, 0) * 2, Math.max(y, 0) * 2, Math.max(z, 0) * 2];
    return size.some(v => v > 0) ? { shape, size } : null;
  }
  return x > 0 && y > 0 && z > 0 ? { shape, size: [x * 2, y * 2, z * 2] } : null;
}

export function primitiveBoxSize(bounds: PrimitiveBounds | null | undefined): [number, number, number] | null {
  if (!bounds) return null;
  return [bounds.x * 2, bounds.y * 2, bounds.z * 2];
}

export function primitiveTypeName(type: number): string {
  return PRIMITIVE_TYPE_NAMES[type] ?? `Unknown (${type})`;
}

export function primitiveRole(editorId: string): 'room' | 'trigger' | 'other' {
  if (ROOM_BASE_IDS.has(editorId)) return 'room';
  if (/trigger/i.test(editorId)) return 'trigger';
  return 'other';
}

export function isTriggerLikeBase(editorId: string): boolean {
  return primitiveRole(editorId) !== 'other';
}

export function primitiveRoleOf(ref: Pick<CellPlacedReference, 'primitive' | 'baseEditorId'>): 'room' | 'trigger' | null {
  if (!ref.primitive) return null;
  return ROOM_BASE_IDS.has(ref.baseEditorId ?? '') ? 'room' : 'trigger';
}
