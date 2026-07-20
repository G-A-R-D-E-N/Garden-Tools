import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import type { NifGeo } from './NifViewport';
import type { CellPlacedReference } from './backend';
import { billboardMatrix, vertsCentroid } from './util/billboard';

/** A geometry batch result keyed by the exact modelPath string GetPlacedReferences returned --
 * either niftool's raw geo JSON (NifGeo) or an { error } for anything that couldn't be
 * resolved/converted (not found loose or in any archive). */
export type CellGeoMap = Record<string, NifGeo | { error: string }>;

/**
 * Live three.js viewport for a whole cell's placed references -- many instances at their real
 * Position/Rotation/Scale, like Creation Kit's render window. Same NIF Z-up convention.
 *
 * Interaction (added 2026-07-20 -- the "big jumbled mess, can't click individual meshes / hide
 * layers" feedback): click a mesh to select its reference (raycast against the InstancedMeshes,
 * mapping the hit instanceId back to its CellPlacedReference); hide/show whole record types via
 * `hiddenTypes`. Selection is a wireframe box hugging the picked instance so it reads regardless of
 * the material underneath. Neither rebuilds the scene -- visibility toggles `.visible` and selection
 * just repositions one highlight box, so toggling a layer or clicking around is instant.
 *
 * Perf: refs are grouped by (model, shape) into one InstancedMesh each, not one Mesh per reference
 * (real cells run into the tens of thousands of refs). Diffuse texture fetched once per unique
 * (model, shape) and shared across its instances.
 */
export default function CellViewport(
  { references, geometry, loadTexture, hiddenTypes, selectedKey, onSelect }: {
    references: CellPlacedReference[];
    geometry: CellGeoMap;
    loadTexture?: (modelPath: string, relTexPath: string) => Promise<string>;
    hiddenTypes?: ReadonlySet<string>;
    selectedKey?: string | null;
    onSelect?: (ref: CellPlacedReference | null) => void;
  }
) {
  const mountRef = useRef<HTMLDivElement>(null);

  // Cross-effect handles so visibility/selection can update WITHOUT rebuilding the scene.
  const onSelectRef = useRef(onSelect);
  onSelectRef.current = onSelect;
  const meshEntriesRef = useRef<{ mesh: THREE.Object3D; recordType: string }[]>([]);
  const modelBBoxRef = useRef<Map<string, THREE.Box3>>(new Map());
  const highlightRef = useRef<THREE.LineSegments | null>(null);
  const refByKeyRef = useRef<Map<string, CellPlacedReference>>(new Map());

  // ---- build the scene (only when the data actually changes) ----
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

    // Pickable instanced meshes (each carries userData.refs so a raycast instanceId maps back to a
    // reference) and the per-type entries the visibility effect toggles.
    const pickables: THREE.InstancedMesh[] = [];
    const meshEntries: { mesh: THREE.Object3D; recordType: string }[] = [];
    const modelBBox = new Map<string, THREE.Box3>();
    const refByKey = new Map<string, CellPlacedReference>();
    for (const r of references) refByKey.set(r.formKey, r);

    const billboardGroups: {
      inst: THREE.InstancedMesh; refs: CellPlacedReference[]; centroid: THREE.Vector3; mode?: number;
    }[] = [];

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
      const modelBox = new THREE.Box3();
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
        g.computeBoundingBox();
        if (g.boundingBox) modelBox.union(g.boundingBox);
        disposables.push(g);

        const mat = new THREE.MeshStandardMaterial({
          color: 0x9aa0aa, metalness: 0.1, roughness: 0.8, side: THREE.DoubleSide,
        });
        disposables.push(mat);

        // Diffuse only (slot 0) for v1.
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
        inst.userData.refs = refs;
        scene.add(inst);
        pickables.push(inst);
        meshEntries.push({ mesh: inst, recordType: refs[0]?.recordType ?? '' });

        if (shape.billboard) {
          inst.frustumCulled = false;
          billboardGroups.push({ inst, refs, centroid: vertsCentroid(shape.verts), mode: shape.billboardMode });
        }
      }
      if (!modelBox.isEmpty()) modelBBox.set(modelPath, modelBox);
    }

    // Markers for refs with no renderable mesh (actors/traps, or an unresolved/failed model).
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
        inst.userData.refs = refs;
        scene.add(inst);
        pickables.push(inst);
        // Markers can mix types (actors + traps + failed statics); group them under a synthetic key so
        // "hide markers" is one toggle rather than fragmenting the type list.
        meshEntries.push({ mesh: inst, recordType: '(markers)' });
      }
    }

    // Selection highlight: a unit wireframe box we reposition onto the picked instance.
    const hlGeo = new THREE.EdgesGeometry(new THREE.BoxGeometry(1, 1, 1));
    const hlMat = new THREE.LineBasicMaterial({ color: 0x59d0ff, depthTest: false, transparent: true });
    const highlight = new THREE.LineSegments(hlGeo, hlMat);
    highlight.matrixAutoUpdate = false;
    highlight.visible = false;
    highlight.renderOrder = 999;
    scene.add(highlight);
    disposables.push(hlGeo, hlMat);

    meshEntriesRef.current = meshEntries;
    modelBBoxRef.current = modelBBox;
    refByKeyRef.current = refByKey;
    highlightRef.current = highlight;

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

    // ---- picking: pointerdown/up with a small move threshold so orbiting never selects ----
    const raycaster = new THREE.Raycaster();
    const ndc = new THREE.Vector2();
    let downX = 0, downY = 0, downT = 0;
    const onPointerDown = (e: PointerEvent) => { downX = e.clientX; downY = e.clientY; downT = e.timeStamp; };
    const onPointerUp = (e: PointerEvent) => {
      if (e.button !== 0) return;
      if (Math.hypot(e.clientX - downX, e.clientY - downY) > 5 || e.timeStamp - downT > 400) return; // a drag, not a click
      const rect = renderer.domElement.getBoundingClientRect();
      ndc.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
      ndc.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
      raycaster.setFromCamera(ndc, camera);
      const visiblePickables = pickables.filter(m => m.visible);
      const hits = raycaster.intersectObjects(visiblePickables, false);
      const hit = hits.find(h => h.instanceId != null);
      if (hit && hit.instanceId != null) {
        const refs = hit.object.userData.refs as CellPlacedReference[] | undefined;
        const ref = refs?.[hit.instanceId];
        onSelectRef.current?.(ref ?? null);
      } else {
        onSelectRef.current?.(null); // clicked empty space -> deselect
      }
    };
    renderer.domElement.addEventListener('pointerdown', onPointerDown);
    renderer.domElement.addEventListener('pointerup', onPointerUp);

    // Apply the current visibility + selection immediately (props may already be set on first build).
    applyVisibility(meshEntries, hiddenTypes);
    applySelection(highlight, modelBBox, refByKey, selectedKey ?? null);

    const bbBase = new THREE.Matrix4();
    const bbOut = new THREE.Matrix4();
    const bbQuat = new THREE.Quaternion();
    const bbPos = new THREE.Vector3();
    const bbScale = new THREE.Vector3();
    const updateBillboards = () => {
      for (const grp of billboardGroups) {
        if (!grp.inst.visible) continue;
        grp.refs.forEach((r, i) => {
          bbPos.set(r.position.x, r.position.y, r.position.z);
          bbQuat.setFromEuler(new THREE.Euler(r.rotation.x, r.rotation.y, r.rotation.z, 'XYZ'));
          bbScale.setScalar(r.scale || 1);
          bbBase.compose(bbPos, bbQuat, bbScale);
          billboardMatrix(bbOut, bbBase, grp.centroid, camera, grp.mode);
          grp.inst.setMatrixAt(i, bbOut);
        });
        grp.inst.instanceMatrix.needsUpdate = true;
      }
    };

    let raf = 0;
    const animate = () => {
      controls.update();
      if (billboardGroups.length) updateBillboards();
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
      renderer.domElement.removeEventListener('pointerdown', onPointerDown);
      renderer.domElement.removeEventListener('pointerup', onPointerUp);
      controls.dispose();
      disposables.forEach(d => d.dispose());
      textures.forEach(t => t.dispose());
      renderer.dispose();
      if (renderer.domElement.parentElement === mount) mount.removeChild(renderer.domElement);
      meshEntriesRef.current = [];
      highlightRef.current = null;
    };
  }, [references, geometry, loadTexture]); // eslint-disable-line react-hooks/exhaustive-deps

  // ---- visibility: toggle .visible per record type, no rebuild ----
  useEffect(() => {
    applyVisibility(meshEntriesRef.current, hiddenTypes);
  }, [hiddenTypes]);

  // ---- selection: reposition the highlight box, no rebuild ----
  useEffect(() => {
    if (highlightRef.current)
      applySelection(highlightRef.current, modelBBoxRef.current, refByKeyRef.current, selectedKey ?? null);
  }, [selectedKey]);

  return <div ref={mountRef} className="nif-viewport cell-viewport" />;
}

