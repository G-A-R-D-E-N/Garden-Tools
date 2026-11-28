import { useEffect, useRef, useState } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import type { HkxBone } from './backend';
import { ragdollGeometry, type RagdollScene } from './hkxRagdollScene';
import type { NifShapeGeo } from './NifViewport';
import { applyShapeTextures, plainShapeMaterial, type ShapeMat } from './util/shapeTextures';
import { makeEffectMaterial } from './util/effectMaterial';
import { poseShape } from './util/skinPose';
import { followStep, MESH_MODES, meshDrawState, parseMeshMode, rootPosition, type MeshMode, type Vec3 } from './util/hkxViewOptions';
import { arrowSegments, loopOffsetStep, motionArm, motionTravel, pathLine, tickSegment, type MotionShape, type MotionState } from './util/rootMotionView';

export interface HkxBody {
  shapes: NifShapeGeo[];
  loadTexture?: (relPath: string, reconstructZ?: boolean) => Promise<string>;
}

interface BodyMesh {
  shape: NifShapeGeo;
  geometry: THREE.BufferGeometry;
  solid: THREE.Mesh;
  wire: THREE.Mesh;
}

interface HkxView {
  setBones(bones: HkxBone[] | null, frameKey: string): void;
  setReference(bones: HkxBone[] | null): void;
  setFollow(follow: boolean): void;
  setMotion(motion: MotionState | null): void;
  setMotionVisible(visible: boolean): void;
  setMesh(mode: MeshMode, wireframe: boolean): void;
  setRagdoll(scene: RagdollScene | null, frameKey: string): void;
  setBody(body: HkxBody | null): void;
  poseBody(bones: HkxBone[] | null, bindPose: boolean): number;
  dispose(): void;
}

function readSetting(key: string): string | null {
  try { return localStorage.getItem(key); } catch { return null; }
}

