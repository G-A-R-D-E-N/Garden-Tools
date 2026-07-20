import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import type { NifGeo } from './NifViewport';
import type { CellPlacedReference } from './backend';

/** A geometry batch result keyed by the exact modelPath string GetPlacedReferences returned --
 * either niftool's raw geo JSON (NifGeo) or an { error } for anything that couldn't be
 * resolved/converted (not found loose or in any archive, or -- currently -- a Next-Gen-format BA2
 * whose contents Mutagen's reader can't decompress; see docs/Prisma/findings). */
export type CellGeoMap = Record<string, NifGeo | { error: string }>;

/**
 * Live three.js viewport for a whole cell's placed references -- many instances at their real
 * Position/Rotation/Scale, not one mesh at the origin like NifViewport. Same NIF Z-up convention.
 *
 * Rotation: FO4 stores Rotation.X/Y/Z on a REFR as radians fed straight into the engine's
 * NiMatrix3::SetEulerAnglesXYZ (confirmed against Mesh-Rendering-Framework's GeoMath.h, which calls
 * exactly that with refr->GetAngle() -- not a guessed convention). three.js's 'XYZ' Euler order is
 * the matching axis-application order.
 *
 * Perf: refs are grouped by modelPath and rendered with one THREE.InstancedMesh per (model, shape)
 * pair instead of one Mesh per reference -- real interior cells here run into the tens of thousands
 * of refs (28k+ observed), and a plain Mesh-per-ref scene would drop to a crawl. Same grouping makes
 * texturing cheap too: one diffuse-texture fetch per unique (model, shape), applied to the shared
 * InstancedMesh material, not one fetch per instance.
 */
