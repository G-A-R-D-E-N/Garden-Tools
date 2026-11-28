import { describe, expect, it } from 'vitest';
import { hexToRgb, parseTriplet, rgbToHex, tripletText } from './cellColour';

describe('cell colour triplets', () => {
  it('formats the stored triplet with spaces', () => {
    expect(tripletText([52, 61, 65])).toBe('52 61 65');
    expect(tripletText(null)).toBe('');
  });

  it('parses spaced or comma-separated triplets of 0-255 integers', () => {
    expect(parseTriplet('52 61 65')).toEqual([52, 61, 65]);
    expect(parseTriplet('1, 2,3')).toEqual([1, 2, 3]);
    expect(parseTriplet('1 2')).toBeNull();
    expect(parseTriplet('1 2 300')).toBeNull();
    expect(parseTriplet('1 2 1.5')).toBeNull();
    expect(parseTriplet('a b c')).toBeNull();
  });

  it('converts between the triplet and a colour input hex value', () => {
    expect(rgbToHex([52, 61, 65])).toBe('#343d41');
    expect(rgbToHex([0, 0, 0])).toBe('#000000');
    expect(hexToRgb('#ff8000')).toEqual([255, 128, 0]);
    expect(hexToRgb(rgbToHex([9, 200, 17]))).toEqual([9, 200, 17]);
  });
});
