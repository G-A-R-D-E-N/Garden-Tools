import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { billboardMatrix, vertsCentroid } from './util/billboard';

export interface NifTexRef { slot: number; path: string; }
export interface NifShapeGeo {
  name: string;
  verts: number[];    // flat x,y,z
  tris: number[];     // flat a,b,c (indices)
  normals: number[];  // flat x,y,z (may be empty)
  uvs: number[];      // flat u,v (may be empty)
  textures?: NifTexRef[];
  skinned?: boolean;
  billboard?: boolean;     // shape has a NiBillboardNode ancestor (glow sprite / lens flare)
  billboardMode?: number;  // nifly::BillboardMode; 1/8/9 = rotate-about-up, else face-camera
}
export interface NifGeo { fo4: boolean; shapes: NifShapeGeo[]; }

/**
 * Live three.js viewport for a NIF's geometry -- rotate/zoom/pan like NifSkope or Blender.
 * NIF is Z-up, so the camera's up is Z and the grid lies in the XY plane at the model's base.
 */
export default function NifViewport(
  { data, wireframe, textured, loadTexture }: {
    data: NifGeo | null;
    wireframe: boolean;
    textured: boolean;
    loadTexture?: (relPath: string) => Promise<string>;
  }
) {
  const mountRef = useRef<HTMLDivElement>(null);
  const matsRef = useRef<THREE.MeshStandardMaterial[]>([]);

  // (re)build the scene whenever the geometry (or textured toggle) changes
  useEffect(() => {
    const mount = mountRef.current;
    if (!mount || !data || data.shapes.length === 0) return;
    let cancelled = false;

    const width = mount.clientWidth || 600;
    const height = mount.clientHeight || 400;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x17181c);

    const camera = new THREE.PerspectiveCamera(50, width / height, 0.01, 1_000_000);
    camera.up.set(0, 0, 1); // NIF Z-up

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
    const mats: THREE.MeshStandardMaterial[] = [];
    const geos: THREE.BufferGeometry[] = [];
    const texes: THREE.Texture[] = [];
    const pairs: { mat: THREE.MeshStandardMaterial; shape: NifShapeGeo; hasUV: boolean }[] = [];
    // Glow/lens-flare shapes under a NiBillboardNode -- re-oriented to face the camera each frame.
    const billboards: { mesh: THREE.Mesh; centroid: THREE.Vector3; mode?: number }[] = [];

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

      const mat = new THREE.MeshStandardMaterial({
        color: 0x9aa0aa, metalness: 0.1, roughness: 0.75,
        side: THREE.DoubleSide, wireframe, flatShading: false,
      });
      const mesh = new THREE.Mesh(g, mat);
      group.add(mesh);
      if (shape.billboard) {
        mesh.matrixAutoUpdate = false; // matrix is driven per-frame by the billboard update
        mesh.frustumCulled = false;
        billboards.push({ mesh, centroid: vertsCentroid(shape.verts), mode: shape.billboardMode });
      }
      mats.push(mat);
      geos.push(g);
      pairs.push({ mat, shape, hasUV });
    }
    scene.add(group);
    matsRef.current = mats;

    // Load DDS textures (converted to PNG data URLs by the C# side) onto each shape's material.
    if (textured && loadTexture) {
      const texLoader = new THREE.TextureLoader();
      const applyUrl = (url: string): Promise<THREE.Texture | null> =>
        new Promise(res => {
          if (!url) return res(null);
          texLoader.load(url, t => res(t), undefined, () => res(null));
        });
      for (const { mat, shape, hasUV } of pairs) {
        if (!hasUV || !shape.textures?.length) continue;
        const diffuse = shape.textures.find(t => t.slot === 0);
        const normal = shape.textures.find(t => t.slot === 1);
        if (diffuse) {
          loadTexture(diffuse.path).then(applyUrl).then(tex => {
            if (cancelled || !tex) return;
            tex.flipY = false; tex.colorSpace = THREE.SRGBColorSpace;
            tex.anisotropy = 4;
            mat.map = tex; mat.color.setHex(0xffffff); mat.needsUpdate = true;
            texes.push(tex);
          });
        }
        if (normal) {
          loadTexture(normal.path).then(applyUrl).then(tex => {
            if (cancelled || !tex) return;
            tex.flipY = false; tex.colorSpace = THREE.NoColorSpace;
            mat.normalMap = tex; mat.needsUpdate = true;
            texes.push(tex);
          });
        }
      }
    }

    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());
    const radius = Math.max(size.x, size.y, size.z, 1e-3);

    // ground grid in XY plane at the model base + colored axes
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
      ro.disconnect();
      controls.dispose();
      geos.forEach(g => g.dispose());
      mats.forEach(m => m.dispose());
      texes.forEach(t => t.dispose());
      renderer.dispose();
      if (renderer.domElement.parentElement === mount) mount.removeChild(renderer.domElement);
      matsRef.current = [];
    };
  }, [data, textured, loadTexture]);

  // wireframe toggle without rebuilding the scene
  useEffect(() => {
    for (const m of matsRef.current) m.wireframe = wireframe;
  }, [wireframe]);

  return <div ref={mountRef} className="nif-viewport" />;
}
