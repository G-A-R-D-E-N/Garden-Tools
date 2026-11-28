import { useEffect, useLayoutEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';
import type { NifGeo } from './NifViewport';
import type { CellLightingData, CellPlacedReference } from './backend';
import { cellLightBase, fo4LightToThree, type LightingSettings, type PointLightPlan, type ToggleableLayer } from './cellLighting';
import { bindCellLightOrbit, buildCellLightRig, buildLightMarkers, configureCellRenderer, refreshCellLights, setCellExposure, setLightWire, type CellLightRig } from './cellLightRig';
import { isMeshPickable, makeCellEffectMaterial } from './cellEffectMaterial';
import { buildPlaceholderGroups, type PlaceholderItem } from './cellPlaceholders';
import { placeholderKindOf } from './cellPlaceholderKind';
import { shapeMapPaths, shapeMaterialPlan, shapeTextureTiling, TILE_BOTH, type TextureTiling } from './cellMaterials';
import { createTextureCache, CELL_TEXTURE_CACHE_MAX, CELL_TEXTURE_CACHE_MAX_BYTES, CELL_TEXTURE_CONCURRENCY, glossToRoughnessInPlace, reconstructNormalZ, type TextureCache } from './cellTextureCache';
import { loadPlainTexture, loadTransformedTexture, parseTextureKey, textureBytes, textureKey, type TextureKind } from './cellTextureLoad';
import { countMaterialNotFound, countMissing, countNoPath, createTextureReport, snapshotTextureReport, type CellTextureReport } from './cellTextureReport';
import { billboardMatrix, shapeWorldMatrix, vertsCentroid } from './util/billboard';
import { cellEditKey } from './util/cellEditKeys';
import { applyCellVisibility, cellLayerKindOf, cellLayerOf, hasRenderableShapes, toggleLayerOf } from './util/cellLayer';
import { applyVertexColors } from './util/effectMaterial';
import { applyHidden, applySelection, refRotation, refScale, type InstanceRef } from './util/cellInstances';
import { GhostController, fetchGhostGeometry } from './util/ghostMesh';
import type { BaseObjectItem } from './assetBrowser/baseObjects';
import type { BaseObjectDragPayload } from './assetBrowser/dragPayload';
import { attachDropTarget } from './cellDropTarget';


export type CellGeoMap = Record<string, NifGeo | { error: string }>;

export type CellPlacementApi = {
  pointAt: (clientX: number, clientY: number) => { x: number; y: number; z: number } | null;
  viewCentre: () => { x: number; y: number; z: number };
};


export type CellTextureStats = {
  ok: number;
  noPath: number;
  resolveFail: number;
  decodeFail: number;
  firstFailure: string | null;
  report: CellTextureReport;
};

type TexOutcome = 'ok' | 'resolveFail' | 'decodeFail';
type TexSink = {
  loadTexture?: (modelPath: string, relTexPath: string) => Promise<string>;
  onOutcome: (outcome: TexOutcome, relTexPath: string, modelPath: string) => void;
};
type TexHost = {
  cache: TextureCache<THREE.Texture>;
  models: Map<string, string>;
  sink: TexSink | null;
};

function createTexHost(): TexHost {
  const host: TexHost = {
    models: new Map(),
    sink: null,
    cache: createTextureCache<THREE.Texture>({
      maxCount: CELL_TEXTURE_CACHE_MAX,
      maxBytes: CELL_TEXTURE_CACHE_MAX_BYTES,
      concurrency: CELL_TEXTURE_CONCURRENCY,
      sizeOf: textureBytes,
      load: async key => {
        const { kind, tiling, rel } = parseTextureKey(key);
        const model = host.models.get(key) ?? '';
        const sink = host.sink;
        const fetchUrl = sink?.loadTexture;
        if (!sink || !fetchUrl) return null;
        const url = await fetchUrl(model, rel);
        if (!url) {
          sink.onOutcome('resolveFail', rel, model);
          return null;
        }
        try {
          const tex = kind === 'normal'
            ? await loadTransformedTexture(url, reconstructNormalZ, THREE.NoColorSpace, tiling)
            : kind === 'gloss'
              ? await loadTransformedTexture(url, glossToRoughnessInPlace, THREE.NoColorSpace, tiling)
              : await loadPlainTexture(url, THREE.SRGBColorSpace, tiling);
          sink.onOutcome('ok', rel, model);
          return tex;
        } catch {
          sink.onOutcome('decodeFail', rel, model);
          return null;
        }
      },
    }),
  };
  return host;
}

function createRenderer(): THREE.WebGLRenderer | null {
  try {
    return new THREE.WebGLRenderer({ antialias: true });
  } catch {
    return null;
  }
}

export default function CellViewport(
  { references, geometry, loadTexture, hiddenTypes, hiddenRefs, selectedKey, onSelect,
    onHideSelected, onUnhideAll, onToggleMarkers, onDeleteSelected, onDuplicateSelected,
    onMoveEnd, onTextureStats, lighting, lightSettings, onReferenceDoubleClick, placementApiRef, onPlaceDrop, onDropLog, previewItem }: {
    references: CellPlacedReference[];
    lighting?: CellLightingData | null;
    lightSettings: LightingSettings;
    geometry: CellGeoMap;
    loadTexture?: (modelPath: string, relTexPath: string) => Promise<string>;
    onTextureStats?: (stats: CellTextureStats) => void;
    hiddenTypes?: ReadonlySet<string>;
    hiddenRefs?: ReadonlySet<string>;
    selectedKey?: string | null;
    onSelect?: (ref: CellPlacedReference | null) => void;
    onHideSelected?: () => void;
    onUnhideAll?: () => void;
    onToggleMarkers?: () => void;
    onDeleteSelected?: () => void;
    onDuplicateSelected?: () => void;
    onMoveEnd?: (ref: CellPlacedReference) => void;
    onReferenceDoubleClick?: (ref: CellPlacedReference) => void;
    placementApiRef?: { current: CellPlacementApi | null };
    onPlaceDrop?: (payload: BaseObjectDragPayload, point: { x: number; y: number; z: number }) => void;
    onDropLog?: (line: string) => void;
    previewItem?: BaseObjectItem | null;
  }
) {
  const mountRef = useRef<HTMLDivElement>(null);
  const texHostRef = useRef<TexHost | null>(null);
  useEffect(() => () => {
    texHostRef.current?.cache.dispose();
    texHostRef.current = null;
  }, []);

  const onSelectRef = useRef(onSelect);
  const onHideSelectedRef = useRef(onHideSelected);
  const onUnhideAllRef = useRef(onUnhideAll);
  const onToggleMarkersRef = useRef(onToggleMarkers);
  const onDeleteSelectedRef = useRef(onDeleteSelected);
  const onDuplicateSelectedRef = useRef(onDuplicateSelected);
  const onMoveEndRef = useRef(onMoveEnd);
  const onPlaceDropRef = useRef(onPlaceDrop);
  const onDropLogRef = useRef(onDropLog);
  const selectedKeyRef = useRef(selectedKey);
  const hiddenRefsRef = useRef(hiddenRefs);
  const onReferenceDoubleClickRef = useRef(onReferenceDoubleClick);
  const lightSettingsRef = useRef(lightSettings);
  const lightRigRef = useRef<CellLightRig | null>(null);
  const rendererRef = useRef<THREE.WebGLRenderer | null>(null);
  useLayoutEffect(() => {
    lightSettingsRef.current = lightSettings;
    onReferenceDoubleClickRef.current = onReferenceDoubleClick;
    onSelectRef.current = onSelect;
    onHideSelectedRef.current = onHideSelected;
    onUnhideAllRef.current = onUnhideAll;
    onToggleMarkersRef.current = onToggleMarkers;
    onDeleteSelectedRef.current = onDeleteSelected;
    onDuplicateSelectedRef.current = onDuplicateSelected;
    onMoveEndRef.current = onMoveEnd;
    onPlaceDropRef.current = onPlaceDrop;
    onDropLogRef.current = onDropLog;
    selectedKeyRef.current = selectedKey;
    hiddenRefsRef.current = hiddenRefs;
  });
  const meshEntriesRef = useRef<{ mesh: THREE.Object3D; recordType: string; layer?: ToggleableLayer }[]>([]);
  const modelBBoxRef = useRef<Map<string, THREE.Box3>>(new Map());
  const scolBBoxRef = useRef<Map<string, THREE.Box3>>(new Map());
  const highlightRef = useRef<THREE.LineSegments | null>(null);
  const refByKeyRef = useRef<Map<string, CellPlacedReference>>(new Map());
  const keyToInstancesRef = useRef<Map<string, InstanceRef[]>>(new Map());
  const cameraApiRef = useRef<{
    focus: (key: string | null) => void; topDown: (key: string | null) => void;
    setOrbitTarget: (key: string | null) => void;
    pointAt: (clientX: number, clientY: number) => { x: number; y: number; z: number } | null;
    viewCentre: () => { x: number; y: number; z: number };
  } | null>(null);



  const gizmoAttachRef = useRef<((key: string | null) => void) | null>(null);
  const ghostRef = useRef<GhostController | null>(null);


  const worldCenterRef = useRef(new THREE.Vector3());
  const savedViewRef = useRef<{ position: THREE.Vector3; target: THREE.Vector3; near: number; far: number } | null>(null);
  const gizmoModeRef = useRef<'translate' | 'rotate'>('translate');


  useEffect(() => {
    const mount = mountRef.current;
    if (!mount) { savedViewRef.current = null; return; }
    let cancelled = false;

    const width = mount.clientWidth || 600;
    const height = mount.clientHeight || 400;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x17181c);

    const camera = new THREE.PerspectiveCamera(55, width / height, 0.1, 5_000_000);
    camera.up.set(0, 0, 1);

    const renderer = createRenderer();
    if (!renderer) { savedViewRef.current = null; return; }
    renderer.setSize(width, height);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    configureCellRenderer(renderer, lightSettingsRef.current);
    rendererRef.current = renderer;
    mount.appendChild(renderer.domElement);

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;


    const disposables: { dispose: () => void }[] = [];
    const posBounds = new THREE.Box3();
    const dummy = new THREE.Object3D();



    const pickables: THREE.InstancedMesh[] = [];
    const meshEntries: { mesh: THREE.Object3D; recordType: string; layer?: ToggleableLayer }[] = [];
    const modelBBox = new Map<string, THREE.Box3>();
    const refByKey = new Map<string, CellPlacedReference>();
    const keyToInstances = new Map<string, InstanceRef[]>();
    for (const r of references) refByKey.set(r.formKey, r);
    const addInstance = (
      inst: THREE.InstancedMesh, id: number, r: CellPlacedReference, billboard: boolean, local?: THREE.Matrix4,
    ) => {
      const entry: InstanceRef = { mesh: inst, id, ref: r, billboard, local };
      const arr = keyToInstances.get(r.formKey);
      if (arr) arr.push(entry); else keyToInstances.set(r.formKey, [entry]);
    };

    const billboardGroups: {
      inst: THREE.InstancedMesh; refs: CellPlacedReference[]; centroid: THREE.Vector3; local?: THREE.Matrix4;
      mode?: number;
    }[] = [];
    const boundsCorner = new THREE.Vector3();


    const byModel = new Map<string, { path: string; recordType: string; layer?: ToggleableLayer; refs: CellPlacedReference[] }>();
    const placeholders: PlaceholderItem[] = [];


    const byDecal = new Map<string, CellPlacedReference[]>();




    type ScolPlacement = { x: number; y: number; z: number; rx: number; ry: number; rz: number; scale: number };
    const byScolModel = new Map<string, { parent: CellPlacedReference; placement: ScolPlacement }[]>();
    const goodGeo = (path: string | null | undefined) => hasRenderableShapes(path ? geometry[path] : undefined);
    for (const r of references) {
      posBounds.expandByPoint(new THREE.Vector3(r.position.x, r.position.y, r.position.z));
      if (r.decalDiffuse) {
        const list = byDecal.get(r.decalDiffuse);
        if (list) list.push(r); else byDecal.set(r.decalDiffuse, [r]);
        continue;
      }
      const kind = cellLayerKindOf(r);
      const path = r.modelPath;
      const resolved = !!path && goodGeo(path);
      const placeholderKind = placeholderKindOf(r, kind, resolved);
      if (placeholderKind) {
        placeholders.push({ ref: r, kind: placeholderKind, recordType: cellLayerOf(r, path ? geometry[path] : undefined) });
        continue;
      }
      if (path && resolved) {
        const layer = toggleLayerOf(kind);
        const recordType = cellLayerOf(r, geometry[path]);
        const key = `${layer ?? ''}|${recordType}|${path}`;
        const group = byModel.get(key);
        if (group) group.refs.push(r); else byModel.set(key, { path, recordType, layer, refs: [r] });
        continue;
      }
      let anyPartOk = false;
      for (const part of r.scolParts ?? []) {
        if (!goodGeo(part.modelPath)) continue;
        anyPartOk = true;
        const list = byScolModel.get(part.modelPath);
        const entries = part.placements.map(placement => ({ parent: r, placement }));
        if (list) list.push(...entries); else byScolModel.set(part.modelPath, entries);
      }
      if (!anyPartOk && !(kind === 'light' && r.light)) {
        placeholders.push({
          ref: r,
          kind: kind === 'effect' ? 'effect' : 'unresolved',
          recordType: cellLayerOf(r, r.modelPath ? geometry[r.modelPath] : undefined),
        });
      }
    }













    const worldCenter = posBounds.getCenter(new THREE.Vector3());
    worldCenterRef.current.copy(worldCenter);

    const sceneLights: PointLightPlan[] = [];
    for (const r of references) {
      if (!r.light) continue;
      const plan = fo4LightToThree(r.light, [
        r.position.x - worldCenter.x, r.position.y - worldCenter.y, r.position.z - worldCenter.z,
      ]);
      if (plan?.visible) sceneLights.push(plan);
    }

    const held: string[] = [];
    const host = texHostRef.current ??= createTexHost();
    const texStats: CellTextureStats = { ok: 0, noPath: 0, resolveFail: 0, decodeFail: 0, firstFailure: null, report: createTextureReport() };
    let texStatsTimer: ReturnType<typeof setTimeout> | undefined;


    const reportTexStats = () => {
      if (!onTextureStats || texStatsTimer) return;
      texStatsTimer = setTimeout(() => {
        texStatsTimer = undefined;
        if (!cancelled) onTextureStats({ ...texStats, report: snapshotTextureReport(texStats.report) });
      }, 300);
    };
    host.sink = {
      loadTexture,
      onOutcome: (outcome, relTexPath, modelPath) => {
        if (outcome === 'ok') texStats.ok++;
        else if (outcome === 'resolveFail') texStats.resolveFail++;
        else texStats.decodeFail++;
        if (outcome === 'resolveFail') countMissing(texStats.report, relTexPath);
        if (outcome !== 'ok') texStats.firstFailure ??= modelPath ? `${relTexPath} (nif: ${modelPath})` : relTexPath;
        reportTexStats();
      },
    };
    const requestMap = (
      target: THREE.MeshStandardMaterial,
      slot: 'map' | 'normalMap' | 'roughnessMap' | 'emissiveMap',
      modelPath: string,
      rel: string,
      kind: TextureKind,
      tiling: TextureTiling,
    ) => {
      const key = textureKey(kind, tiling, rel);
      if (!host.models.has(key)) host.models.set(key, modelPath);
      held.push(key);
      host.cache.acquire(key).then(tex => {
        if (cancelled || !tex) return;
        target[slot] = tex;
        if (slot === 'map') target.color.setHex(0xffffff);
        target.needsUpdate = true;
      });
    };

    const buildShapeMesh = (shape: NifGeo['shapes'][number], modelPath: string) => {
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
      disposables.push(g);





      const maps = shapeMapPaths(shape);
      const tiling = shapeTextureTiling(shape);
      const plan = shapeMaterialPlan(shape, hasUV && !!maps.smoothSpec);
      const mat = shape.effectShader
        ? makeCellEffectMaterial(shape.effect)
        : new THREE.MeshStandardMaterial({
          color: 0x9aa0aa,
          metalness: plan.metalness,
          roughness: plan.roughness,
          side: plan.side === 'double' ? THREE.DoubleSide : THREE.FrontSide,
          alphaTest: plan.alphaTest ?? 0,
          transparent: plan.transparent,
          opacity: plan.opacity,
          emissive: plan.emissive ? new THREE.Color().setRGB(...plan.emissive, THREE.SRGBColorSpace) : 0x000000,
          emissiveIntensity: plan.emissiveIntensity,
        });
      applyVertexColors(g, mat, shape.vertexColors);
      disposables.push(mat);




      if (loadTexture && shape.bgsmPath && !shape.material) {
        countMaterialNotFound(texStats.report);
        reportTexStats();
      }
      if (loadTexture && !maps.diffuse) {
        texStats.noPath++;
        countNoPath(texStats.report, shape.shaderType ?? '(unknown)');
        reportTexStats();
      }
      if (hasUV && loadTexture) {
        if (maps.diffuse) requestMap(mat as THREE.MeshStandardMaterial, 'map', modelPath, maps.diffuse, 'plain', tiling);
        if (maps.normal && !shape.effectShader) requestMap(mat as THREE.MeshStandardMaterial, 'normalMap', modelPath, maps.normal, 'normal', tiling);
        if (maps.smoothSpec && !shape.effectShader) requestMap(mat as THREE.MeshStandardMaterial, 'roughnessMap', modelPath, maps.smoothSpec, 'gloss', tiling);
        if (maps.glow && !shape.effectShader) requestMap(mat as THREE.MeshStandardMaterial, 'emissiveMap', modelPath, maps.glow, 'plain', tiling);
      }
      return { geometry: g, material: mat };
    };

    for (const { path: modelPath, recordType, layer, refs } of byModel.values()) {
      const nifGeo = geometry[modelPath] as NifGeo;
      const modelBox = new THREE.Box3();
      for (const shape of nifGeo.shapes) {
        if (!shape.verts.length || !shape.tris.length) continue;
        const { geometry: g, material: mat } = buildShapeMesh(shape, modelPath);
        if (shape.billboard && shape.world?.length === 16) {
          const wm = shapeWorldMatrix(shape.world);
          for (let i = 0; i < shape.verts.length; i += 3) {
            boundsCorner.set(shape.verts[i], shape.verts[i + 1], shape.verts[i + 2]).applyMatrix4(wm);
            modelBox.expandByPoint(boundsCorner);
          }
        } else if (g.boundingBox) {
          modelBox.union(g.boundingBox);
        }

        const inst = new THREE.InstancedMesh(g, mat, refs.length);
        refs.forEach((r, i) => {
          dummy.position.set(r.position.x - worldCenter.x, r.position.y - worldCenter.y, r.position.z - worldCenter.z);
          refRotation(r.rotation.x, r.rotation.y, r.rotation.z, dummy.rotation);
          dummy.scale.setScalar(refScale(r.scale));
          dummy.updateMatrix();
          inst.setMatrixAt(i, dummy.matrix);
        });
        inst.instanceMatrix.needsUpdate = true;
        inst.userData.refs = refs;
        scene.add(inst);
        pickables.push(inst);
        meshEntries.push({ mesh: inst, recordType, layer });
        const shapeWorld = shape.world?.length === 16 ? shapeWorldMatrix(shape.world) : undefined;
        refs.forEach((r, i) => addInstance(inst, i, r, !!shape.billboard, shapeWorld));

        if (shape.billboard) {
          inst.frustumCulled = false;
          billboardGroups.push({
            inst, refs, centroid: vertsCentroid(shape.verts), local: shapeWorld, mode: shape.billboardMode,
          });
        }
      }
      if (!modelBox.isEmpty()) modelBBox.set(modelPath, modelBox);
    }






    const scolBBox = new Map<string, THREE.Box3>();
    const _scolRefPos = new THREE.Vector3();
    const _scolRefQuat = new THREE.Quaternion();
    const _scolRefScale = new THREE.Vector3();
    const _scolRefM = new THREE.Matrix4();
    const _scolLocPos = new THREE.Vector3();
    const _scolLocQuat = new THREE.Quaternion();
    const _scolLocScale = new THREE.Vector3();
    for (const [modelPath, entries] of byScolModel) {
      const nifGeo = geometry[modelPath] as NifGeo;
      for (const shape of nifGeo.shapes) {
        if (!shape.verts.length || !shape.tris.length) continue;
        const { geometry: g, material: mat } = buildShapeMesh(shape, modelPath);

        const inst = new THREE.InstancedMesh(g, mat, entries.length);
        const locals: THREE.Matrix4[] = [];
        entries.forEach(({ parent, placement }, i) => {
          _scolRefPos.set(parent.position.x - worldCenter.x, parent.position.y - worldCenter.y, parent.position.z - worldCenter.z);
          _scolRefQuat.setFromEuler(refRotation(parent.rotation.x, parent.rotation.y, parent.rotation.z));
          _scolRefScale.setScalar(refScale(parent.scale));
          _scolRefM.compose(_scolRefPos, _scolRefQuat, _scolRefScale);

          _scolLocPos.set(placement.x, placement.y, placement.z);
          _scolLocQuat.setFromEuler(new THREE.Euler(placement.rx, placement.ry, placement.rz, 'XYZ'));
          _scolLocScale.setScalar(placement.scale || 1);
          const local = new THREE.Matrix4().compose(_scolLocPos, _scolLocQuat, _scolLocScale);
          locals.push(local);

          dummy.matrix.multiplyMatrices(_scolRefM, local);
          inst.setMatrixAt(i, dummy.matrix);




          if (g.boundingBox) {
            const box = g.boundingBox.clone().applyMatrix4(local);
            const existing = scolBBox.get(parent.formKey);
            if (existing) existing.union(box); else scolBBox.set(parent.formKey, box);
          }
        });
        inst.instanceMatrix.needsUpdate = true;
        inst.userData.refs = entries.map(e => e.parent);
        scene.add(inst);
        pickables.push(inst);
        meshEntries.push({ mesh: inst, recordType: cellLayerOf(entries[0].parent, nifGeo) });
        entries.forEach(({ parent }, i) => addInstance(inst, i, parent, false, locals[i]));
      }
    }











    const decalGeo = new THREE.PlaneGeometry(1, 1);
    disposables.push(decalGeo);
    for (const [texPath, refs] of byDecal) {
      const mat = new THREE.MeshBasicMaterial({
        color: 0xffffff, side: THREE.DoubleSide, transparent: true, depthWrite: false, visible: false,
      });
      disposables.push(mat);

      if (loadTexture) {
        const key = textureKey('plain', TILE_BOTH, texPath);
        if (!host.models.has(key)) host.models.set(key, '');
        held.push(key);
        host.cache.acquire(key).then(tex => {
          if (cancelled || !tex) return;
          mat.map = tex;
          mat.visible = true;
          mat.needsUpdate = true;
        });
      }

      const inst = new THREE.InstancedMesh(decalGeo, mat, refs.length);
      refs.forEach((r, i) => {
        dummy.position.set(r.position.x - worldCenter.x, r.position.y - worldCenter.y, r.position.z - worldCenter.z);
        refRotation(r.rotation.x, r.rotation.y, r.rotation.z, dummy.rotation);
        const w = (r.decalWidth || 32) * refScale(r.scale);
        const h = (r.decalHeight || 32) * refScale(r.scale);
        dummy.scale.set(w, h, 1);
        dummy.updateMatrix();
        inst.setMatrixAt(i, dummy.matrix);
      });
      inst.instanceMatrix.needsUpdate = true;
      inst.userData.refs = refs;
      scene.add(inst);
      pickables.push(inst);
      meshEntries.push({ mesh: inst, recordType: cellLayerOf(refs[0], undefined) });
      refs.forEach((r, i) => addInstance(inst, i, r, false));
    }


    const placeholderBuild = buildPlaceholderGroups(placeholders, worldCenter, geometry);
    disposables.push(...placeholderBuild.disposables);
    for (const group of placeholderBuild.groups) {
      scene.add(group.mesh);
      if (group.pickable) pickables.push(group.mesh);
      meshEntries.push({ mesh: group.mesh, recordType: group.recordType, layer: group.layer });
      group.refs.forEach((r, i) => addInstance(group.mesh, i, r, false, group.locals[i]));
    }


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
    scolBBoxRef.current = scolBBox;
    refByKeyRef.current = refByKey;
    highlightRef.current = highlight;
    keyToInstancesRef.current = keyToInstances;

    const size = posBounds.getSize(new THREE.Vector3());
    const radius = Math.max(size.x, size.y, size.z, 50);

    const lightRefs = references.filter(r => !!r.light);
    const lightMarkers = buildLightMarkers(lightRefs, worldCenter);
    if (lightMarkers) {
      lightMarkers.userData.refs = lightRefs;
      pickables.push(lightMarkers);
      lightRefs.forEach((r, i) => addInstance(lightMarkers, i, r, false));
    }
    const lightRig = buildCellLightRig(scene, cellLightBase(lighting, sceneLights, radius), worldCenter, lightMarkers);
    lightRigRef.current = lightRig;
    disposables.push(lightRig);


    const groundZ = posBounds.isEmpty() ? 0 : posBounds.min.z - worldCenter.z;
    const grid = new THREE.GridHelper(radius * 2.5, 24, 0x3a5a80, 0x2a2a30);
    grid.rotation.x = Math.PI / 2;
    grid.position.set(0, 0, groundZ);
    scene.add(grid);
    const axes = new THREE.AxesHelper(radius * 0.5);
    scene.add(axes);

    controls.target.set(0, 0, 0);
    camera.near = Math.max(radius / 500, 0.1);
    camera.far = radius * 50;
    camera.position.set(radius * 1.2, -radius * 1.4, radius * 0.9);
    camera.updateProjectionMatrix();
    controls.update();






    const transformControls = new TransformControls(camera, renderer.domElement);
    transformControls.size = 0.9;
    transformControls.setMode(gizmoModeRef.current);
    scene.add(transformControls.getHelper());
    const gizmoAnchor = new THREE.Object3D();
    scene.add(gizmoAnchor);







    const _giCurM = new THREE.Matrix4();
    const _giCurPos = new THREE.Vector3();
    const _giCurQuat = new THREE.Quaternion();
    const _giCurScale = new THREE.Vector3();
    const _giNewPos = new THREE.Vector3();
    const _giNewQuat = new THREE.Quaternion();
    const _giRefScale = new THREE.Vector3();
    const _giNewM = new THREE.Matrix4();
    const applyGizmoTransformToMesh = (formKey: string) => {
      const entries = keyToInstances.get(formKey);
      const r = refByKey.get(formKey);
      if (!entries || !r) return;
      _giNewPos.set(r.position.x - worldCenter.x, r.position.y - worldCenter.y, r.position.z - worldCenter.z);
      _giNewQuat.setFromEuler(refRotation(r.rotation.x, r.rotation.y, r.rotation.z));
      const dirty = new Set<THREE.InstancedMesh>();
      for (const e of entries) {
        if (e.billboard) continue;
        if (e.local) {



          _giRefScale.setScalar(refScale(r.scale));
          _giNewM.compose(_giNewPos, _giNewQuat, _giRefScale);
          _giNewM.multiply(e.local);
        } else {
          e.mesh.getMatrixAt(e.id, _giCurM);
          _giCurM.decompose(_giCurPos, _giCurQuat, _giCurScale);
          _giNewM.compose(_giNewPos, _giNewQuat, _giCurScale);
        }
        e.mesh.setMatrixAt(e.id, _giNewM);
        dirty.add(e.mesh);
      }
      for (const m of dirty) m.instanceMatrix.needsUpdate = true;
      applySelection(highlight, modelBBox, refByKey, formKey, worldCenter, scolBBox);
    };



    transformControls.addEventListener('objectChange', () => {
      const key = selectedKeyRef.current;
      const r = key ? refByKey.get(key) : undefined;
      if (!key || !r) return;
      r.position.x = gizmoAnchor.position.x + worldCenter.x;
      r.position.y = gizmoAnchor.position.y + worldCenter.y;
      r.position.z = gizmoAnchor.position.z + worldCenter.z;
      const e = new THREE.Euler().setFromQuaternion(gizmoAnchor.quaternion, 'XYZ');
      r.rotation.x = e.x; r.rotation.y = e.y; r.rotation.z = e.z;
      applyGizmoTransformToMesh(key);
    });

    const poseOf = (r: CellPlacedReference) =>
      [r.position.x, r.position.y, r.position.z, r.rotation.x, r.rotation.y, r.rotation.z].join(',');
    let poseAtDown = '';
    transformControls.addEventListener('mouseDown', () => {
      controls.enabled = false;
      const key = selectedKeyRef.current;
      const r = key ? refByKey.get(key) : undefined;
      poseAtDown = r ? poseOf(r) : '';
    });
    transformControls.addEventListener('mouseUp', () => {
      controls.enabled = true;
      const key = selectedKeyRef.current;
      const r = key ? refByKey.get(key) : undefined;
      if (r && poseOf(r) !== poseAtDown) onMoveEndRef.current?.(r);
    });

    const attachGizmoTo = (key: string | null) => {
      const r = key ? refByKey.get(key) : undefined;
      if (!r) { transformControls.detach(); return; }
      gizmoAnchor.position.set(r.position.x - worldCenter.x, r.position.y - worldCenter.y, r.position.z - worldCenter.z);
      gizmoAnchor.quaternion.setFromEuler(refRotation(r.rotation.x, r.rotation.y, r.rotation.z));
      transformControls.attach(gizmoAnchor);
    };
    gizmoAttachRef.current = attachGizmoTo;


    const raycaster = new THREE.Raycaster();
    const ndc = new THREE.Vector2();
    let downX = 0, downY = 0, downT = 0;
    const onPointerDown = (e: PointerEvent) => { downX = e.clientX; downY = e.clientY; downT = e.timeStamp; };
    const pickRefAt = (clientX: number, clientY: number): CellPlacedReference | null => {
      const rect = renderer.domElement.getBoundingClientRect();
      ndc.x = ((clientX - rect.left) / rect.width) * 2 - 1;
      ndc.y = -((clientY - rect.top) / rect.height) * 2 + 1;
      raycaster.setFromCamera(ndc, camera);
      const visiblePickables = pickables.filter(isMeshPickable);
      const hits = raycaster.intersectObjects(visiblePickables, false);
      const hit = hits.find(h => h.instanceId != null);
      if (!hit || hit.instanceId == null) return null;
      const refs = hit.object.userData.refs as CellPlacedReference[] | undefined;
      return refs?.[hit.instanceId] ?? null;
    };
    const onPointerUp = (e: PointerEvent) => {
      if (transformControls.dragging) return;
      if (e.button !== 0) return;
      if (Math.hypot(e.clientX - downX, e.clientY - downY) > 5 || e.timeStamp - downT > 400) return;
      onSelectRef.current?.(pickRefAt(e.clientX, e.clientY));
    };
    const onRefDblClick = (e: MouseEvent) => {
      if (transformControls.dragging) return;
      const ref = pickRefAt(e.clientX, e.clientY);
      if (!ref) return;
      onSelectRef.current?.(ref);
      onReferenceDoubleClickRef.current?.(ref);
    };
    renderer.domElement.addEventListener('pointerdown', onPointerDown);
    renderer.domElement.addEventListener('pointerup', onPointerUp);
    renderer.domElement.addEventListener('dblclick', onRefDblClick);

    const ghost = new GhostController(scene, fetchGhostGeometry);
    ghostRef.current = ghost;
    const toScene = (p: { x: number; y: number; z: number }) =>
      new THREE.Vector3(p.x - worldCenter.x, p.y - worldCenter.y, p.z - worldCenter.z);
    const groundPlane = new THREE.Plane(new THREE.Vector3(0, 0, 1), -groundZ);
    const pointAt = (clientX: number, clientY: number) => {
      const rect = renderer.domElement.getBoundingClientRect();
      ndc.x = ((clientX - rect.left) / rect.width) * 2 - 1;
      ndc.y = -((clientY - rect.top) / rect.height) * 2 + 1;
      raycaster.setFromCamera(ndc, camera);
      const hit = raycaster.intersectObjects(pickables.filter(isMeshPickable), false).find(h => h.instanceId != null);
      const local = hit ? hit.point : raycaster.ray.intersectPlane(groundPlane, new THREE.Vector3());
      return local ? { x: local.x + worldCenter.x, y: local.y + worldCenter.y, z: local.z + worldCenter.z } : null;
    };
    const viewCentre = () => ({
      x: controls.target.x + worldCenter.x, y: controls.target.y + worldCenter.y, z: controls.target.z + worldCenter.z,
    });
    const detachDrop = attachDropTarget({
      mount,
      ghost,
      pointAt,
      toScene,
      sink: () => ({ placeDrop: onPlaceDropRef.current, log: onDropLogRef.current }),
    });


    const _v = new THREE.Vector3();
    const radiusOf = (r: CellPlacedReference) => {
      const box = (r.modelPath ? modelBBox.get(r.modelPath) : undefined) ?? scolBBox.get(r.formKey);
      const rad = box ? box.getSize(_v).length() * 0.5 * refScale(r.scale) : 60;
      return Math.max(rad, 30);
    };
    const focusOn = (k: string | null) => {
      const r = k ? refByKey.get(k) : undefined;
      if (!r) return;
      const c = new THREE.Vector3(r.position.x - worldCenter.x, r.position.y - worldCenter.y, r.position.z - worldCenter.z);
      const dist = radiusOf(r) * 3;
      controls.target.copy(c);
      camera.position.set(c.x + dist * 0.8, c.y - dist, c.z + dist * 0.6);
      camera.near = Math.max(dist / 500, 0.05); camera.far = dist * 200;
      camera.updateProjectionMatrix(); controls.update();
    };
    const topDownOn = (k: string | null) => {
      const r = k ? refByKey.get(k) : undefined;
      if (!r) return;
      const c = new THREE.Vector3(r.position.x - worldCenter.x, r.position.y - worldCenter.y, r.position.z - worldCenter.z);
      const dist = radiusOf(r) * 3;
      controls.target.copy(c);
      camera.position.set(c.x, c.y - 0.001, c.z + dist);
      camera.updateProjectionMatrix(); controls.update();
    };







    const setOrbitTarget = (k: string | null) => {
      const r = k ? refByKey.get(k) : undefined;
      if (r) controls.target.set(r.position.x - worldCenter.x, r.position.y - worldCenter.y, r.position.z - worldCenter.z);
      else controls.target.set(0, 0, 0);
      controls.update();
    };
    cameraApiRef.current = { focus: focusOn, topDown: topDownOn, setOrbitTarget, pointAt, viewCentre };
    if (placementApiRef) placementApiRef.current = { pointAt, viewCentre };
    setOrbitTarget(selectedKey ?? null);
    const savedView = savedViewRef.current;
    if (savedView) {
      camera.position.copy(savedView.position).sub(worldCenter);
      controls.target.copy(savedView.target).sub(worldCenter);
      camera.near = savedView.near;
      camera.far = savedView.far;
      camera.updateProjectionMatrix();
      controls.update();
    }







    const unbindLights = bindCellLightOrbit(controls, lightRig, () => lightSettingsRef.current);
    setLightWire(lightRig, selectedKey ? refByKey.get(selectedKey) ?? null : null);

    const onKeyDown = (e: KeyboardEvent) => {
      const tag = (e.target as HTMLElement | null)?.tagName;
      if (tag === 'INPUT' || tag === 'TEXTAREA') return;
      if (e.key === 'Escape' && ghost.visible) { ghost.hide(); e.preventDefault(); return; }
      const k = e.key.toLowerCase();
      const editKey = cellEditKey(e);
      if (editKey) {
        e.preventDefault();
        if (e.repeat) return;
        if (editKey === 'delete') onDeleteSelectedRef.current?.();
        else onDuplicateSelectedRef.current?.();
        return;
      }
      if (k === '1' && e.altKey) { onUnhideAllRef.current?.(); e.preventDefault(); }
      else if (k === '1') { onHideSelectedRef.current?.(); e.preventDefault(); }
      else if (k === 'd' && !e.ctrlKey && !e.altKey && !e.shiftKey) { onSelectRef.current?.(null); e.preventDefault(); }
      else if (k === 'f' && e.shiftKey) { focusOn(selectedKeyRef.current ?? null); e.preventDefault(); }
      else if (k === 't' && !e.ctrlKey && !e.altKey && !e.shiftKey) { topDownOn(selectedKeyRef.current ?? null); e.preventDefault(); }
      else if (k === 'm' && !e.ctrlKey && !e.altKey) { onToggleMarkersRef.current?.(); e.preventDefault(); }
      else if (k === 'g' && !e.ctrlKey && !e.altKey) { gizmoModeRef.current = 'translate'; transformControls.setMode('translate'); e.preventDefault(); }
      else if (k === 'r' && !e.ctrlKey && !e.altKey) { gizmoModeRef.current = 'rotate'; transformControls.setMode('rotate'); e.preventDefault(); }
    };
    window.addEventListener('keydown', onKeyDown);


    applyCellVisibility(meshEntries, hiddenTypes, lightSettingsRef.current.layers);
    applySelection(highlight, modelBBox, refByKey, selectedKey ?? null, worldCenter, scolBBox);
    applyHidden(keyToInstances, hiddenRefs, worldCenter);
    attachGizmoTo(selectedKey ?? null);

    const bbBase = new THREE.Matrix4();
    const bbOut = new THREE.Matrix4();
    const bbQuat = new THREE.Quaternion();
    const bbPos = new THREE.Vector3();
    const bbScale = new THREE.Vector3();
    const bbZero = new THREE.Matrix4().makeScale(0, 0, 0);
    const updateBillboards = () => {
      const hidden = hiddenRefsRef.current;
      for (const grp of billboardGroups) {
        if (!grp.inst.visible) continue;
        grp.refs.forEach((r, i) => {
          if (hidden?.has(r.formKey)) { grp.inst.setMatrixAt(i, bbZero); return; }
          bbPos.set(r.position.x - worldCenter.x, r.position.y - worldCenter.y, r.position.z - worldCenter.z);
          bbQuat.setFromEuler(refRotation(r.rotation.x, r.rotation.y, r.rotation.z));
          bbScale.setScalar(refScale(r.scale));
          bbBase.compose(bbPos, bbQuat, bbScale);
          if (grp.local) bbBase.multiply(grp.local);
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
      renderer.domElement.removeEventListener('dblclick', onRefDblClick);
      window.removeEventListener('keydown', onKeyDown);
      unbindLights();
      lightRigRef.current = null;
      rendererRef.current = null;
      if (texStatsTimer) clearTimeout(texStatsTimer);
      savedViewRef.current = {
        position: camera.position.clone().add(worldCenterRef.current),
        target: controls.target.clone().add(worldCenterRef.current),
        near: camera.near,
        far: camera.far,
      };
      transformControls.dispose();
      controls.dispose();
      disposables.forEach(d => d.dispose());
      for (const key of held) host.cache.release(key);
      host.sink = null;
      renderer.dispose();
      if (renderer.domElement.parentElement === mount) mount.removeChild(renderer.domElement);
      meshEntriesRef.current = [];
      highlightRef.current = null;
      gizmoAttachRef.current = null;
      detachDrop();
      ghost.dispose();
      ghostRef.current = null;
      if (placementApiRef) placementApiRef.current = null;
      scolBBoxRef.current = new Map();
    };
  }, [references, geometry, loadTexture, lighting]);


  useEffect(() => {
    applyCellVisibility(meshEntriesRef.current, hiddenTypes, lightSettings.layers);
  }, [hiddenTypes, lightSettings.layers]);


  useEffect(() => {
    if (highlightRef.current)
      applySelection(highlightRef.current, modelBBoxRef.current, refByKeyRef.current, selectedKey ?? null,
        worldCenterRef.current, scolBBoxRef.current);
    gizmoAttachRef.current?.(selectedKey ?? null);
    cameraApiRef.current?.setOrbitTarget(selectedKey ?? null);
    if (lightRigRef.current)
      setLightWire(lightRigRef.current, selectedKey ? refByKeyRef.current.get(selectedKey) ?? null : null);
  }, [selectedKey]);

  useEffect(() => {
    lightSettingsRef.current = lightSettings;
    const rig = lightRigRef.current;
    if (rig) refreshCellLights(rig, lightSettings);
    if (rendererRef.current) setCellExposure(rendererRef.current, lightSettings);
  }, [lightSettings]);


  useEffect(() => {
    applyHidden(keyToInstancesRef.current, hiddenRefs, worldCenterRef.current);
  }, [hiddenRefs]);

  useEffect(() => {
    const ghost = ghostRef.current;
    if (!ghost) return;
    const c = previewItem ? cameraApiRef.current?.viewCentre() : null;
    if (!previewItem || !c) { ghost.hide(); return; }
    const wc = worldCenterRef.current;
    ghost.show(previewItem.modelPath, new THREE.Vector3(c.x - wc.x, c.y - wc.y, c.z - wc.z));
  }, [previewItem]);

  return (
    <>
      <div ref={mountRef} className="nif-viewport cell-viewport" />
    </>
  );
}
