import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { billboardMatrix, shapeWorldMatrix } from './billboard';

describe('shapeWorldMatrix', () => {
  it('reads a column-major world and falls back to identity', () => {
    const world = shapeWorldMatrix([0, 2, 0, 0, -2, 0, 0, 0, 0, 0, 2, 0, 10, 20, 30, 1]);
    const p = new THREE.Vector3(1, 0, 0).applyMatrix4(world);
    expect(p.toArray()).toEqual([10, 22, 30]);
    expect(shapeWorldMatrix(undefined).equals(new THREE.Matrix4())).toBe(true);
    expect(shapeWorldMatrix([1, 2]).equals(new THREE.Matrix4())).toBe(true);
  });

  it('places the billboard centroid at its world position', () => {
    const world = shapeWorldMatrix([0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1]);
    const camera = new THREE.PerspectiveCamera();
    camera.position.set(0, -100, 0);
    camera.updateMatrixWorld();
    const centroid = new THREE.Vector3(1, 0, 0);
    const out = billboardMatrix(new THREE.Matrix4(), world, centroid, camera, 0);
    const placed = centroid.clone().applyMatrix4(out);
    expect(placed.x).toBeCloseTo(10);
    expect(placed.y).toBeCloseTo(21);
    expect(placed.z).toBeCloseTo(30);
  });
});