export default function CellViewport(
  { references, geometry, loadTexture }: {
    references: CellPlacedReference[];
    geometry: CellGeoMap;
    loadTexture?: (modelPath: string, relTexPath: string) => Promise<string>;
  }
) {
  const mountRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const mount = mountRef.current;
    if (!mount || references.length === 0) return;
    let cancelled = false;

    const width = mount.clientWidth || 600;
    const height = mount.clientHeight || 400;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x17181c);

    const camera = new THREE.PerspectiveCamera(55, width / height, 0.1, 5_000_000);
    camera.up.set(0, 0, 1); // NIF/REFR Z-up

    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(width, height);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    mount.appendChild(renderer.domElement);

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;

    scene.add(new THREE.AmbientLight(0xffffff, 0.7));
    const key = new THREE.DirectionalLight(0xffffff, 1.0); key.position.set(1, 1, 2); scene.add(key);
    const fill = new THREE.DirectionalLight(0xffffff, 0.35); fill.position.set(-1.5, -1, 0.5); scene.add(fill);

    const disposables: { dispose: () => void }[] = [];
    const posBounds = new THREE.Box3();
    const dummy = new THREE.Object3D();

    // Group refs by modelPath so each unique mesh becomes one (or a few, if multi-shape) InstancedMesh.
    const byModel = new Map<string, CellPlacedReference[]>();
    const markers: CellPlacedReference[] = [];       // no model, or model resolve/convert failed
    for (const r of references) {
      posBounds.expandByPoint(new THREE.Vector3(r.position.x, r.position.y, r.position.z));
      const geo = r.modelPath ? geometry[r.modelPath] : undefined;
      const ok = geo && 'shapes' in geo && geo.shapes.some(s => s.verts.length > 0 && s.tris.length > 0);
      if (r.modelPath && ok) {
        const list = byModel.get(r.modelPath);
        if (list) list.push(r); else byModel.set(r.modelPath, [r]);
      } else {
        markers.push(r);
      }
    }

    const texLoader = loadTexture ? new THREE.TextureLoader() : null;
    const textures: THREE.Texture[] = [];

    for (const [modelPath, refs] of byModel) {
      const nifGeo = geometry[modelPath] as NifGeo;
      for (const shape of nifGeo.shapes) {
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
        disposables.push(g);

        const mat = new THREE.MeshStandardMaterial({
          color: 0x9aa0aa, metalness: 0.1, roughness: 0.8, side: THREE.DoubleSide,
        });
        disposables.push(mat);

        // Diffuse only (slot 0) for v1 -- normal maps would double the fetch/convert cost per
        // unique mesh, and the flat-gray fallback already reads fine for anything still pending.
        if (hasUV && texLoader && loadTexture && shape.textures?.length) {
          const diffuse = shape.textures.find(t => t.slot === 0);
          if (diffuse) {
            loadTexture(modelPath, diffuse.path).then(url => {
              if (cancelled || !url) return;
              texLoader.load(url, tex => {
                if (cancelled) { tex.dispose(); return; }
                tex.flipY = false;
                tex.colorSpace = THREE.SRGBColorSpace;
                tex.anisotropy = 4;
                mat.map = tex;
                mat.color.setHex(0xffffff);
                mat.needsUpdate = true;
                textures.push(tex);
              }, undefined, () => {});
            });
          }
        }

        const inst = new THREE.InstancedMesh(g, mat, refs.length);
        refs.forEach((r, i) => {
          dummy.position.set(r.position.x, r.position.y, r.position.z);
          dummy.rotation.set(r.rotation.x, r.rotation.y, r.rotation.z, 'XYZ');
          dummy.scale.setScalar(r.scale || 1);
          dummy.updateMatrix();
          inst.setMatrixAt(i, dummy.matrix);
        });
        inst.instanceMatrix.needsUpdate = true;
        scene.add(inst);
      }
    }

    // Markers for refs with no renderable mesh (actors/traps, or an unresolved/failed model) --
    // a small sphere, colored by why, so the viewer never just silently drops a reference.
    if (markers.length > 0) {
      const markerGeo = new THREE.SphereGeometry(8, 8, 6);
      disposables.push(markerGeo);
      const noModelMat = new THREE.MeshBasicMaterial({ color: 0x5a6472, transparent: true, opacity: 0.55 });
      const failedMat = new THREE.MeshBasicMaterial({ color: 0xd08a4a, transparent: true, opacity: 0.75 });
      disposables.push(noModelMat, failedMat);
      const noModelRefs = markers.filter(r => !r.modelPath);
      const failedRefs = markers.filter(r => r.modelPath);
      for (const [mat, refs] of [[noModelMat, noModelRefs], [failedMat, failedRefs]] as const) {
        if (refs.length === 0) continue;
        const inst = new THREE.InstancedMesh(markerGeo, mat, refs.length);
        refs.forEach((r, i) => {
          dummy.position.set(r.position.x, r.position.y, r.position.z);
          dummy.rotation.set(0, 0, 0);
          dummy.scale.setScalar(1);
          dummy.updateMatrix();
          inst.setMatrixAt(i, dummy.matrix);
        });
        inst.instanceMatrix.needsUpdate = true;
        scene.add(inst);
      }
    }

    const size = posBounds.getSize(new THREE.Vector3());
    const center = posBounds.getCenter(new THREE.Vector3());
    const radius = Math.max(size.x, size.y, size.z, 50);

    const grid = new THREE.GridHelper(radius * 2.5, 24, 0x3a5a80, 0x2a2a30);
    grid.rotation.x = Math.PI / 2;
    grid.position.set(center.x, center.y, posBounds.min.z);
    scene.add(grid);
    const axes = new THREE.AxesHelper(radius * 0.5);
    axes.position.copy(center);
    scene.add(axes);

    controls.target.copy(center);
    camera.near = Math.max(radius / 500, 0.1);
    camera.far = radius * 50;
    camera.position.set(center.x + radius * 1.2, center.y - radius * 1.4, center.z + radius * 0.9);
    camera.updateProjectionMatrix();
    controls.update();

    let raf = 0;
    const animate = () => { controls.update(); renderer.render(scene, camera); raf = requestAnimationFrame(animate); };
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
      disposables.forEach(d => d.dispose());
      textures.forEach(t => t.dispose());
      renderer.dispose();
      if (renderer.domElement.parentElement === mount) mount.removeChild(renderer.domElement);
      void cancelled;
    };
  }, [references, geometry, loadTexture]);

  return <div ref={mountRef} className="nif-viewport cell-viewport" />;
}