function applyVisibility(
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

/** Position the highlight box onto the selected reference (model bbox at the REFR transform), or hide it. */
function applySelection(
  highlight: THREE.LineSegments,
  modelBBox: Map<string, THREE.Box3>,
  refByKey: Map<string, CellPlacedReference>,
  selectedKey: string | null,
) {
  const ref = selectedKey ? refByKey.get(selectedKey) : undefined;
  if (!ref) { highlight.visible = false; return; }

  const box = ref.modelPath ? modelBBox.get(ref.modelPath) : undefined;
  if (box) { box.getCenter(_selCenter); box.getSize(_selSize); }
  else { _selCenter.set(0, 0, 0); _selSize.set(24, 24, 24); } // markers: a small fixed box
  // Guard against a zero-thickness axis (flat planes) so the wireframe still shows.
  _selSize.set(Math.max(_selSize.x, 1), Math.max(_selSize.y, 1), Math.max(_selSize.z, 1));

  _selPos.set(ref.position.x, ref.position.y, ref.position.z);
  _selQuat.setFromEuler(new THREE.Euler(ref.rotation.x, ref.rotation.y, ref.rotation.z, 'XYZ'));
  _selScale.setScalar(ref.scale || 1);
  _selMat.compose(_selPos, _selQuat, _selScale);                          // REFR transform
  _selLocal.makeTranslation(_selCenter.x, _selCenter.y, _selCenter.z);    // bbox center in model space
  _selLocal.scale(_selSize);                                              // unit box -> bbox size
  highlight.matrix.multiplyMatrices(_selMat, _selLocal);
  highlight.visible = true;
}
