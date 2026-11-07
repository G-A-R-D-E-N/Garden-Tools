import { useEffect, useRef, useState } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { billboardMatrix, vertsCentroid } from './util/billboard';
import { buildConnectMarkers, disposeConnectMarkers } from './util/connectMarkers';
import { makeEffectMaterial, applyVertexColors } from './util/effectMaterial';
import { placeCollisionPoints, type CollisionGeo } from './util/collisionGeometry';

export interface NifTexRef { slot: number; path: string; }
export interface NifShapeGeo {
  name: string;
  block?: number;
  verts: number[];
  tris: number[];
  normals: number[];
  uvs: number[];
  uvs2?: number[];
  textures?: NifTexRef[];
  skinned?: boolean;
  billboard?: boolean;
  billboardMode?: number;
  effectShader?: boolean;
  bgsmPath?: string;




  effect?: NifEffectParams;
  vertexColors?: number[];
}


export interface NifEffectParams {
  baseColor: [number, number, number, number];
  baseColorScale: number;
  emissiveColor: [number, number, number];
  emissiveMultiple: number;
  useFalloff: boolean;
  falloffStartAngle: number; falloffStopAngle: number;
  falloffStartOpacity: number; falloffStopOpacity: number;
  softFalloffDepth: number;
  vertexAlpha: boolean;
  alphaBlend: boolean;
  alphaSrcBlend?: number;
  alphaDstBlend?: number;
}
export interface NifGeo { fo4: boolean; shapes: NifShapeGeo[]; }
export interface NifConnectMarker { world: number[]; active: boolean; }




type ShapeMat = THREE.MeshStandardMaterial | THREE.MeshBasicMaterial;

function setWireframe(mats: ShapeMat[], on: boolean) {
  for (const m of mats) m.wireframe = on;
}

const COLLISION_COLOUR = 0x3ec7ff;

function collisionGroup(geo: CollisionGeo): THREE.Group {
  const group = new THREE.Group();
  for (const body of geo.bodies) {
    for (const shape of body.shapes) {
      if (!shape.triangles.length) continue;
      const g = new THREE.BufferGeometry();
      g.setAttribute('position', new THREE.Float32BufferAttribute(placeCollisionPoints(shape.vertices, body.transform), 3));
      g.setIndex(shape.triangles);
      const fill = new THREE.MeshBasicMaterial({
        color: COLLISION_COLOUR, transparent: true, opacity: 0.25, depthWrite: false, side: THREE.DoubleSide,
      });
      const edges = new THREE.LineBasicMaterial({ color: COLLISION_COLOUR, transparent: true, opacity: 0.9 });
      group.add(new THREE.Mesh(g, fill), new THREE.LineSegments(new THREE.WireframeGeometry(g), edges));
    }
  }
  return group;
}


