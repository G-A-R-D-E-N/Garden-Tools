import { describe, expect, it } from 'vitest';
import {
  isTriggerLikeBase, primitiveBoxSize, primitiveRole, primitiveTypeName, primitiveVolume,
} from './cellPrimitive';

describe('primitiveBoxSize', () => {
  it('doubles the stored half-extents into the full box size', () => {
    expect(primitiveBoxSize({ x: 512, y: 288, z: 192 })).toEqual([1024, 576, 384]);
  });

  it('returns null when there are no bounds', () => {
    expect(primitiveBoxSize(null)).toBeNull();
    expect(primitiveBoxSize(undefined)).toBeNull();
  });
});

describe('primitiveTypeName', () => {
  it('names the five XPRM shape types', () => {
    expect([1, 2, 3, 4, 5].map(primitiveTypeName)).toEqual(['Box', 'Sphere', 'Plane', 'Line', 'Ellipsoid']);
  });

  it('reports an unknown type with its number', () => {
    expect(primitiveTypeName(9)).toBe('Unknown (9)');
  });
});

describe('primitiveRole', () => {
  it('marks room and portal markers as rooms', () => {
    expect(primitiveRole('RoomMarker')).toBe('room');
    expect(primitiveRole('PortalMarker')).toBe('room');
  });

  it('marks any trigger name as a trigger, whatever its case', () => {
    expect(primitiveRole('TriggerBox')).toBe('trigger');
    expect(primitiveRole('DialogueTrigger01')).toBe('trigger');
    expect(primitiveRole('triggerVolume')).toBe('trigger');
  });

  it('leaves other bases as other', () => {
    expect(primitiveRole('LightBox')).toBe('other');
    expect(primitiveRole('CollisionMarker')).toBe('other');
    expect(primitiveRole('')).toBe('other');
  });
});

describe('isTriggerLikeBase', () => {
  it('is true for room and trigger bases only', () => {
    expect(isTriggerLikeBase('RoomMarker')).toBe(true);
    expect(isTriggerLikeBase('TriggerBox')).toBe(true);
    expect(isTriggerLikeBase('LightBox')).toBe(false);
    expect(isTriggerLikeBase('PackageLocationPrimitive')).toBe(false);
  });
});

describe('primitiveVolume', () => {
  // Constraint, detects: a primitive type other than box being sized as a box, or a plane with bounds z of 0 being rejected.
  it('sizes each primitive type from its half-extents', () => {
    const bounds = { x: 5, y: 3, z: 2 };
    expect(primitiveVolume(bounds, 1)).toEqual({ shape: 'box', size: [10, 6, 4] });
    expect(primitiveVolume(bounds, 2)).toEqual({ shape: 'sphere', size: [10, 10, 10] });
    expect(primitiveVolume(bounds, 5)).toEqual({ shape: 'ellipsoid', size: [10, 6, 4] });
    expect(primitiveVolume({ x: 5, y: 3, z: 0 }, 3)).toEqual({ shape: 'plane', size: [10, 6, 1] });
    expect(primitiveVolume({ x: 5, y: 0, z: 2 }, 4)).toEqual({ shape: 'line', size: [10, 0, 4] });
    expect(primitiveVolume(bounds, 9)).toEqual({ shape: 'box', size: [10, 6, 4] });
  });

  it('returns null when the bounds cannot describe the shape', () => {
    expect(primitiveVolume(null, 1)).toBeNull();
    expect(primitiveVolume({ x: 0, y: 3, z: 2 }, 1)).toBeNull();
    expect(primitiveVolume({ x: 0, y: 0, z: 0 }, 2)).toBeNull();
    expect(primitiveVolume({ x: 5, y: 0, z: 2 }, 3)).toBeNull();
    expect(primitiveVolume({ x: 0, y: 0, z: 0 }, 4)).toBeNull();
  });
});
