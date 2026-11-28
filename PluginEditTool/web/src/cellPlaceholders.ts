import * as THREE from 'three';
import type { NifGeo } from './NifViewport';
import type { CellPlacedReference } from './backend';
import type { ToggleableLayer } from './cellLighting';
import { primitiveVolume, type PrimitiveShape } from './cellPrimitive';
import { refRotation, refScale } from './util/cellInstances';
import { hasRenderableShapes } from './util/cellLayer';

export type PlaceholderKind = 'marker' | 'unresolved' | 'effect' | 'trigger' | 'room' | 'xmarker';

export type PlaceholderItem = { ref: CellPlacedReference; kind: PlaceholderKind; recordType: string };

export type PlaceholderGroup = {
  mesh: THREE.InstancedMesh;
  refs: CellPlacedReference[];
  locals: THREE.Matrix4[];
  layer: ToggleableLayer;
  recordType: string;
  pickable: boolean;
};

export type PlaceholderBuild = {
  groups: PlaceholderGroup[];
  disposables: { dispose: () => void }[];
};

export type LocalBox = { center: [number, number, number]; size: [number, number, number] };

export const MARKER_RADIUS = 3;
export const XMARKER_RADIUS = 10;
export const TRIGGER_DEFAULT_SIZE = 64;
export const ROOM_DEFAULT_SIZE = 256;
export const BOX_FILL_OPACITY = 0.08;

const LAYER_OF: Record<PlaceholderKind, ToggleableLayer> = {
  marker: 'marker',
  unresolved: 'marker',
  effect: 'effect',
  trigger: 'trigger',
  room: 'room',
  xmarker: 'marker',
};

export const PLACEHOLDER_COLOURS: Record<PlaceholderKind, number> = {
  marker: 0x5a6472,
  unresolved: 0xd08a4a,
  effect: 0xc27cff,
  trigger: 0xff9900,
  room: 0x00ffff,
  xmarker: 0xff0000,
};

const BOX_KINDS: ReadonlySet<PlaceholderKind> = new Set(['trigger', 'room']);

const _pos = new THREE.Vector3();
const _quat = new THREE.Quaternion();
const _scale = new THREE.Vector3();
const _origin = new THREE.Vector3();
const _size = new THREE.Vector3();
const _identity = new THREE.Quaternion();
const _world = new THREE.Matrix4();

export function localBoundsOf(geo: NifGeo | { error: string } | undefined): LocalBox | null {
  if (!geo || !('shapes' in geo)) return null;
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (const shape of geo.shapes) {
    for (let i = 0; i + 2 < shape.verts.length; i += 3) {
      for (let a = 0; a < 3; a++) {
        const v = shape.verts[i + a];
        if (v < min[a]) min[a] = v;
        if (v > max[a]) max[a] = v;
      }
    }
  }
  if (!Number.isFinite(min[0])) return null;
  return {
    center: [(min[0] + max[0]) / 2, (min[1] + max[1]) / 2, (min[2] + max[2]) / 2],
    size: [
      Math.max(max[0] - min[0], 1),
      Math.max(max[1] - min[1], 1),
      Math.max(max[2] - min[2], 1),
    ],
  };
}

function primitiveVolumeOf(ref: Pick<CellPlacedReference, 'primitive'>) {
  return ref.primitive ? primitiveVolume(ref.primitive.bounds, ref.primitive.type) : null;
}

export function primitiveLocalBox(ref: Pick<CellPlacedReference, 'primitive'>): LocalBox | null {
  const volume = primitiveVolumeOf(ref);
  return volume ? { center: [0, 0, 0], size: volume.size } : null;
}

export function defaultBoxOf(kind: PlaceholderKind): LocalBox {
  const edge = kind === 'room' ? ROOM_DEFAULT_SIZE : TRIGGER_DEFAULT_SIZE;
  return { center: [0, 0, 0], size: [edge, edge, edge] };
}

export function placeholderLocal(out: THREE.Matrix4, kind: PlaceholderKind, bounds: LocalBox | null): THREE.Matrix4 {
  if (!BOX_KINDS.has(kind)) return out.identity();
  const box = bounds ?? defaultBoxOf(kind);
  _origin.set(box.center[0], box.center[1], box.center[2]);
  _size.set(box.size[0], box.size[1], box.size[2]);
  return out.compose(_origin, _identity, _size);
}

export function placeholderMatrix(
  out: THREE.Matrix4,
  ref: CellPlacedReference,
  worldCenter: THREE.Vector3,
  local: THREE.Matrix4,
): THREE.Matrix4 {
  _pos.set(ref.position.x - worldCenter.x, ref.position.y - worldCenter.y, ref.position.z - worldCenter.z);
  _quat.setFromEuler(refRotation(ref.rotation.x, ref.rotation.y, ref.rotation.z));
  _scale.setScalar(refScale(ref.scale));
  _world.compose(_pos, _quat, _scale);
  return out.multiplyMatrices(_world, local);
}

