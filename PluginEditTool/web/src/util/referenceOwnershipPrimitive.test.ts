import { describe, expect, it } from 'vitest';
import { ownershipChange, parseFactionRank, parseFiniteNumber, primitiveChange } from './referenceOwnershipPrimitive';

describe('referenceOwnershipPrimitive', () => {
  it('parses finite numbers and whole faction ranks only', () => {
    expect(parseFiniteNumber(' 2.5 ')).toBe(2.5);
    expect(parseFiniteNumber('')).toBeNull();
    expect(parseFiniteNumber('abc')).toBeNull();
    expect(parseFactionRank('')).toBeNull();
    expect(parseFactionRank('-3')).toBe(-3);
    expect(parseFactionRank('1.5')).toBeUndefined();
    expect(parseFactionRank('9999999999')).toBeUndefined();
  });

  it('returns no change, a clear, or the full owner change', () => {
    const none = { owner: null, noCrime: false, rank: null };
    expect(ownershipChange(none, none)).toBeUndefined();
    expect(ownershipChange({ owner: '000840:Base.esm', noCrime: false, rank: 2 }, { owner: null, noCrime: false, rank: null })).toBeNull();
    const owned = { owner: '000840:Base.esm', noCrime: true, rank: 2 };
    expect(ownershipChange({ owner: '000840:Base.esm', noCrime: false, rank: 2 }, owned)).toEqual({
      owner: '000840:Base.esm', noCrime: true, rank: 2,
    });
    expect(ownershipChange(owned, owned)).toBeUndefined();
  });

  it('sends only the primitive parts that changed', () => {
    const current = { bounds: [1, 2, 3], color: [0, 0.5, 1, 0.4] };
    expect(primitiveChange(current, { bounds: [1, 2, 3], color: [0, 0.5, 1, 0.4] })).toBeUndefined();
    expect(primitiveChange(current, { bounds: [4, 2, 3], color: [0, 0.5, 1, 0.4] })).toEqual({ bounds: { x: 4, y: 2, z: 3 } });
    expect(primitiveChange(current, { bounds: [1, 2, 3], color: [1, 0, 0, 1] })).toEqual({ color: [1, 0, 0, 1] });
  });
});