export default function NifViewport(
  { data, wireframe, textured, loadTexture, loadCollision, connectPoints, highlight, onPickVertex }: {
    data: NifGeo | null;
    wireframe: boolean;
    textured: boolean;
    loadTexture?: (relPath: string) => Promise<string>;
    loadCollision?: () => Promise<CollisionGeo>;
    connectPoints?: NifConnectMarker[];
    highlight?: { block?: number; verts: readonly number[] } | null;
    onPickVertex?: (block: number, vertex: number) => void;
  }
) {
  const mountRef = useRef<HTMLDivElement>(null);
  const matsRef = useRef<ShapeMat[]>([]);
  const sceneRef = useRef<THREE.Scene | null>(null);
  const radiusRef = useRef(1);
  const pointsRef = useRef<THREE.Points | null>(null);
  const pickRef = useRef(onPickVertex);
  useEffect(() => { pickRef.current = onPickVertex; }, [onPickVertex]);
  const [showCollision, setShowCollision] = useState(false);
  const [collision, setCollision] = useState<CollisionGeo | null>(null);
  const [collisionError, setCollisionError] = useState('');


  useEffect(() => {
    const mount = mountRef.current;
    if (!mount || !data || data.shapes.length === 0) return;
    let cancelled = false;

    const width = mount.clientWidth || 600;
    const height = mount.clientHeight || 400;

    const scene = new THREE.Scene();
    sceneRef.current = scene;
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

    scene.add(new THREE.AmbientLight(0xffffff, 0.65));
    const key = new THREE.DirectionalLight(0xffffff, 1.0); key.position.set(1, 1, 2); scene.add(key);
    const fill = new THREE.DirectionalLight(0xffffff, 0.35); fill.position.set(-1.5, -1, 0.5); scene.add(fill);

    const group = new THREE.Group();
    const box = new THREE.Box3();
    const mats: ShapeMat[] = [];
    const geos: THREE.BufferGeometry[] = [];
    const texes: THREE.Texture[] = [];
    const pairs: { mat: ShapeMat; shape: NifShapeGeo; hasUV: boolean }[] = [];

    const billboards: { mesh: THREE.Mesh; centroid: THREE.Vector3; mode?: number }[] = [];
    const pickables: { mesh: THREE.Mesh; shape: NifShapeGeo }[] = [];

    for (const shape of data.shapes) {
      if (!shape.verts.length || !shape.tris.length) continue;
      const g = new THREE.BufferGeometry();
      g.setAttribute('position', new THREE.Float32BufferAttribute(shape.verts, 3));
      const hasUV = shape.uvs.length > 0;
      if (hasUV) g.setAttribute('uv', new THREE.Float32BufferAttribute(shape.uvs, 2));
      g.setIndex(shape.tris);
      if (shape.normals.length === shape.verts.length)
        g.setAttribute('normal', new THREE.Float32BufferAttribute(shape.normals, 3));
      else
        g.computeVertexNormals();
      g.computeBoundingBox();
      if (g.boundingBox) box.union(g.boundingBox);




      const mat: ShapeMat = shape.effectShader
        ? makeEffectMaterial(shape.effect, true)
        : new THREE.MeshStandardMaterial({
            color: 0x9aa0aa, metalness: 0.1, roughness: 0.75,
            side: THREE.DoubleSide, wireframe, flatShading: false,
          });
      applyVertexColors(g, mat, shape.vertexColors);
      const mesh = new THREE.Mesh(g, mat);
      group.add(mesh);
      if (shape.billboard) {
        mesh.matrixAutoUpdate = false;
        mesh.frustumCulled = false;
        billboards.push({ mesh, centroid: vertsCentroid(shape.verts), mode: shape.billboardMode });
      }
      mats.push(mat);
      geos.push(g);
      pairs.push({ mat, shape, hasUV });
      pickables.push({ mesh, shape });
    }
    scene.add(group);
    matsRef.current = mats;


    if (textured && loadTexture) {
      const texLoader = new THREE.TextureLoader();
      const applyUrl = (url: string): Promise<THREE.Texture | null> =>
        new Promise(res => {
          if (!url) return res(null);
          texLoader.load(url, t => res(t), undefined, () => res(null));
        });
      for (const { mat, shape, hasUV } of pairs) {
        if (!hasUV || !(shape.textures?.length || shape.bgsmPath)) continue;


        const diffuse = shape.textures?.find(t => t.slot === 0)
          ?? (shape.bgsmPath ? { slot: 0, path: shape.bgsmPath } : undefined);


        const normal = shape.effectShader ? undefined : shape.textures?.find(t => t.slot === 1);
        if (diffuse) {
          loadTexture(diffuse.path).then(applyUrl).then(tex => {
            if (cancelled || !tex) return;
            tex.flipY = false; tex.colorSpace = THREE.SRGBColorSpace;


            tex.wrapS = THREE.RepeatWrapping; tex.wrapT = THREE.RepeatWrapping;
            tex.anisotropy = 4;
            mat.map = tex;



            if (!shape.effectShader) mat.color.setHex(0xffffff);
            mat.needsUpdate = true;
            texes.push(tex);
          });
        }
        if (normal && mat instanceof THREE.MeshStandardMaterial) {
          loadTexture(normal.path).then(applyUrl).then(tex => {
            if (cancelled || !tex) return;
            tex.flipY = false; tex.colorSpace = THREE.NoColorSpace;
            tex.wrapS = THREE.RepeatWrapping; tex.wrapT = THREE.RepeatWrapping;
            mat.normalMap = tex; mat.needsUpdate = true;
            texes.push(tex);
          });
        }
      }
    }

    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());
    const radius = Math.max(size.x, size.y, size.z, 1e-3);
    radiusRef.current = radius;


    const grid = new THREE.GridHelper(radius * 4, 24, 0x3a5a80, 0x2a2a30);
    grid.rotation.x = Math.PI / 2;
    grid.position.set(center.x, center.y, box.min.z);
    scene.add(grid);
    const axes = new THREE.AxesHelper(radius * 1.2);
    axes.position.copy(center);
    scene.add(axes);

    controls.target.copy(center);
    camera.near = radius / 200;
    camera.far = radius * 200;
    camera.position.set(center.x + radius * 1.9, center.y - radius * 2.3, center.z + radius * 1.5);
    camera.updateProjectionMatrix();
    controls.update();

    const identity = new THREE.Matrix4();
    let raf = 0;
    const animate = () => {
      controls.update();
      for (const b of billboards) billboardMatrix(b.mesh.matrix, identity, b.centroid, camera, b.mode);
      renderer.render(scene, camera);
      raf = requestAnimationFrame(animate);
    };
    animate();

    const raycaster = new THREE.Raycaster();
    const downAt = new THREE.Vector2(-1e9, -1e9);
    const onDown = (e: PointerEvent) => downAt.set(e.clientX, e.clientY);
    const onPick = (e: MouseEvent) => {
      if (!pickRef.current) return;
      if (Math.hypot(e.clientX - downAt.x, e.clientY - downAt.y) > 4) return;
      const rect = renderer.domElement.getBoundingClientRect();
      if (!rect.width || !rect.height) return;
      raycaster.setFromCamera(new THREE.Vector2(((e.clientX - rect.left) / rect.width) * 2 - 1,
        -((e.clientY - rect.top) / rect.height) * 2 + 1), camera);
      const hits = raycaster.intersectObjects(pickables.map(p => p.mesh), false);
      if (!hits.length) return;
      const hit = hits[0];
      const target = pickables.find(p => p.mesh === hit.object);
      if (!target || target.shape.block === undefined) return;
      const verts = target.shape.verts;
      const face = hit.face;
      const corners = face ? [face.a, face.b, face.c] : [];
      let best = -1;
      let bestD = Infinity;
      for (const v of corners) {
        if (v < 0 || 3 * v + 2 >= verts.length) continue;
        const d = (verts[3 * v] - hit.point.x) ** 2 + (verts[3 * v + 1] - hit.point.y) ** 2 + (verts[3 * v + 2] - hit.point.z) ** 2;
        if (d < bestD) { bestD = d; best = v; }
      }
      if (best >= 0) pickRef.current(target.shape.block, best);
    };
    renderer.domElement.addEventListener('pointerdown', onDown);
    renderer.domElement.addEventListener('click', onPick);

    const onResize = () => {
      if (!mount) return;
      const w = mount.clientWidth || 600, h = mount.clientHeight || 400;
      camera.aspect = w / h; camera.updateProjectionMatrix(); renderer.setSize(w, h);
    };
    const ro = new ResizeObserver(onResize);
    ro.observe(mount);

    return () => {
      cancelled = true;
      cancelAnimationFrame(raf);
      renderer.domElement.removeEventListener('pointerdown', onDown);
      renderer.domElement.removeEventListener('click', onPick);
      ro.disconnect();
      controls.dispose();
      geos.forEach(g => g.dispose());
      mats.forEach(m => m.dispose());
      texes.forEach(t => t.dispose());
      renderer.dispose();
      if (renderer.domElement.parentElement === mount) mount.removeChild(renderer.domElement);
      matsRef.current = [];
      sceneRef.current = null;
    };
  }, [data, textured, loadTexture, onPickVertex]);


  useEffect(() => {
    const scene = sceneRef.current;
    if (pointsRef.current && scene) {
      scene.remove(pointsRef.current);
      pointsRef.current.geometry.dispose();
      (pointsRef.current.material as THREE.Material).dispose();
      pointsRef.current = null;
    }
    if (!scene || !data || !highlight || !highlight.verts.length || highlight.block === undefined) return;
    const shape = data.shapes.find(s => s.block === highlight.block);
    if (!shape) return;
    const pos: number[] = [];
    for (const i of highlight.verts) {
      if (i < 0 || 3 * i + 2 >= shape.verts.length) continue;
      pos.push(shape.verts[3 * i], shape.verts[3 * i + 1], shape.verts[3 * i + 2]);
    }
    if (!pos.length) return;
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    const points = new THREE.Points(g, new THREE.PointsMaterial({
      color: 0xff9800, size: radiusRef.current * 0.02, sizeAttenuation: true, depthTest: false,
    }));
    pointsRef.current = points;
    scene.add(points);
    return () => {
      scene.remove(points);
      g.dispose();
      (points.material as THREE.Material).dispose();
      if (pointsRef.current === points) pointsRef.current = null;
    };
  }, [data, highlight]);

  useEffect(() => {
    setWireframe(matsRef.current, wireframe);
  }, [wireframe]);

  useEffect(() => {
    const scene = sceneRef.current;
    if (!scene || !connectPoints?.length) return;
    const group = buildConnectMarkers(connectPoints, radiusRef.current);
    scene.add(group);
    return () => {
      scene.remove(group);
      disposeConnectMarkers(group);
    };
  }, [data, textured, loadTexture, connectPoints]);

  useEffect(() => {
    if (!showCollision || !loadCollision) return;
    let live = true;
    loadCollision().then(
      geo => { if (live) { setCollision(geo); setCollisionError(''); } },
      err => { if (live) setCollisionError(err instanceof Error ? err.message : String(err)); },
    );
    return () => { live = false; };
  }, [showCollision, loadCollision]);

  useEffect(() => {
    const scene = sceneRef.current;
    if (!scene || !showCollision || !collision) return;
    const group = collisionGroup(collision);
    scene.add(group);
    return () => {
      scene.remove(group);
      group.traverse(obj => {
        const mesh = obj as THREE.Mesh;
        mesh.geometry?.dispose();
        (mesh.material as THREE.Material | undefined)?.dispose();
      });
    };
  }, [collision, showCollision, data, textured, loadTexture]);

  return (
    <div className="nif-viewport-wrap">
      <div ref={mountRef} className="nif-viewport" />
      {loadCollision && (
        <label className="nif-viewport-collision" title="Decoded collision. Authored collision has not been tested in the game until the owner confirms it.">
          <input type="checkbox" checked={showCollision} onChange={e => setShowCollision(e.target.checked)} /> Collision
          {collisionError && <span className="nif-viewport-collision-error"> {collisionError}</span>}
        </label>
      )}
    </div>
  );
}
