import * as THREE from 'three';
import type { NifGeo, NifShapeGeo } from '../NifViewport';

export const THUMB_SIZE = 192;
export const DETAIL_SIZE = 512;
const PREPARED_CAPACITY = 24;
const FOV_DEGREES = 35;
const FRAME_MARGIN = 1.1;

export interface PreviewView {
  yaw: number;
  pitch: number;
  zoom: number;
  size: number;
}

export const THUMB_VIEW: PreviewView = { yaw: 0.65, pitch: 0.35, zoom: 1, size: THUMB_SIZE };

export interface PreviewRenderer {
  render(modelPath: string, view: PreviewView): Promise<string | null>;
}

export interface PreviewSources {
  fetchGeometry: (modelPath: string) => Promise<NifGeo | null>;
  loadTexture: (modelPath: string, relPath: string) => Promise<string>;
  decodeTexture: (url: string) => Promise<THREE.Texture>;
}

interface Prepared {
  geo: NifGeo;
  textures: Map<string, THREE.Texture>;
  dispose: () => void;
}

function diffusePath(shape: NifShapeGeo): string {
  return shape.textures?.find(t => t.slot === 0)?.path ?? '';
}

async function prepare(modelPath: string, sources: PreviewSources): Promise<Prepared | null> {
  const geo = await sources.fetchGeometry(modelPath);
  if (!geo) return null;
  const textures = new Map<string, THREE.Texture>();
  const paths = new Set(geo.shapes.map(diffusePath).filter(path => path !== ''));
  await Promise.all([...paths].map(async path => {
    try {
      const url = await sources.loadTexture(modelPath, path);
      if (url) textures.set(path, await sources.decodeTexture(url));
    } catch {
      return;
    }
  }));
  return {
    geo,
    textures,
    dispose: () => { for (const texture of textures.values()) texture.dispose(); },
  };
}

function shapeGeometry(shape: NifShapeGeo): THREE.BufferGeometry {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(shape.verts, 3));
  if (shape.uvs.length === shape.verts.length / 3 * 2) {
    geometry.setAttribute('uv', new THREE.Float32BufferAttribute(shape.uvs, 2));
  }
  geometry.setIndex(shape.tris);
  if (shape.normals.length === shape.verts.length) {
    geometry.setAttribute('normal', new THREE.Float32BufferAttribute(shape.normals, 3));
  } else {
    geometry.computeVertexNormals();
  }
  return geometry;
}

interface SceneParts {
  scene: THREE.Scene;
  root: THREE.Group;
  owned: Array<{ dispose: () => void }>;
}

function buildScene(data: Prepared): SceneParts {
  const scene = new THREE.Scene();
  const root = new THREE.Group();
  root.rotation.x = Math.PI / 2;
  scene.add(root);
  const owned: Array<{ dispose: () => void }> = [];
  for (const shape of data.geo.shapes) {
    if (!shape.verts.length || !shape.tris.length) continue;
    const geometry = shapeGeometry(shape);
    const map = data.textures.get(diffusePath(shape)) ?? null;
    const material = new THREE.MeshStandardMaterial({
      color: map ? 0xffffff : 0xa9afb8,
      map,
      roughness: 0.75,
      metalness: 0.05,
      side: THREE.DoubleSide,
    });
    owned.push(geometry, material);
    root.add(new THREE.Mesh(geometry, material));
  }
  return { scene, root, owned };
}

function addLights(scene: THREE.Scene, center: THREE.Vector3, distance: number): void {
  scene.add(new THREE.AmbientLight(0xffffff, 0.5));
  const rig: Array<[THREE.Vector3, number, number]> = [
    [new THREE.Vector3(1, 1.2, 1), 0xffffff, 1.8],
    [new THREE.Vector3(-1, 0.4, 0.6), 0xdfe8ff, 0.6],
    [new THREE.Vector3(0, 0.6, -1), 0xffffff, 0.8],
  ];
  for (const [offset, color, intensity] of rig) {
    const light = new THREE.DirectionalLight(color, intensity);
    light.position.copy(center).addScaledVector(offset.normalize(), distance);
    light.target.position.copy(center);
    scene.add(light, light.target);
  }
}

function frameCamera(root: THREE.Group, view: PreviewView, scene: THREE.Scene): THREE.PerspectiveCamera | null {
  const box = new THREE.Box3().setFromObject(root);
  if (box.isEmpty()) return null;
  const sphere = box.getBoundingSphere(new THREE.Sphere());
  const radius = Math.max(sphere.radius, 1e-3);
  const distance = radius / Math.sin(THREE.MathUtils.degToRad(FOV_DEGREES / 2)) * FRAME_MARGIN / view.zoom;
  const direction = new THREE.Vector3(
    Math.cos(view.pitch) * Math.sin(view.yaw),
    Math.sin(view.pitch),
    Math.cos(view.pitch) * Math.cos(view.yaw),
  );
  const camera = new THREE.PerspectiveCamera(FOV_DEGREES, 1, distance / 100, distance * 10);
  camera.position.copy(sphere.center).addScaledVector(direction, distance);
  camera.lookAt(sphere.center);
  addLights(scene, sphere.center, distance);
  return camera;
}

export function createPreviewRenderer(sources: PreviewSources): PreviewRenderer {
  const prepared = new Map<string, Promise<Prepared | null>>();
  let gl: THREE.WebGLRenderer | null = null;
  let glFailed = false;

  const ensureGl = (size: number): THREE.WebGLRenderer | null => {
    if (glFailed) return null;
    if (!gl) {
      try {
        const canvas = document.createElement('canvas');
        gl = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true, preserveDrawingBuffer: true });
      } catch {
        glFailed = true;
        return null;
      }
    }
    gl.setPixelRatio(1);
    gl.setSize(size, size, false);
    gl.setClearColor(0x000000, 0);
    return gl;
  };

  const preparedFor = (modelPath: string): Promise<Prepared | null> => {
    const hit = prepared.get(modelPath);
    if (hit) {
      prepared.delete(modelPath);
      prepared.set(modelPath, hit);
      return hit;
    }
    const next = prepare(modelPath, sources);
    prepared.set(modelPath, next);
    next.then(entry => { if (!entry) prepared.delete(modelPath); }, () => { prepared.delete(modelPath); });
    while (prepared.size > PREPARED_CAPACITY) {
      const oldest = prepared.entries().next();
      if (oldest.done) break;
      prepared.delete(oldest.value[0]);
      void oldest.value[1].then(entry => entry?.dispose(), () => undefined);
    }
    return next;
  };

  return {
    async render(modelPath, view) {
      const data = await preparedFor(modelPath).catch(() => null);
      if (!data) return null;
      const renderer = ensureGl(view.size);
      if (!renderer) return null;
      const { scene, root, owned } = buildScene(data);
      try {
        const camera = frameCamera(root, view, scene);
        if (!camera) return null;
        renderer.render(scene, camera);
        return renderer.domElement.toDataURL('image/png');
      } finally {
        for (const item of owned) item.dispose();
      }
    },
  };
}
