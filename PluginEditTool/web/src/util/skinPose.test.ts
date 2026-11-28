import { describe, expect, it } from 'vitest';
import type { HkxBone } from '../backend';
import { poseShape, type ShapeSkin, type SkinBone } from './skinPose';

const hkx = (name: string, translation: [number, number, number], rotation: [number, number, number, number] = [0, 0, 0, 1]): HkxBone =>
  ({ name, parent: -1, translation, rotation, scale: [1, 1, 1] });

const skinBone = (name: string, translation = [0, 0, 0], rotation = [1, 0, 0, 0, 1, 0, 0, 0, 1]): SkinBone =>
  ({ name, rotation, translation, scale: 1 });

const close = (got: number[], want: number[]) => {
  expect(got).toHaveLength(want.length);
  want.forEach((value, i) => expect(got[i]).toBeCloseTo(value, 5));
};

describe('poseShape', () => {
  it('moves a vertex with the HKX bone of the same name, matched without regard to case', () => {
    // Constraint: detects a skinned body left at its NIF bind position, or a bone missed because HKX and NIF differ in letter case.
    const skin: ShapeSkin = { weights: [1, 0, 0, 0], boneIndices: [0, 0, 0, 0], bones: [skinBone('Hip')] };
    const out = poseShape([1, 2, 3], [0, 0, 1], skin, [hkx('hip', [10, 0, 0])]);
    close(out.positions, [11, 2, 3]);
    close(out.normals, [0, 0, 1]);
    expect(out.unmatched).toBe(0);
  });

  it('applies BoneData before the pose and rotates the normal with the bone', () => {
    // Constraint: detects a swapped matrix order (pose applied before BoneData) and a normal left unrotated.
    const skin: ShapeSkin = { weights: [1, 0, 0, 0], boneIndices: [0, 0, 0, 0], bones: [skinBone('Hip', [0, 0, -5])] };
    const quarter = Math.SQRT1_2;
    const out = poseShape([1, 0, 5], [1, 0, 0], skin, [hkx('Hip', [0, 0, 0], [0, 0, quarter, quarter])]);
    close(out.positions, [0, 1, 0]);
    close(out.normals, [0, 1, 0]);
  });

  it('renormalises the weights and skips zero-weight slots', () => {
    // Constraint: detects weights that do not sum to one pulling a vertex towards the origin, and a zero slot indexing a bone it does not use.
    const skin: ShapeSkin = {
      weights: [0.5, 0.25, 0, 0], boneIndices: [0, 1, 1, 1], bones: [skinBone('A'), skinBone('B')],
    };
    const out = poseShape([0, 0, 0], [], skin, [hkx('A', [9, 0, 0]), hkx('B', [0, 0, 0])]);
    close(out.positions, [6, 0, 0]);
    expect(out.normals).toEqual([]);
  });

  it('keeps the NIF position for a bone the skeleton lacks and counts it', () => {
    // Constraint: detects a body that collapses to the origin, or hides a skeleton whose bone names do not match the mesh.
    const skin: ShapeSkin = {
      weights: [1, 0, 0, 0, 1, 0, 0, 0], boneIndices: [0, 0, 0, 0, 1, 0, 0, 0], bones: [skinBone('Hip'), skinBone('Tail')],
    };
    const out = poseShape([1, 1, 1, 2, 2, 2], [], skin, [hkx('Hip', [10, 0, 0])]);
    close(out.positions, [11, 1, 1, 2, 2, 2]);
    expect(out.unmatched).toBe(1);
  });
});
