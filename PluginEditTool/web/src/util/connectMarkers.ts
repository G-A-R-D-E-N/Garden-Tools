import * as THREE from 'three';

export function buildConnectMarkers(points: { world: number[]; active: boolean }[], radius: number): THREE.Group {
  const group = new THREE.Group();
  const size = radius * 0.02;
  for (const point of points) {
    const geometry = new THREE.SphereGeometry(point.active ? size * 1.6 : size, 12, 8);
    const material = new THREE.MeshBasicMaterial({ color: point.active ? 0xffb020 : 0x35d0ff, depthTest: false });
    const marker = new THREE.Mesh(geometry, material);
    marker.position.fromArray(point.world);
    marker.renderOrder = 1;
    group.add(marker);
  }
  return group;
}

export function disposeConnectMarkers(group: THREE.Group): void {
  group.traverse(object => {
    if (!(object instanceof THREE.Mesh)) return;
    object.geometry.dispose();
    (object.material as THREE.Material).dispose();
  });
}
