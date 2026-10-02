import { describe, expect, it } from 'vitest';
import { parseCellGridCoordinate } from './util/cellGrid';

const accepted: Array<[string, number]> = [
  ['0', 0],
  ['-33', -33],
  ['+32', 32],
  ['2147483647', 2147483647],
  ['-2147483648', -2147483648],
];

const rejected = [
  '',
  '12abc',
  '1.5',
  '1e3',
  ' 5',
  '5 ',
  '9007199254740992',
  '2147483648',
  '-2147483649',
  '4294967297',
];

describe('parseCellGridCoordinate', () => {
  it.each(accepted)('accepts complete integer token %s', (text, expected) => {
    expect(parseCellGridCoordinate(text)).toBe(expected);
  });

  it.each(rejected)('rejects non-integer token %s', text => {
    expect(parseCellGridCoordinate(text)).toBeNull();
  });
});
