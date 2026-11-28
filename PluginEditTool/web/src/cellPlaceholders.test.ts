import * as THREE from 'three';
import { describe, expect, it } from 'vitest';
import type { CellPlacedReference } from './backend';
import {
  MARKER_RADIUS, PLACEHOLDER_COLOURS, ROOM_DEFAULT_SIZE, TRIGGER_DEFAULT_SIZE, buildPlaceholderGroups, defaultBoxOf,
  localBoundsOf, placeholderLocal, placeholderMatrix, type PlaceholderItem,
} from './cellPlaceholders';
import type { NifGeo } from './NifViewport';

const reference = (formKey: string, overrides: Partial<CellPlacedReference> = {}): CellPlacedReference => ({
  formKey,
  recordType: 'PlacedObject',
  baseType: 'STAT',
  baseFormKey: '000002:Fallout4.esm',
  modelPath: null,
  position: { x: 0, y: 0, z: 0 },
  rotation: { x: 0, y: 0, z: 0 },
  scale: 1,
  ...overrides,
});

const nifWithVerts = (...shapes: number[][]) => ({
  fo4: false,
  shapes: shapes.map((verts, i) => ({ name: `s${i}`, verts, tris: [], normals: [], uvs: [] })),
}) as unknown as NifGeo;

const nifWithMesh = (verts: number[]) => ({
  fo4: false,
  shapes: [{ name: 's0', verts, tris: [0, 1, 2], normals: [], uvs: [] }],
}) as unknown as NifGeo;

const scaleOf = (m: THREE.Matrix4) => [m.elements[0], m.elements[5], m.elements[10]];
const translationOf = (m: THREE.Matrix4) => [m.elements[12], m.elements[13], m.elements[14]];

describe('localBoundsOf', () => {
  it('returns null without a loaded mesh', () => {
    expect(localBoundsOf(undefined)).toBeNull();
    expect(localBoundsOf({ error: 'missing' })).toBeNull();
    expect(localBoundsOf(nifWithVerts([]))).toBeNull();
  });

  it('boxes every shape vertex, centre and size', () => {
    const box = localBoundsOf(nifWithVerts([0, 0, 0, 10, 20, 30], [-5, 0, 0]));
    expect(box).toEqual({ center: [2.5, 10, 15], size: [15, 20, 30] });
  });

  it('clamps a flat or single-point mesh to at least one unit on each axis', () => {
    expect(localBoundsOf(nifWithVerts([4, 4, 4]))).toEqual({ center: [4, 4, 4], size: [1, 1, 1] });
  });
});

describe('default placeholder sizes', () => {
  it('uses 64 for triggers and 256 for rooms when there are no bounds', () => {
    expect(TRIGGER_DEFAULT_SIZE).toBe(64);
    expect(ROOM_DEFAULT_SIZE).toBe(256);
    expect(defaultBoxOf('trigger').size).toEqual([64, 64, 64]);
    expect(defaultBoxOf('room').size).toEqual([256, 256, 256]);
  });
});

describe('placeholderLocal', () => {
  it('leaves point markers at identity', () => {
    const m = placeholderLocal(new THREE.Matrix4(), 'marker', { center: [1, 2, 3], size: [4, 5, 6] });
    expect(m.equals(new THREE.Matrix4())).toBe(true);
  });

  it('sizes a trigger to its default box when it has no bounds', () => {
    const m = placeholderLocal(new THREE.Matrix4(), 'trigger', null);
    expect(scaleOf(m)).toEqual([64, 64, 64]);
    expect(translationOf(m)).toEqual([0, 0, 0]);
  });

  it('fits a room box to the model bounds, offset to its centre', () => {
    const m = placeholderLocal(new THREE.Matrix4(), 'room', { center: [1, 2, 3], size: [4, 5, 6] });
    expect(scaleOf(m)).toEqual([4, 5, 6]);
    expect(translationOf(m)).toEqual([1, 2, 3]);
  });
});

