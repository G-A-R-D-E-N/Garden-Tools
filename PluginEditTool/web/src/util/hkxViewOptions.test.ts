import { describe, expect, it } from 'vitest';
import type { HkxBone } from '../backend';
import { followStep, meshDrawState, parseMeshMode, referenceModelPose, rootPosition, type Vec3 } from './hkxViewOptions';

const bone = (
  name: string, parent: number, translation: [number, number, number],
  rotation: [number, number, number, number] = [0, 0, 0, 1], scale: [number, number, number] = [1, 1, 1],
): HkxBone => ({ name, parent, translation, rotation, scale });

const close = (got: readonly number[], want: readonly number[]) => {
  expect(got).toHaveLength(want.length);
  want.forEach((value, i) => expect(got[i]).toBeCloseTo(value, 5));
};

const quarterZ = Math.SQRT1_2;

describe('parseMeshMode', () => {
  it('keeps a saved mode and falls back to Shaded for a missing or unknown value', () => {
    // Constraint: detects a stale or hand-edited localStorage value leaving the viewport with no valid mode, so the body never draws.
    expect(parseMeshMode('Off')).toBe('Off');
    expect(parseMeshMode('Wireframe')).toBe('Wireframe');
    expect(parseMeshMode(null)).toBe('Shaded');
    expect(parseMeshMode('wireframe')).toBe('Shaded');
  });
});

describe('meshDrawState', () => {
  it('draws the surface only when Shaded and the wire on Wireframe or on Shaded with the checkbox', () => {
    // Constraint: detects Wireframe mode that still draws the surface, the checkbox overlaying an Off body, and a checkbox that does nothing on Shaded.
    expect(meshDrawState('Shaded', false)).toEqual({ solid: true, wire: false });
    expect(meshDrawState('Shaded', true)).toEqual({ solid: true, wire: true });
    expect(meshDrawState('Wireframe', false)).toEqual({ solid: false, wire: true });
    expect(meshDrawState('Wireframe', true)).toEqual({ solid: false, wire: true });
    expect(meshDrawState('Off', true)).toEqual({ solid: false, wire: false });
  });
});

describe('referenceModelPose', () => {
  it('scales and rotates a child by its parent, so the ghost matches the model-space pose the host returns', () => {
    // Constraint: detects a ghost skeleton built from raw local translations (collapsed to the origin) or composed without the parent scale and rotation.
    const local = [
      bone('Root', -1, [10, 0, 0], [0, 0, quarterZ, quarterZ], [2, 2, 2]),
      bone('Child', 0, [1, 0, 0]),
      bone('Tip', 1, [0, 1, 0]),
    ];
    const model = referenceModelPose(local);
    expect(model).not.toBeNull();
    close(model![0].translation, [10, 0, 0]);
    close(model![1].translation, [10, 2, 0]);
    close(model![1].rotation, [0, 0, quarterZ, quarterZ]);
    close(model![1].scale, [2, 2, 2]);
    close(model![2].translation, [8, 2, 0]);
  });

  it('refuses a parent that does not come before its child instead of drawing a wrong pose', () => {
    // Constraint: detects an out-of-order or self-parented skeleton producing NaN or stale positions; the host's model_space_pose refuses the same input.
    expect(referenceModelPose([bone('A', 1, [0, 0, 0]), bone('B', -1, [0, 0, 0])])).toBeNull();
    expect(referenceModelPose([bone('A', 0, [0, 0, 0])])).toBeNull();
  });
});

describe('rootPosition', () => {
  it('reads the first root bone, not bone zero, and nothing for an empty pose', () => {
    // Constraint: detects Follow travel tracking a child bone (the camera would chase an animated limb) when the first listed bone is not a root.
    expect(rootPosition([bone('Child', 1, [9, 9, 9]), bone('Root', -1, [1, 2, 3])])).toEqual([1, 2, 3]);
    expect(rootPosition([])).toBeNull();
  });
});

describe('followStep', () => {
  const at = (x: number): Vec3 => [x, 0, 0];

  it('moves the camera by the travel since framing and never twice for the same travel', () => {
    // Constraint: detects a camera that accumulates the whole travel every frame (runs away) or lags a frame behind the character.
    const first = followStep(at(3), at(0), at(0), true);
    expect(first).toEqual({ step: at(3), applied: at(3) });
    const second = followStep(at(5), at(0), first.applied, true);
    expect(second).toEqual({ step: at(2), applied: at(5) });
    expect(followStep(at(5), at(0), second.applied, true).step).toEqual(at(0));
  });

  it('turning Follow travel off returns the camera to the framing centre', () => {
    // Constraint: detects an off toggle that leaves the camera where the character last was.
    const off = followStep(at(5), at(0), at(5), false);
    expect(off).toEqual({ step: at(-5), applied: at(0) });
  });

  it('measures travel from the pose the view was framed at, so framing mid-clip does not jump the camera', () => {
    // Constraint: detects a re-frame (new body, new clip) at travel 4 followed by a camera shift of 4 on the next frame.
    expect(followStep(at(4), at(4), at(0), true)).toEqual({ step: at(0), applied: at(0) });
    expect(followStep(at(6), at(4), at(0), true)).toEqual({ step: at(2), applied: at(2) });
  });
});
