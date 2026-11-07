import * as THREE from 'three';
import { getCell } from '../backend';
import type { NifGeo } from '../NifViewport';

type GeoEntry = NifGeo | { error: string };
type Disposable = { dispose: () => void };

const GHOST_BOX_SIZE = 48;

export type GhostGeometryFetch = (modelPath: string) => Promise<NifGeo | null>;

export async function fetchGhostGeometry(modelPath: string): Promise<NifGeo | null> {
  const cell = getCell();
  if (!cell) return null;
  const reply = JSON.parse(await cell.GetGeometryBatch(JSON.stringify([modelPath]))) as { geometry?: Record<string, GeoEntry> };
  const entry = reply.geometry?.[modelPath];
  return entry && 'shapes' in entry ? entry : null;
}

function ghostMaterial(): THREE.MeshStandardMaterial {
  return new THREE.MeshStandardMaterial({
    color: 0x9aa0aa, metalness: 0.1, roughness: 0.8, transparent: true, opacity: 0.4,
    depthWrite: false, side: THREE.DoubleSide,
  });
}

function buildTemplate(geo: NifGeo, owned: Disposable[]): THREE.Object3D | null {
  const group = new THREE.Group();
  const mat = ghostMaterial();
  owned.push(mat);
  for (const shape of geo.shapes) {
    if (!shape.verts.length || !shape.tris.length) continue;
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(shape.verts, 3));
    g.setIndex(shape.tris);
    g.computeVertexNormals();
    owned.push(g);
    group.add(new THREE.Mesh(g, mat));
  }
  return group.children.length > 0 ? group : null;
}

export class GhostController {
  private readonly scene: THREE.Scene;
  private readonly fetchGeometry: GhostGeometryFetch;
  private readonly group = new THREE.Group();
  private readonly cache = new Map<string, THREE.Object3D | null>();
  private readonly owned: Disposable[] = [];
  private readonly box: THREE.Object3D;
  private chain: Promise<void> = Promise.resolve();
  private token = 0;
  private disposed = false;

  constructor(scene: THREE.Scene, fetchGeometry: GhostGeometryFetch) {
    this.scene = scene;
    this.fetchGeometry = fetchGeometry;
    const geo = new THREE.BoxGeometry(GHOST_BOX_SIZE, GHOST_BOX_SIZE, GHOST_BOX_SIZE);
    const mat = ghostMaterial();
    this.owned.push(geo, mat);
    this.box = new THREE.Mesh(geo, mat);
    this.box.position.set(0, 0, GHOST_BOX_SIZE / 2);
    this.group.visible = false;
    scene.add(this.group);
  }

  get visible(): boolean {
    return this.group.visible;
  }

  show(modelPath: string, position: THREE.Vector3): void {
    this.group.position.copy(position);
    this.group.visible = true;
    const token = ++this.token;
    this.setContent(this.box);
    if (!modelPath) return;
    void this.resolve(modelPath).then(template => {
      if (template && token === this.token) this.setContent(template);
    });
  }

  moveTo(position: THREE.Vector3): void {
    this.group.position.copy(position);
  }

  hide(): void {
    this.token++;
    this.group.visible = false;
  }

  dispose(): void {
    this.hide();
    this.disposed = true;
    this.group.clear();
    this.scene.remove(this.group);
    for (const d of this.owned) d.dispose();
    this.owned.length = 0;
    this.cache.clear();
  }

  private setContent(template: THREE.Object3D): void {
    this.group.clear();
    this.group.add(template.clone());
  }

  private resolve(modelPath: string): Promise<THREE.Object3D | null> {
    if (this.cache.has(modelPath)) return Promise.resolve(this.cache.get(modelPath) ?? null);
    const next = this.chain.then(() => this.build(modelPath));
    this.chain = next.then(() => undefined);
    return next;
  }

  private async build(modelPath: string): Promise<THREE.Object3D | null> {
    if (this.cache.has(modelPath)) return this.cache.get(modelPath) ?? null;
    let template: THREE.Object3D | null = null;
    try {
      const geo = await this.fetchGeometry(modelPath);
      if (!this.disposed && geo) template = buildTemplate(geo, this.owned);
    } catch {
      template = null;
    }
    if (!this.disposed) this.cache.set(modelPath, template);
    return template;
  }
}