describe('placeholderMatrix', () => {
  it('places the reference relative to the cell centre and applies its local box', () => {
    const local = new THREE.Matrix4().makeScale(2, 2, 2);
    const out = placeholderMatrix(
      new THREE.Matrix4(),
      reference('a', { position: { x: 10, y: 20, z: 30 } }),
      new THREE.Vector3(1, 2, 3),
      local,
    );
    expect(translationOf(out)).toEqual([9, 18, 27]);
    expect(scaleOf(out)).toEqual([2, 2, 2]);
  });

  it('treats a non-positive reference scale as 1', () => {
    const out = placeholderMatrix(
      new THREE.Matrix4(),
      reference('a', { scale: 0 }),
      new THREE.Vector3(),
      new THREE.Matrix4(),
    );
    expect(scaleOf(out)).toEqual([1, 1, 1]);
  });
});

describe('buildPlaceholderGroups', () => {
  const worldCenter = new THREE.Vector3(10, 0, 0);
  const r1 = reference('a', { position: { x: 10, y: 0, z: 0 } });
  const r2 = reference('b', { position: { x: 110, y: 0, z: 0 } });
  const r3 = reference('c');
  const trigger = reference('d', { modelPath: 'Triggers/TriggerBox.nif', recordType: 'PlacedTrap' });
  const effect = reference('e');
  const items: PlaceholderItem[] = [
    { ref: r1, kind: 'marker', recordType: 'STAT' },
    { ref: r2, kind: 'marker', recordType: 'STAT' },
    { ref: r3, kind: 'unresolved', recordType: 'STAT' },
    { ref: trigger, kind: 'trigger', recordType: 'TRAP' },
    { ref: effect, kind: 'effect', recordType: '(FX / mist)' },
  ];

  it('returns nothing for no items', () => {
    const build = buildPlaceholderGroups([], worldCenter, {});
    expect(build.groups).toEqual([]);
  });

  it('builds one group per kind and record type, with a trigger fill and wire pair', () => {
    const build = buildPlaceholderGroups(items, worldCenter, {});
    expect(build.groups).toHaveLength(5);
    const triggers = build.groups.filter(g => g.layer === 'trigger');
    expect(triggers).toHaveLength(2);
    expect(triggers.map(g => g.pickable)).toEqual([true, false]);
  });

  it('groups same-kind references that share a record type into one instanced mesh', () => {
    const [markers] = buildPlaceholderGroups(items, worldCenter, {}).groups.filter(
      g => g.layer === 'marker' && g.recordType === 'STAT' && g.refs.length === 2,
    );
    expect(markers.refs).toEqual([r1, r2]);
    expect(markers.mesh.count).toBe(2);
    expect(markers.pickable).toBe(true);
    expect((markers.mesh.geometry as THREE.OctahedronGeometry).parameters.radius).toBe(MARKER_RADIUS);
    expect((markers.mesh.material as THREE.MeshBasicMaterial).color.getHex()).toBe(PLACEHOLDER_COLOURS.marker);
  });

  it('places each instance at its reference position relative to the cell centre', () => {
    const [markers] = buildPlaceholderGroups(items, worldCenter, {}).groups.filter(
      g => g.layer === 'marker' && g.refs.length === 2,
    );
    const m = new THREE.Matrix4();
    markers.mesh.getMatrixAt(0, m);
    expect(translationOf(m)).toEqual([0, 0, 0]);
    markers.mesh.getMatrixAt(1, m);
    expect(translationOf(m)).toEqual([100, 0, 0]);
  });

  it('draws an unresolved marker in the orange colour on the marker layer', () => {
    const unresolved = buildPlaceholderGroups(items, worldCenter, {}).groups.find(g => g.refs[0] === r3);
    expect(unresolved?.layer).toBe('marker');
    const material = unresolved?.mesh.material as THREE.MeshBasicMaterial;
    expect(material.color.getHex()).toBe(PLACEHOLDER_COLOURS.unresolved);
    expect(material.opacity).toBe(0.75);
  });

  it('draws an effect placeholder in the purple colour on the effect layer', () => {
    const fx = buildPlaceholderGroups(items, worldCenter, {}).groups.find(g => g.refs[0] === effect);
    expect(fx?.layer).toBe('effect');
    expect(fx?.recordType).toBe('(FX / mist)');
    expect((fx?.mesh.material as THREE.MeshBasicMaterial).color.getHex()).toBe(PLACEHOLDER_COLOURS.effect);
  });

  it('gives a trigger a translucent pickable fill and a non-pickable wireframe at the default size', () => {
    const [fill, wire] = buildPlaceholderGroups(items, worldCenter, {}).groups.filter(g => g.layer === 'trigger');
    const fillMat = fill.mesh.material as THREE.MeshBasicMaterial;
    const wireMat = wire.mesh.material as THREE.MeshBasicMaterial;
    expect(fillMat.opacity).toBe(0.08);
    expect(fillMat.wireframe).toBe(false);
    expect(wireMat.wireframe).toBe(true);
    expect(fill.mesh.geometry).toBe(wire.mesh.geometry);
    expect(scaleOf(fill.locals[0])).toEqual([64, 64, 64]);
  });

  it('keeps a marker placeholder when its reference has no model in the geometry map', () => {
    const marker = reference('m', { modelPath: 'Clutter/Beacon.nif' });
    const build = buildPlaceholderGroups([{ ref: marker, kind: 'marker', recordType: 'STAT' }], worldCenter, {});
    expect(build.groups).toHaveLength(1);
    expect(build.groups[0].layer).toBe('marker');
  });

  it('keeps an unresolved placeholder when its model failed to load', () => {
    const marker = reference('m', { modelPath: 'Clutter/Beacon.nif' });
    const geometry = { 'Clutter/Beacon.nif': { error: 'unavailable' } };
    const [group] = buildPlaceholderGroups(
      [{ ref: marker, kind: 'unresolved', recordType: 'STAT' }], worldCenter, geometry,
    ).groups;
    expect((group.mesh.material as THREE.MeshBasicMaterial).color.getHex()).toBe(PLACEHOLDER_COLOURS.unresolved);
  });

  it('drops a marker whose model has usable geometry, so the model is drawn instead', () => {
    const marker = reference('m', { modelPath: 'Clutter/Beacon.nif' });
    const geometry = { 'Clutter/Beacon.nif': nifWithMesh([0, 0, 0, 1, 0, 0, 0, 1, 0]) };
    const build = buildPlaceholderGroups([{ ref: marker, kind: 'marker', recordType: 'STAT' }], worldCenter, geometry);
    expect(build.groups).toEqual([]);
  });

  it('drops a room or trigger whose model has usable geometry', () => {
    const room = reference('r', { modelPath: 'Architecture/RoomBounds.nif' });
    const trigger = reference('t', { modelPath: 'Triggers/TriggerBox.nif' });
    const geometry = {
      'Architecture/RoomBounds.nif': nifWithMesh([0, 0, 0, 1, 0, 0, 0, 1, 0]),
      'Triggers/TriggerBox.nif': nifWithMesh([0, 0, 0, 1, 0, 0, 0, 1, 0]),
    };
    const build = buildPlaceholderGroups([
      { ref: room, kind: 'room', recordType: 'STAT' },
      { ref: trigger, kind: 'trigger', recordType: 'TRAP' },
    ], worldCenter, geometry);
    expect(build.groups).toEqual([]);
  });

  it('sizes a room wireframe to the bounds of geometry with no triangles', () => {
    const room = reference('r', { modelPath: 'Architecture/RoomBounds.nif' });
    const geometry = { 'Architecture/RoomBounds.nif': nifWithVerts([0, 0, 0, 10, 20, 30]) };
    const build = buildPlaceholderGroups([{ ref: room, kind: 'room', recordType: 'STAT' }], worldCenter, geometry);
    expect(build.groups).toHaveLength(2);
    const [fill, wire] = build.groups;
    expect(fill.layer).toBe('room');
    expect(fill.pickable).toBe(true);
    expect(wire.pickable).toBe(false);
    expect((wire.mesh.material as THREE.MeshBasicMaterial).wireframe).toBe(true);
    expect((wire.mesh.material as THREE.MeshBasicMaterial).color.getHex()).toBe(0x00ffff);
    expect((fill.mesh.material as THREE.MeshBasicMaterial).opacity).toBe(0.08);
    expect(scaleOf(fill.locals[0])).toEqual([10, 20, 30]);
  });

  it('sizes a room box from its primitive half-extents, scaled and rotated with the reference', () => {
    const room = reference('r', {
      scale: 2,
      rotation: { x: 0, y: 90, z: 0 },
      primitive: { type: 1, bounds: { x: 512, y: 288, z: 192 } },
      baseEditorId: 'RoomMarker',
    });
    const geometry = { 'Architecture/RoomBounds.nif': nifWithVerts([0, 0, 0, 10, 20, 30]) };
    const build = buildPlaceholderGroups([{ ref: room, kind: 'room', recordType: 'STAT' }], worldCenter, geometry);
    expect(build.groups).toHaveLength(2);
    expect(scaleOf(build.groups[0].locals[0])).toEqual([1024, 576, 384]);
    const world = new THREE.Matrix4();
    build.groups[0].mesh.getMatrixAt(0, world);
    const worldScale = new THREE.Vector3().setFromMatrixScale(world);
    expect(worldScale.x).toBeCloseTo(2048, 3);
    expect(worldScale.y).toBeCloseTo(1152, 3);
    expect(worldScale.z).toBeCloseTo(768, 3);
  });

  it('keeps a primitive room box even when its model has renderable geometry', () => {
    const room = reference('r', {
      modelPath: 'Architecture/RoomBounds.nif',
      primitive: { type: 1, bounds: { x: 1, y: 2, z: 3 } },
      baseEditorId: 'RoomMarker',
    });
    const geometry = { 'Architecture/RoomBounds.nif': nifWithMesh([0, 0, 0, 1, 0, 0, 0, 1, 0]) };
    const build = buildPlaceholderGroups([{ ref: room, kind: 'room', recordType: 'STAT' }], worldCenter, geometry);
    expect(build.groups).toHaveLength(2);
    expect(scaleOf(build.groups[0].locals[0])).toEqual([2, 4, 6]);
  });

  it('ignores non-positive primitive bounds and falls back to the model bounds', () => {
    const trigger = reference('t', {
      modelPath: 'Triggers/TriggerBox.nif',
      primitive: { type: 1, bounds: { x: 0, y: 4, z: 4 } },
    });
    const geometry = { 'Triggers/TriggerBox.nif': nifWithVerts([0, 0, 0, 10, 20, 30]) };
    const [group] = buildPlaceholderGroups([{ ref: trigger, kind: 'trigger', recordType: 'TRAP' }], worldCenter, geometry).groups;
    expect(scaleOf(group.locals[0])).toEqual([10, 20, 30]);
  });

  it('draws an xmarker as an unlit red sphere on the marker layer', () => {
    const marker = reference('x', { modelPath: 'Markers/XMarkerHeading.nif', baseEditorId: 'XMarkerHeading' });
    const build = buildPlaceholderGroups([{ ref: marker, kind: 'xmarker', recordType: 'STAT' }], worldCenter, {});
    expect(build.groups).toHaveLength(1);
    const [group] = build.groups;
    expect(group.layer).toBe('marker');
    expect(group.pickable).toBe(true);
    const material = group.mesh.material as THREE.MeshBasicMaterial;
    expect(material).toBeInstanceOf(THREE.MeshBasicMaterial);
    expect(material.color.getHex()).toBe(0xff0000);
    expect(material.toneMapped).toBe(false);
    expect((group.mesh.geometry as THREE.SphereGeometry).parameters.radius).toBe(10);
  });

  it('keeps an xmarker placeholder even when its marker model has renderable geometry', () => {
    const marker = reference('x', { modelPath: 'Markers/XMarker.nif' });
    const geometry = { 'Markers/XMarker.nif': nifWithMesh([0, 0, 0, 1, 0, 0, 0, 1, 0]) };
    const build = buildPlaceholderGroups([{ ref: marker, kind: 'xmarker', recordType: 'STAT' }], worldCenter, geometry);
    expect(build.groups).toHaveLength(1);
    expect(build.groups[0].refs).toEqual([marker]);
  });

  it('falls back to the room default size when the model is not loaded', () => {
    const room = reference('r', { modelPath: 'Architecture/RoomBounds.nif' });
    const [group] = buildPlaceholderGroups([{ ref: room, kind: 'room', recordType: 'STAT' }], worldCenter, {}).groups;
    expect(scaleOf(group.locals[0])).toEqual([256, 256, 256]);
  });

  it('keeps disposables for every shared geometry and material it creates', () => {
    const build = buildPlaceholderGroups(items, worldCenter, {});
    expect(build.disposables.length).toBeGreaterThan(0);
    for (const d of build.disposables) expect(typeof d.dispose).toBe('function');
  });
});

