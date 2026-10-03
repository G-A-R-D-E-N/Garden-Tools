import * as THREE from 'three';
import type { CellPlacedReference } from '../backend';

export type InstanceRef = {
  mesh: THREE.InstancedMesh; id: number; ref: CellPlacedReference; billboard: boolean;
  local?: THREE.Matrix4;
};

const _zeroM = new THREE.Matrix4().makeScale(0, 0, 0);
const _hidePos = new THREE.Vector3();
const _hideQuat = new THREE.Quaternion();
const _hideScale = new THREE.Vector3();
const _hideM = new THREE.Matrix4();

export function applyHidden(keyToInstances: Map<string, InstanceRef[]>, hidden: ReadonlySet<string> | undefined, worldCenter: THREE.Vector3) {
  const dirty = new Set<THREE.InstancedMesh>();
  for (const [formKey, entries] of keyToInstances) {
    const isHidden = hidden?.has(formKey) ?? false;
    for (const e of entries) {
      if (e.billboard) { continue; }
      if (isHidden) {
        e.mesh.setMatrixAt(e.id, _zeroM);
      } else {
        _hidePos.set(e.ref.position.x - worldCenter.x, e.ref.position.y - worldCenter.y, e.ref.position.z - worldCenter.z);
        _hideQuat.setFromEuler(new THREE.Euler(e.ref.rotation.x, e.ref.rotation.y, e.ref.rotation.z, 'XYZ'));
        _hideScale.setScalar(e.ref.scale || 1);
        _hideM.compose(_hidePos, _hideQuat, _hideScale);

        if (e.local) _hideM.multiply(e.local);
        e.mesh.setMatrixAt(e.id, _hideM);
      }
      dirty.add(e.mesh);
    }
  }
  for (const m of dirty) m.instanceMatrix.needsUpdate = true;
}

export function applyVisibility(
  entries: { mesh: THREE.Object3D; recordType: string }[],
  hiddenTypes?: ReadonlySet<string>,
) {
  for (const e of entries) e.mesh.visible = !hiddenTypes || !hiddenTypes.has(e.recordType);
}

const _selMat = new THREE.Matrix4();
const _selPos = new THREE.Vector3();
const _selQuat = new THREE.Quaternion();
const _selScale = new THREE.Vector3();
const _selCenter = new THREE.Vector3();
const _selSize = new THREE.Vector3();
const _selLocal = new THREE.Matrix4();

export function applySelection(
  highlight: THREE.LineSegments,
  modelBBox: Map<string, THREE.Box3>,
  refByKey: Map<string, CellPlacedReference>,
  selectedKey: string | null,
  worldCenter: THREE.Vector3,
  scolBBox?: Map<string, THREE.Box3>,
) {
  const ref = selectedKey ? refByKey.get(selectedKey) : undefined;
  if (!ref) { highlight.visible = false; return; }

  const box = (ref.modelPath ? modelBBox.get(ref.modelPath) : undefined) ?? scolBBox?.get(ref.formKey);
  if (box) { box.getCenter(_selCenter); box.getSize(_selSize); }
  else { _selCenter.set(0, 0, 0); _selSize.set(24, 24, 24); }

  _selSize.set(Math.max(_selSize.x, 1), Math.max(_selSize.y, 1), Math.max(_selSize.z, 1));

  _selPos.set(ref.position.x - worldCenter.x, ref.position.y - worldCenter.y, ref.position.z - worldCenter.z);
  _selQuat.setFromEuler(new THREE.Euler(ref.rotation.x, ref.rotation.y, ref.rotation.z, 'XYZ'));
  _selScale.setScalar(ref.scale || 1);
  _selMat.compose(_selPos, _selQuat, _selScale);
  _selLocal.makeTranslation(_selCenter.x, _selCenter.y, _selCenter.z);
  _selLocal.scale(_selSize);
  highlight.matrix.multiplyMatrices(_selMat, _selLocal);
  highlight.visible = true;
}
