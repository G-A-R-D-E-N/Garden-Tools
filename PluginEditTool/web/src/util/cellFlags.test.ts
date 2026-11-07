import { describe, expect, it } from 'vitest';
import { CELL_FLAGS, hasFlag, setFlag, unknownFlagBits, withUnknownBits } from './cellFlags';

describe('cell flag bitmask', () => {
  it('names each bit once and keeps the Mutagen values', () => {
    const bits = CELL_FLAGS.map(f => f.bit);
    expect(new Set(bits).size).toBe(bits.length);
    expect(CELL_FLAGS.find(f => f.label === 'Has water')?.bit).toBe(0x0002);
    expect(CELL_FLAGS.find(f => f.label === 'Public area')?.bit).toBe(0x0020);
    expect(CELL_FLAGS.find(f => f.locked)?.bit).toBe(0x0001);
  });

  it('setFlag turns one bit on or off and leaves the others alone', () => {
    expect(setFlag(3, 0x0020, true)).toBe(0x23);
    expect(setFlag(0x23, 0x0002, false)).toBe(0x21);
    expect(setFlag(3, 0x0002, true)).toBe(3);
    expect(setFlag(1, 0x0002, false)).toBe(1);
  });

  it('hasFlag reads a single bit', () => {
    expect(hasFlag(3, 0x0002)).toBe(true);
    expect(hasFlag(3, 0x0004)).toBe(false);
  });

  it('unknown bits are the ones no named flag covers', () => {
    expect(unknownFlagBits(3)).toBe(0);
    expect(unknownFlagBits(19)).toBe(0x10);
    expect(unknownFlagBits(0xffff)).toBe(0xc210);
  });

  it('withUnknownBits replaces only the unknown part and keeps named flags', () => {
    expect(withUnknownBits(3, 0x10)).toBe(19);
    expect(withUnknownBits(19, 0)).toBe(3);
    expect(withUnknownBits(0xffff, 0)).toBe(0x3def);
  });
});
