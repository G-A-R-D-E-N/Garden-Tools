import { describe, expect, it } from 'vitest';
import { HAVOK_METRES_TO_NIF, placeCollisionPoints, type NifTransform } from './collisionGeometry';

const identity: NifTransform = { t: [0, 0, 0], r: [1, 0, 0, 0, 1, 0, 0, 0, 1], s: 1 };

describe('placeCollisionPoints', () => {
  it('converts a Havok metre point to NIF units under an identity node', () => {
    // Regression: packfile points are metres, so skipping the 69.99125 factor draws every body about 70 times too small.
    const out = placeCollisionPoints([1, 0, 0], identity);
    expect(out[0]).toBeCloseTo(HAVOK_METRES_TO_NIF, 6);
    expect(out[1]).toBeCloseTo(0, 6);
    expect(out[2]).toBeCloseTo(0, 6);
  });

  it('places a point by its node transform: rotation, then scale, then translation', () => {
    // Regression: a body must follow its node's transform, so a node turned 90 degrees about Z, scaled by 2 and moved must carry the point with it.
    const node: NifTransform = { t: [10, 20, 30], r: [0, -1, 0, 1, 0, 0, 0, 0, 1], s: 2 };
    const out = placeCollisionPoints([1, 0, 0], node);
    expect(out[0]).toBeCloseTo(10, 6);
    expect(out[1]).toBeCloseTo(20 + 2 * HAVOK_METRES_TO_NIF, 6);
    expect(out[2]).toBeCloseTo(30, 6);
  });
});
