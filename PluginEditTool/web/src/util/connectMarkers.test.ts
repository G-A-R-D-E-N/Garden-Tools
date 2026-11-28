import { describe, expect, it, vi } from 'vitest';
import * as THREE from 'three';
import { buildConnectMarkers, disposeConnectMarkers } from './connectMarkers';

describe('buildConnectMarkers', () => {
  it('disposes the geometry and material of every marker', () => {
    const group = buildConnectMarkers([{ world: [1, 2, 3], active: false }, { world: [4, 5, 6], active: true }], 10);
    const meshes = group.children as THREE.Mesh[];
    const geometrySpies = meshes.map(m => vi.spyOn(m.geometry, 'dispose'));
    const materialSpies = meshes.map(m => vi.spyOn(m.material as THREE.Material, 'dispose'));
    disposeConnectMarkers(group);
    for (const spy of [...geometrySpies, ...materialSpies]) expect(spy).toHaveBeenCalledTimes(1);
  });

  it('places one marker per point and draws the active one larger and in the highlight colour', () => {
    const group = buildConnectMarkers([{ world: [1, 2, 3], active: false }, { world: [4, 5, 6], active: true }], 10);
    const meshes = group.children as THREE.Mesh[];
    expect(meshes.map(m => m.position.toArray())).toEqual([[1, 2, 3], [4, 5, 6]]);
    expect((meshes[0].material as THREE.MeshBasicMaterial).color.getHex()).toBe(0x35d0ff);
    expect((meshes[1].material as THREE.MeshBasicMaterial).color.getHex()).toBe(0xffb020);
    expect((meshes[0].geometry as THREE.SphereGeometry).parameters.radius).toBeCloseTo(0.2);
    expect((meshes[1].geometry as THREE.SphereGeometry).parameters.radius).toBeCloseTo(0.32);
    disposeConnectMarkers(group);
  });
});
