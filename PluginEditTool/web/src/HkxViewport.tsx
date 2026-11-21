import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import type { HkxBone } from './backend';

interface HkxView {
  setBones(bones: HkxBone[] | null, frameKey: string): void;
  dispose(): void;
}

function segmentPoints(bones: HkxBone[]): THREE.Vector3[] {
  const points: THREE.Vector3[] = [];
  for (const bone of bones) {
    const parent = bones[bone.parent];
    if (!parent) continue;
    points.push(new THREE.Vector3(...parent.translation), new THREE.Vector3(...bone.translation));
  }
  return points;
}

function createView(mount: HTMLDivElement): HkxView {
  const width = mount.clientWidth || 600;
  const height = mount.clientHeight || 400;

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x17181c);

  const camera = new THREE.PerspectiveCamera(50, width / height, 0.01, 1_000_000);
  camera.up.set(0, 0, 1);

  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setSize(width, height);
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  mount.appendChild(renderer.domElement);

  const controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;

  const material = new THREE.LineBasicMaterial({ color: 0x9ad4ff });
  let lines: THREE.LineSegments | null = null;
  let framedKey = '';

  const frame = (box: THREE.Box3) => {
    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());
    const radius = Math.max(size.x, size.y, size.z, 1e-3);
    controls.target.copy(center);
    camera.near = radius / 200;
    camera.far = radius * 200;
    camera.position.set(center.x + radius * 1.9, center.y - radius * 2.3, center.z + radius * 1.5);
    camera.updateProjectionMatrix();
    controls.update();
  };

  let raf = 0;
  const animate = () => {
    controls.update();
    renderer.render(scene, camera);
    raf = requestAnimationFrame(animate);
  };
  animate();

  const onResize = () => {
    const w = mount.clientWidth || 600;
    const h = mount.clientHeight || 400;
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    renderer.setSize(w, h);
  };
  const ro = new ResizeObserver(onResize);
  ro.observe(mount);

  return {
    setBones(bones, frameKey) {
      if (lines) {
        scene.remove(lines);
        lines.geometry.dispose();
        lines = null;
      }
      if (!bones || bones.length === 0) return;
      lines = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(segmentPoints(bones)), material);
      scene.add(lines);
      if (frameKey !== framedKey) {
        framedKey = frameKey;
        frame(new THREE.Box3().setFromPoints(bones.map(b => new THREE.Vector3(...b.translation))));
      }
    },
    dispose() {
      cancelAnimationFrame(raf);
      ro.disconnect();
      controls.dispose();
      lines?.geometry.dispose();
      material.dispose();
      renderer.dispose();
      if (renderer.domElement.parentElement === mount) mount.removeChild(renderer.domElement);
    },
  };
}

export default function HkxViewport({ bones, frameKey }: { bones: HkxBone[] | null; frameKey: string }) {
  const mountRef = useRef<HTMLDivElement>(null);
  const viewRef = useRef<HkxView | null>(null);

  useEffect(() => {
    const mount = mountRef.current;
    if (!mount) return;
    const view = createView(mount);
    viewRef.current = view;
    return () => {
      viewRef.current = null;
      view.dispose();
    };
  }, []);

  useEffect(() => {
    viewRef.current?.setBones(bones, frameKey);
  }, [bones, frameKey]);

  return <div ref={mountRef} className="nif-viewport hkx-viewport" />;
}