function writeSetting(key: string, value: string): void {
  try { localStorage.setItem(key, value); } catch { return; }
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

function addSegments(points: Vec3[], segments: [Vec3, Vec3][]): void {
  for (const [from, to] of segments) points.push(from, to);
}

function overlayPoints(shape: MotionShape, extent: number): Vec3[] {
  const points: Vec3[] = [];
  const line = pathLine(shape.path);
  for (let i = 1; i < line.length; i++) points.push(line[i - 1], line[i]);
  const arm = motionArm(extent);
  addSegments(points, shape.marks.map(mark => tickSegment(mark, arm)));
  const end = shape.path[shape.path.length - 1];
  if (end) addSegments(points, arrowSegments(end, shape.forward, arm));
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

  scene.add(new THREE.AmbientLight(0xffffff, 0.65));
  const key = new THREE.DirectionalLight(0xffffff, 1.0);
  key.position.set(1, 1, 2);
  scene.add(key);

  const material = new THREE.LineBasicMaterial({ color: 0x9ad4ff });
  const ragdollMaterial = new THREE.LineBasicMaterial({ color: 0xf0b26a });
  const selectedMaterial = new THREE.LineBasicMaterial({ color: 0xff4fa0 });
  const referenceMaterial = new THREE.LineBasicMaterial({ color: 0x8a909c, transparent: true, opacity: 0.5 });
  const overlayMaterial = new THREE.LineBasicMaterial({ color: 0xdaa520 });
  const wireMaterial = new THREE.MeshBasicMaterial({ color: 0xcfe8ff, wireframe: true, transparent: true, opacity: 0.6 });
  let lines: THREE.LineSegments | null = null;
  let referenceLines: THREE.LineSegments | null = null;
  let meshState = { solid: true, wire: false };
  let followTravel = false;
  let travel: Vec3 = [0, 0, 0];
  let boneTravel: Vec3 = [0, 0, 0];
  let extractedTravel: Vec3 = [0, 0, 0];
  let loopOffset: Vec3 = [0, 0, 0];
  let lastMotionFrame = 0;
  let motionState: MotionState | null = null;
  let overlay: THREE.LineSegments | null = null;
  let overlayShape: MotionShape | null = null;
  let overlayVisible = true;
  let overlayHadBox = false;
  let framedAt: Vec3 = [0, 0, 0];
  let applied: Vec3 = [0, 0, 0];
  let rootOrigin: Vec3 | null = null;
  let originKey = '';
  let ragdollLines: THREE.LineSegments[] = [];
  let skeletonVisible = true;
  let framedKey = '';
  let boneBox: THREE.Box3 | null = null;
  let bodyGroup: THREE.Group | null = null;
  let bodyMeshes: BodyMesh[] = [];
  const bodyTextures: THREE.Texture[] = [];
  let bodyGeneration = 0;

  let bodyMaterials: ShapeMat[] = [];

  const clearBody = () => {
    bodyGeneration++;
    if (bodyGroup) scene.remove(bodyGroup);
    for (const mesh of bodyMeshes) mesh.geometry.dispose();
    for (const mat of bodyMaterials) mat.dispose();
    for (const tex of bodyTextures) tex.dispose();
    bodyMaterials = [];
    bodyTextures.length = 0;
    bodyMeshes = [];
    bodyGroup = null;
  };

  const clearRagdoll = () => {
    for (const set of ragdollLines) {
      scene.remove(set);
      set.geometry.dispose();
    }
    ragdollLines = [];
  };

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
    framedAt = travel;
    applied = [0, 0, 0];
    loopOffset = [0, 0, 0];
  };

  const retravel = () => {
    travel = [boneTravel[0] + extractedTravel[0], boneTravel[1] + extractedTravel[1], boneTravel[2] + extractedTravel[2]];
  };

  const place = () => {
    const at = motionState?.at;
    for (const object of [lines, bodyGroup]) {
      if (!object) continue;
      object.position.set(at?.[0] ?? 0, at?.[1] ?? 0, at?.[2] ?? 0);
      object.rotation.set(0, 0, at?.[3] ?? 0);
    }
  };

  const rebuildOverlay = (shape: MotionShape | null) => {
    if (overlay) {
      scene.remove(overlay);
      overlay.geometry.dispose();
      overlay = null;
    }
    overlayShape = shape;
    overlayHadBox = boneBox !== null;
    if (!shape) return;
    const bounds = new THREE.Box3().setFromPoints(pathLine(shape.path).map(point => new THREE.Vector3(...point)));
    if (boneBox) bounds.union(boneBox);
    const size = bounds.getSize(new THREE.Vector3());
    const points = overlayPoints(shape, Math.max(size.x, size.y, size.z));
    overlay = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(points.map(point => new THREE.Vector3(...point))), overlayMaterial);
    overlay.visible = overlayVisible;
    scene.add(overlay);
  };

  const syncTravel = () => {
    const next = followStep(travel, framedAt, applied, followTravel, loopOffset);
    applied = next.applied;
    const shift = new THREE.Vector3(...next.step);
    if (shift.lengthSq() === 0) return;
    camera.position.add(shift);
    controls.target.add(shift);
  };

  const applyMesh = () => {
    for (const mesh of bodyMeshes) {
      mesh.solid.visible = meshState.solid;
      mesh.wire.visible = meshState.wire;
    }
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
    setBody(body) {
      clearBody();
      if (!body) return;
      const generation = bodyGeneration;
      const group = new THREE.Group();
      const targets: { mat: ShapeMat; shape: NifShapeGeo; hasUV: boolean }[] = [];
      for (const shape of body.shapes) {
        if (!shape.verts.length || !shape.tris.length) continue;
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.Float32BufferAttribute(shape.verts, 3));
        const hasUV = shape.uvs.length > 0;
        if (hasUV) geometry.setAttribute('uv', new THREE.Float32BufferAttribute(shape.uvs, 2));
        geometry.setAttribute('normal', new THREE.Float32BufferAttribute(
          shape.normals.length === shape.verts.length ? shape.normals : new Array<number>(shape.verts.length).fill(0), 3));
        geometry.setIndex(shape.tris);
        if (shape.normals.length !== shape.verts.length) geometry.computeVertexNormals();
        const mat: ShapeMat = shape.effectShader ? makeEffectMaterial(shape.effect, true) : plainShapeMaterial(false);
        mat.polygonOffset = true;
        mat.polygonOffsetFactor = 1;
        mat.polygonOffsetUnits = 1;
        const solid = new THREE.Mesh(geometry, mat);
        const wire = new THREE.Mesh(geometry, wireMaterial);
        group.add(solid, wire);
        bodyMeshes.push({ shape, geometry, solid, wire });
        bodyMaterials.push(mat);
        targets.push({ mat, shape, hasUV });
      }
      bodyGroup = group;
      place();
      applyMesh();
      scene.add(group);
      const box = new THREE.Box3().setFromObject(group);
      if (boneBox) box.union(boneBox);
      if (!box.isEmpty()) frame(box);
      if (body.loadTexture) applyShapeTextures(targets, body.loadTexture, () => generation !== bodyGeneration, bodyTextures);
    },
    poseBody(bones, bindPose) {
      let unmatched = 0;
      for (const { shape, geometry } of bodyMeshes) {
        if (!shape.skin) continue;
        const posed = bones && !bindPose ? poseShape(shape.verts, shape.normals, shape.skin, bones) : null;
        const position = geometry.getAttribute('position') as THREE.BufferAttribute;
        position.array.set(posed ? posed.positions : shape.verts);
        position.needsUpdate = true;
        const normal = geometry.getAttribute('normal') as THREE.BufferAttribute;
        const restored = posed && posed.normals.length === position.array.length ? posed.normals
          : shape.normals.length === shape.verts.length ? shape.normals : null;
        if (restored) {
          normal.array.set(restored);
          normal.needsUpdate = true;
        } else if (!posed) {
          geometry.computeVertexNormals();
        }
        geometry.computeBoundingSphere();
        if (posed) unmatched = Math.max(unmatched, posed.unmatched);
      }
      return unmatched;
    },
    setBones(bones, frameKey) {
      if (lines) {
        scene.remove(lines);
        lines.geometry.dispose();
        lines = null;
      }
      if (!bones || bones.length === 0) {
        rootOrigin = null;
        return;
      }
      boneBox = new THREE.Box3().setFromPoints(bones.map(b => new THREE.Vector3(...b.translation)));
      if (overlayShape && !overlayHadBox) rebuildOverlay(overlayShape);
      lines = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(segmentPoints(bones)), material);
      lines.visible = skeletonVisible;
      scene.add(lines);
      place();
      const root = rootPosition(bones);
      if (root) {
        if (frameKey !== originKey || !rootOrigin) {
          originKey = frameKey;
          rootOrigin = root;
        }
        boneTravel = [root[0] - rootOrigin[0], root[1] - rootOrigin[1], root[2] - rootOrigin[2]];
        retravel();
      }
      if (frameKey !== framedKey) {
        framedKey = frameKey;
        frame(boneBox.clone());
      }
      syncTravel();
    },
    setReference(bones) {
      if (referenceLines) {
        scene.remove(referenceLines);
        referenceLines.geometry.dispose();
        referenceLines = null;
      }
      if (!bones || bones.length === 0) return;
      referenceLines = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(segmentPoints(bones)), referenceMaterial);
      scene.add(referenceLines);
    },
    setFollow(follow) {
      followTravel = follow;
      syncTravel();
    },
    setMotion(motion) {
      const shape = motion?.shape ?? null;
      if (shape !== overlayShape) {
        rebuildOverlay(shape);
        loopOffset = [0, 0, 0];
        lastMotionFrame = 0;
      }
      if (motion) {
        const nextTravel = motionTravel(motion.at, motion.shape.origin);
        loopOffset = loopOffsetStep(loopOffset, motion.frame, lastMotionFrame, motion.frameCount, motion.playing, followTravel, motion.shape.net,
                                    nextTravel, extractedTravel);
        lastMotionFrame = motion.frame;
        extractedTravel = nextTravel;
      } else {
        extractedTravel = [0, 0, 0];
        lastMotionFrame = 0;
      }
      motionState = motion;
      retravel();
      place();
      syncTravel();
    },
    setMotionVisible(visible) {
      overlayVisible = visible;
      if (overlay) overlay.visible = visible;
    },
    setMesh(mode, wireframe) {
      meshState = meshDrawState(mode, wireframe);
      applyMesh();
    },
    setRagdoll(ragdoll, frameKey) {
      clearRagdoll();
      skeletonVisible = ragdoll ? ragdoll.overlays.skeleton : true;
      if (lines) lines.visible = skeletonVisible;
      if (!ragdoll) return;
      const geometry = ragdollGeometry(ragdoll);
      for (const [points, mat] of [[geometry.normal, ragdollMaterial], [geometry.selected, selectedMaterial]] as const) {
        if (points.length === 0) continue;
        const set = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(points.map(p => new THREE.Vector3(...p))), mat);
        scene.add(set);
        ragdollLines.push(set);
      }
      // Constraint: the camera frames on the ragdoll only when no bone lines exist, so a loaded clip keeps its own framing.
      if (!lines && frameKey !== framedKey && ragdoll.bodies.length > 0) {
        framedKey = frameKey;
        frame(new THREE.Box3().setFromPoints(ragdoll.bodies.map(b => new THREE.Vector3(...b.position))));
      }
    },
    dispose() {
      cancelAnimationFrame(raf);
      ro.disconnect();
      controls.dispose();
      lines?.geometry.dispose();
      referenceLines?.geometry.dispose();
      overlay?.geometry.dispose();
      overlayMaterial.dispose();
      clearRagdoll();
      clearBody();
      material.dispose();
      referenceMaterial.dispose();
      wireMaterial.dispose();
      ragdollMaterial.dispose();
      selectedMaterial.dispose();
      renderer.dispose();
      if (renderer.domElement.parentElement === mount) mount.removeChild(renderer.domElement);
    },
  };
}

