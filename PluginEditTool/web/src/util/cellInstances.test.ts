import * as THREE from 'three';
import { describe, expect, it } from 'vitest';
import type { CellPlacedReference } from '../backend';
import { applyHidden, type InstanceRef, refRotation, refScale } from './cellInstances';

const placed = (formKey: string, x: number): CellPlacedReference => ({
  formKey,
  recordType: 'PlacedObject',
  baseType: 'STAT',
  baseFormKey: '000002:Fallout4.esm',
  modelPath: 'Clutter/Lamp.nif',
  position: { x, y: 0, z: 0 },
  rotation: { x: 0, y: 0, z: 0 },
  scale: 1,
});

describe('applyHidden', () => {
  const instanceFor = (ref: CellPlacedReference, local?: THREE.Matrix4, billboard = false) => {
    const mesh = new THREE.InstancedMesh(new THREE.BoxGeometry(1, 1, 1), new THREE.MeshBasicMaterial(), 1);
    const entry: InstanceRef = { mesh, id: 0, ref, billboard, local };
    return { mesh, keyToInstances: new Map([[ref.formKey, [entry]]]) };
  };
  const matrixOf = (mesh: THREE.InstancedMesh) => {
    const m = new THREE.Matrix4();
    mesh.getMatrixAt(0, m);
    return m.elements;
  };

  it('applies the local matrix after the reference transform', () => {
    const local = new THREE.Matrix4().makeScale(2, 3, 4);
    const { mesh, keyToInstances } = instanceFor(placed('a', 10), local);
    applyHidden(keyToInstances, undefined, new THREE.Vector3());
    const e = matrixOf(mesh);
    expect([e[0], e[5], e[10]]).toEqual([2, 3, 4]);
    expect([e[12], e[13], e[14]]).toEqual([10, 0, 0]);
  });

  it('subtracts the cell centre from the reference position', () => {
    const { mesh, keyToInstances } = instanceFor(placed('a', 10));
    applyHidden(keyToInstances, undefined, new THREE.Vector3(10, 0, 0));
    expect(matrixOf(mesh)[12]).toBe(0);
  });

  it('collapses a hidden reference to zero scale and keeps the local matrix out of it', () => {
    const { mesh, keyToInstances } = instanceFor(placed('a', 10), new THREE.Matrix4().makeScale(2, 2, 2));
    applyHidden(keyToInstances, new Set(['a']), new THREE.Vector3());
    expect([matrixOf(mesh)[0], matrixOf(mesh)[5], matrixOf(mesh)[10]]).toEqual([0, 0, 0]);
  });

  it('leaves billboard entries alone', () => {
    const { mesh, keyToInstances } = instanceFor(placed('a', 10), undefined, true);
    applyHidden(keyToInstances, new Set(['a']), new THREE.Vector3());
    expect(matrixOf(mesh)[0]).toBe(1);
    expect(matrixOf(mesh)[12]).toBe(0);
  });
});

describe('refRotation', () => {
  it('builds an XYZ-ordered euler from the reference angles', () => {
    const euler = refRotation(0.1, 0.2, 0.3);
    expect(euler.order).toBe('XYZ');
    expect([euler.x, euler.y, euler.z]).toEqual([0.1, 0.2, 0.3]);
    const quat = new THREE.Quaternion().setFromEuler(euler);
    const expected = new THREE.Quaternion().setFromEuler(new THREE.Euler(0.1, 0.2, 0.3, 'XYZ'));
    expect(quat.angleTo(expected)).toBeLessThan(1e-6);
  });

  it('keeps XYZ order rather than the ZYX order a Rz*Ry*Rx product would use', () => {
    const xyz = new THREE.Quaternion().setFromEuler(refRotation(0.7, 0.4, 0.2));
    const zyx = new THREE.Quaternion().setFromEuler(new THREE.Euler(0.7, 0.4, 0.2, 'ZYX'));
    expect(xyz.angleTo(zyx)).toBeGreaterThan(0.01);
  });

  it('writes into the supplied euler when one is given', () => {
    const target = new THREE.Euler();
    expect(refRotation(1, 2, 3, target)).toBe(target);
    expect([target.x, target.y, target.z, target.order]).toEqual([1, 2, 3, 'XYZ']);
  });
});

describe('refScale', () => {
  it('keeps a positive scale', () => {
    expect(refScale(2.5)).toBe(2.5);
    expect(refScale(0.001)).toBe(0.001);
  });

  it('falls back to 1 for zero, negative and non-numeric scales', () => {
    expect(refScale(0)).toBe(1);
    expect(refScale(-3)).toBe(1);
    expect(refScale(Number.NaN)).toBe(1);
  });
});