describe('primitive shapes', () => {
  const worldCenter = new THREE.Vector3();
  const primitiveRef = (type: number, bounds: { x: number; y: number; z: number }, baseEditorId = 'TriggerBox') =>
    reference(`p${type}`, { primitive: { type, bounds }, baseEditorId });
  const groupsFor = (ref: CellPlacedReference, kind: 'trigger' | 'room' = 'trigger') =>
    buildPlaceholderGroups([{ ref, kind, recordType: 'STAT' }], worldCenter, {}).groups;

  // Constraint, detects: a sphere primitive drawn as a box because every room and trigger primitive shares one box geometry.
  it('draws a sphere primitive as a sphere sized by its largest half-extent', () => {
    const [fill, wire] = groupsFor(primitiveRef(2, { x: 50, y: 20, z: 10 }));
    expect(fill.mesh.geometry).toBeInstanceOf(THREE.SphereGeometry);
    expect(fill.mesh.geometry).toBe(wire.mesh.geometry);
    expect(scaleOf(fill.locals[0])).toEqual([100, 100, 100]);
    expect(fill.pickable).toBe(true);
  });

  // Constraint, detects: an ellipsoid primitive losing its separate semi-axes, so it renders as a sphere or a box.
  it('draws an ellipsoid primitive as a sphere scaled by each half-extent', () => {
    const [fill] = groupsFor(primitiveRef(5, { x: 60, y: 20, z: 40 }));
    expect(fill.mesh.geometry).toBeInstanceOf(THREE.SphereGeometry);
    expect(scaleOf(fill.locals[0])).toEqual([120, 40, 80]);
  });

  // Constraint, detects: a plane primitive (bounds z of 0) falling back to the default box or drawing a solid slab.
  it('draws a plane primitive as a flat rectangle even when bounds z is zero', () => {
    const [fill, wire] = groupsFor(primitiveRef(3, { x: 50, y: 30, z: 0 }), 'room');
    expect(fill.mesh.geometry).toBeInstanceOf(THREE.PlaneGeometry);
    expect(wire.mesh.geometry).toBe(fill.mesh.geometry);
    expect(scaleOf(fill.locals[0])).toEqual([100, 60, 1]);
  });

  // Constraint, detects: a line primitive drawn as a filled box instead of a single wire segment.
  it('draws a line primitive as one wire segment with nothing to pick', () => {
    const groups = groupsFor(primitiveRef(4, { x: 40, y: 0, z: 30 }));
    expect(groups).toHaveLength(1);
    const [wire] = groups;
    expect((wire.mesh.material as THREE.MeshBasicMaterial).wireframe).toBe(true);
    expect(wire.pickable).toBe(false);
    expect(wire.mesh.geometry).not.toBeInstanceOf(THREE.BoxGeometry);
    expect(scaleOf(wire.locals[0]).filter(v => v !== 0)).toEqual([80, 60]);
  });

  // Constraint, detects: box primitives losing their box geometry when other shapes were added to the same kind.
  it('keeps a box primitive as a box and groups shapes of one kind separately', () => {
    const box = primitiveRef(1, { x: 32, y: 16, z: 8 });
    const sphere = primitiveRef(2, { x: 10, y: 10, z: 10 });
    const build = buildPlaceholderGroups([
      { ref: box, kind: 'trigger', recordType: 'STAT' },
      { ref: sphere, kind: 'trigger', recordType: 'STAT' },
    ], worldCenter, {});
    expect(build.groups).toHaveLength(4);
    expect(build.groups.find(g => g.refs[0] === box)?.mesh.geometry).toBeInstanceOf(THREE.BoxGeometry);
    expect(build.groups.find(g => g.refs[0] === sphere)?.mesh.geometry).toBeInstanceOf(THREE.SphereGeometry);
  });

  // Constraint, detects: a sphere with unusable bounds producing a degenerate placeholder instead of the default box.
  it('falls back to the default box when the bounds cannot describe the shape', () => {
    const [fill] = groupsFor(primitiveRef(2, { x: 0, y: 0, z: 0 }));
    expect(fill.mesh.geometry).toBeInstanceOf(THREE.BoxGeometry);
    expect(scaleOf(fill.locals[0])).toEqual([64, 64, 64]);
  });
});
