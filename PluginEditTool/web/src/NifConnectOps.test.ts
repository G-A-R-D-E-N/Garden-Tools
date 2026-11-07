import { describe, expect, it } from 'vitest';
import type { NifConnectBlock, NifConnectPoint } from './backend';
import { connectOps } from './NifConnectOps';

const point = (name: string, translation = [0, 0, 0]): NifConnectPoint =>
  ({ parent: 'Chest', name, rotation: [1, 0, 0, 0], translation, scale: 1 });

const parents = (points: NifConnectPoint[]): NifConnectBlock =>
  ({ block: 2, type: 'BSConnectPoint::Parents', name: 'CPA', points });

const children = (names: string[], skinned = true): NifConnectBlock =>
  ({ block: 3, type: 'BSConnectPoint::Children', name: 'CPT', skinned, names });

describe('connectOps for connect point parents', () => {
  it('removes a middle row by rewriting it from the row after and dropping the last', () => {
    const before = parents([point('A'), point('B', [5, 5, 5]), point('C', [7, 7, 7])]);
    const after = parents([point('A'), point('C', [7, 7, 7])]);
    expect(connectOps(before, after)).toEqual([
      { op: 'set', block: 2, index: 1, field: 'name', value: 'C' },
      { op: 'set', block: 2, index: 1, field: 'translation', value: [7, 7, 7] },
      { op: 'remove', block: 2, index: 2 },
    ]);
  });

  it('orders sets first, then removes from the end', () => {
    const before = parents([point('A'), point('B'), point('C'), point('D')]);
    const after = parents([point('A2'), point('B')]);
    expect(connectOps(before, after)).toEqual([
      { op: 'set', block: 2, index: 0, field: 'name', value: 'A2' },
      { op: 'remove', block: 2, index: 3 },
      { op: 'remove', block: 2, index: 2 },
    ]);
  });

  it('orders sets first, then appends', () => {
    const before = parents([point('A')]);
    const after = parents([point('A2'), point('B'), point('C')]);
    expect(connectOps(before, after)).toEqual([
      { op: 'set', block: 2, index: 0, field: 'name', value: 'A2' },
      { op: 'add', block: 2, value: point('B') },
      { op: 'add', block: 2, value: point('C') },
    ]);
  });
});

describe('connectOps for connect point children', () => {
  it('sets the skinned flag and a shifted name, then removes the last child', () => {
    const before = children(['A', 'B', 'C'], true);
    const after = children(['A', 'C'], false);
    expect(connectOps(before, after)).toEqual([
      { op: 'set', block: 3, field: 'skinned', value: false },
      { op: 'set', block: 3, index: 1, field: 'name', value: 'C' },
      { op: 'remove', block: 3, index: 2 },
    ]);
  });
});

describe('connectOps across block types', () => {
  it('emits nothing when the draft type differs from the original', () => {
    expect(connectOps(parents([point('A')]), children(['A']))).toEqual([]);
  });
});