export default function HkxViewport(
  { bones, frameKey, ragdoll, body, reference, motion }: {
    bones: HkxBone[] | null; frameKey: string; ragdoll?: RagdollScene | null; body?: HkxBody | null; reference?: HkxBone[] | null;
    motion?: MotionState | null;
  },
) {
  const mountRef = useRef<HTMLDivElement>(null);
  const viewRef = useRef<HkxView | null>(null);
  const [bindPose, setBindPose] = useState(false);
  const [unmatched, setUnmatched] = useState(0);
  const [showReference, setShowReference] = useState(false);
  const [followTravel, setFollowTravel] = useState(false);
  const [showMotion, setShowMotion] = useState(true);
  const [meshMode, setMeshMode] = useState(() => parseMeshMode(readSetting('hkx.meshMode')));
  const [meshWireframe, setMeshWireframe] = useState(() => readSetting('hkx.meshWireframe') === '1');

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

  useEffect(() => {
    viewRef.current?.setReference(showReference ? reference ?? null : null);
  }, [reference, showReference]);

  useEffect(() => {
    viewRef.current?.setFollow(followTravel);
  }, [followTravel]);

  useEffect(() => {
    viewRef.current?.setMotion(motion ?? null);
  }, [motion]);

  useEffect(() => {
    viewRef.current?.setMotionVisible(showMotion);
  }, [showMotion]);

  useEffect(() => {
    viewRef.current?.setMesh(meshMode, meshWireframe);
    writeSetting('hkx.meshMode', meshMode);
    writeSetting('hkx.meshWireframe', meshWireframe ? '1' : '0');
  }, [meshMode, meshWireframe]);

  useEffect(() => {
    viewRef.current?.setRagdoll(ragdoll ?? null, frameKey);
  }, [bones, ragdoll, frameKey]);

  // Constraint: the body is rebuilt only when it changes; a new pose only rewrites the vertex buffers.
  const poseRef = useRef({ bones, bindPose });
  useEffect(() => {
    poseRef.current = { bones, bindPose };
  });

  useEffect(() => {
    viewRef.current?.setBody(body ?? null);
    setUnmatched(viewRef.current?.poseBody(poseRef.current.bones, poseRef.current.bindPose) ?? 0);
  }, [body]);

  useEffect(() => {
    setUnmatched(viewRef.current?.poseBody(bones, bindPose) ?? 0);
  }, [bones, bindPose]);

  return (
    <>
      <div ref={mountRef} className="nif-viewport hkx-viewport" />
      <div className="hkx-viewopts">
        <label title={reference ? undefined : 'No reference pose: load a clip with a skeleton first'}>
          <input type="checkbox" checked={showReference} disabled={!reference}
            onChange={e => setShowReference(e.target.checked)} /> Reference pose
        </label>
        {bones ? (
          <label>
            <input type="checkbox" checked={followTravel} onChange={e => setFollowTravel(e.target.checked)} /> Follow travel
          </label>
        ) : null}
        {motion ? (
          <label>
            <input type="checkbox" checked={showMotion} onChange={e => setShowMotion(e.target.checked)} /> Root-motion path
          </label>
        ) : null}
        {body ? (
          <>
            <label>
              Mesh{' '}
              <select aria-label="Mesh mode" value={meshMode} onChange={e => setMeshMode(parseMeshMode(e.target.value))}>
                {MESH_MODES.map(mode => <option key={mode} value={mode}>{mode}</option>)}
              </select>
            </label>
            <label>
              <input type="checkbox" checked={meshWireframe} onChange={e => setMeshWireframe(e.target.checked)} /> Mesh wireframe
            </label>
          </>
        ) : null}
      </div>
      {body ? (
        <div className="nif-flabel" role="status">
          <label>
            <input type="checkbox" checked={bindPose} onChange={e => setBindPose(e.target.checked)} /> Bind pose
          </label>
          {unmatched > 0 ? ` ${unmatched} body bone(s) have no skeleton bone of the same name and stay in the bind pose.` : ''}
        </div>
      ) : null}
    </>
  );
}