type Part = { geometry: THREE.BufferGeometry; material: THREE.Material; pickable: boolean };

function shapeGeometry(shape: PrimitiveShape): THREE.BufferGeometry {
  if (shape === 'sphere' || shape === 'ellipsoid') return new THREE.SphereGeometry(0.5, 16, 12);
  if (shape === 'plane') return new THREE.PlaneGeometry(1, 1);
  if (shape === 'line') {
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.Float32BufferAttribute([-0.5, -0.5, -0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5], 3));
    return geometry;
  }
  return new THREE.BoxGeometry(1, 1, 1);
}

function boxParts(colour: number, shape: PrimitiveShape, disposables: { dispose: () => void }[]): Part[] {
  const box = shapeGeometry(shape);
  const fill = new THREE.MeshBasicMaterial({
    color: colour, transparent: true, opacity: BOX_FILL_OPACITY, depthWrite: false, side: THREE.DoubleSide,
  });
  const wire = new THREE.MeshBasicMaterial({
    color: colour, wireframe: true, transparent: true, opacity: 0.6, depthWrite: false,
  });
  disposables.push(box, fill, wire);
  const wireOnly: Part = { geometry: box, material: wire, pickable: false };
  return shape === 'line' ? [wireOnly] : [{ geometry: box, material: fill, pickable: true }, wireOnly];
}

function partsOf(kind: PlaceholderKind, shape: PrimitiveShape, disposables: { dispose: () => void }[]): Part[] {
  const colour = PLACEHOLDER_COLOURS[kind];
  if (BOX_KINDS.has(kind)) return boxParts(colour, shape, disposables);
  if (kind === 'xmarker') {
    const sphere = new THREE.SphereGeometry(XMARKER_RADIUS, 12, 8);
    const material = new THREE.MeshBasicMaterial({ color: colour, toneMapped: false });
    disposables.push(sphere, material);
    return [{ geometry: sphere, material, pickable: true }];
  }
  const octa = new THREE.OctahedronGeometry(MARKER_RADIUS);
  const material = new THREE.MeshBasicMaterial({
    color: colour, transparent: true, opacity: kind === 'unresolved' ? 0.75 : 0.55, depthWrite: false,
  });
  disposables.push(octa, material);
  return [{ geometry: octa, material, pickable: true }];
}

export function buildPlaceholderGroups(
  items: PlaceholderItem[],
  worldCenter: THREE.Vector3,
  geometry: Record<string, NifGeo | { error: string }>,
): PlaceholderBuild {
  const disposables: { dispose: () => void }[] = [];
  const build: PlaceholderBuild = { groups: [], disposables };
  const byKey = new Map<string, PlaceholderItem[]>();
  const shapeOf = (item: PlaceholderItem): PrimitiveShape =>
    BOX_KINDS.has(item.kind) ? primitiveVolumeOf(item.ref)?.shape ?? 'box' : 'box';
  for (const item of items) {
    const exempt = item.kind === 'xmarker' || (BOX_KINDS.has(item.kind) && !!item.ref.primitive);
    if (!exempt && hasRenderableShapes(item.ref.modelPath ? geometry[item.ref.modelPath] : undefined)) continue;
    const key = `${item.kind}|${item.recordType}|${shapeOf(item)}`;
    const list = byKey.get(key);
    if (list) list.push(item); else byKey.set(key, [item]);
  }
  const partsByKey = new Map<string, Part[]>();
  const boundsByPath = new Map<string, LocalBox | null>();
  const boundsFor = (kind: PlaceholderKind, ref: CellPlacedReference): LocalBox | null => {
    if (!BOX_KINDS.has(kind)) return null;
    const fromPrimitive = primitiveLocalBox(ref);
    if (fromPrimitive) return fromPrimitive;
    const path = ref.modelPath;
    if (!path) return null;
    if (!boundsByPath.has(path)) boundsByPath.set(path, localBoundsOf(geometry[path]));
    return boundsByPath.get(path) ?? null;
  };
  const instance = new THREE.Matrix4();
  for (const list of byKey.values()) {
    const { kind, recordType } = list[0];
    const shape = shapeOf(list[0]);
    const partsKey = `${kind}|${shape}`;
    let parts = partsByKey.get(partsKey);
    if (!parts) {
      parts = partsOf(kind, shape, disposables);
      partsByKey.set(partsKey, parts);
    }
    const locals = list.map(item => placeholderLocal(new THREE.Matrix4(), kind, boundsFor(kind, item.ref)));
    for (const part of parts) {
      const mesh = new THREE.InstancedMesh(part.geometry, part.material, list.length);
      list.forEach((item, i) => {
        mesh.setMatrixAt(i, placeholderMatrix(instance, item.ref, worldCenter, locals[i]));
      });
      mesh.instanceMatrix.needsUpdate = true;
      const refs = list.map(item => item.ref);
      mesh.userData.refs = refs;
      build.groups.push({ mesh, refs, locals, layer: LAYER_OF[kind], recordType, pickable: part.pickable });
    }
  }
  return build;
}
