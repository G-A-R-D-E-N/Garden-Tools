import * as THREE from 'three';
import type { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import type { CellPlacedReference } from './backend';
import {
  LIGHT_RESELECT_DELAY_MS, MAX_CELL_LIGHTS, MAX_CELL_SPOT_LIGHTS, SPOT_PENUMBRA, clampExposure, lightFocusMoved,
  resolveLightPlan, type CellLightBase, type CellLightPlan, type DirectionalLightPlan, type HemisphereLightPlan,
  type LightingSettings, type PointLightPlan, type Vec3,
} from './cellLighting';
import { setCellEffectsVisible } from './cellEffectMaterial';

export const LIGHT_MARKER_RADIUS = 4;
const LIGHT_MARKER_SEGMENTS_WIDTH = 8;
const LIGHT_MARKER_SEGMENTS_HEIGHT = 6;
const LIGHT_WIRE_SEGMENTS_WIDTH = 24;
const LIGHT_WIRE_SEGMENTS_HEIGHT = 16;
const LIGHT_WIRE_COLOUR = 0xffe08a;
const LIGHT_WIRE_OPACITY = 0.35;
const NEUTRAL_FILL_COLOUR = 0xffffff;

export type LightMarkerMesh = THREE.InstancedMesh<THREE.BufferGeometry, THREE.MeshBasicMaterial>;

export interface CellLightRig {
  scene: THREE.Scene;
  base: CellLightBase;
  worldCenter: THREE.Vector3;
  focus: Vec3;
  ambient: THREE.AmbientLight;
  hemisphere: THREE.HemisphereLight;
  sun: THREE.DirectionalLight;
  fill: THREE.DirectionalLight;
  rim: THREE.DirectionalLight;
  points: THREE.PointLight[];
  spots: THREE.SpotLight[];
  fog: THREE.Fog | null;
  markers: LightMarkerMesh | null;
  wire: THREE.LineSegments;
  dispose: () => void;
}

const srgbColour = (c: Vec3) => new THREE.Color().setRGB(c[0], c[1], c[2], THREE.SRGBColorSpace);
const linearColour = (c: Vec3) => new THREE.Color().setRGB(c[0], c[1], c[2], THREE.LinearSRGBColorSpace);

export function buildLightMarkers(refs: readonly CellPlacedReference[], worldCenter: THREE.Vector3): LightMarkerMesh | null {
  if (refs.length === 0) return null;
  const geometry: THREE.BufferGeometry = new THREE.SphereGeometry(LIGHT_MARKER_RADIUS, LIGHT_MARKER_SEGMENTS_WIDTH, LIGHT_MARKER_SEGMENTS_HEIGHT);
  const material = new THREE.MeshBasicMaterial({ color: 0xffffff });
  const inst: LightMarkerMesh = new THREE.InstancedMesh(geometry, material, refs.length);
  const dummy = new THREE.Object3D();
  refs.forEach((r, i) => {
    dummy.position.set(r.position.x - worldCenter.x, r.position.y - worldCenter.y, r.position.z - worldCenter.z);
    dummy.rotation.set(0, 0, 0);
    dummy.scale.setScalar(1);
    dummy.updateMatrix();
    inst.setMatrixAt(i, dummy.matrix);
    const c = r.light?.color;
    inst.setColorAt(i, srgbColour([c?.r ?? 1, c?.g ?? 1, c?.b ?? 1]));
  });
  inst.instanceMatrix.needsUpdate = true;
  if (inst.instanceColor) inst.instanceColor.needsUpdate = true;
  return inst;
}

export function buildCellLightRig(
  scene: THREE.Scene,
  base: CellLightBase,
  worldCenter: THREE.Vector3,
  markers: LightMarkerMesh | null,
): CellLightRig {
  const ambient = new THREE.AmbientLight(srgbColour(base.ambient.color), 0);
  scene.add(ambient);

  const hemisphere = new THREE.HemisphereLight(0xffffff, 0x000000, 0);
  scene.add(hemisphere);

  const sun = new THREE.DirectionalLight(0xffffff, 0);
  scene.add(sun);

  const fill = new THREE.DirectionalLight(NEUTRAL_FILL_COLOUR, 0);
  scene.add(fill);

  const rim = new THREE.DirectionalLight(NEUTRAL_FILL_COLOUR, 0);
  scene.add(rim);

  const points = Array.from({ length: MAX_CELL_LIGHTS }, () => {
    const light = new THREE.PointLight(0xffffff, 0, 0, 2);
    scene.add(light);
    return light;
  });
  const spots = Array.from({ length: MAX_CELL_SPOT_LIGHTS }, () => {
    const light = new THREE.SpotLight(0xffffff, 0, 0, Math.PI / 4, SPOT_PENUMBRA, 2);
    scene.add(light);
    return light;
  });

  const fog = base.fog ? new THREE.Fog(srgbColour(base.fog.color), base.fog.near, base.fog.far) : null;

  if (markers) scene.add(markers);

  const wireSphere = new THREE.SphereGeometry(1, LIGHT_WIRE_SEGMENTS_WIDTH, LIGHT_WIRE_SEGMENTS_HEIGHT);
  const wireGeometry = new THREE.WireframeGeometry(wireSphere);
  wireSphere.dispose();
  const wire = new THREE.LineSegments(
    wireGeometry,
    new THREE.LineBasicMaterial({
      color: LIGHT_WIRE_COLOUR, transparent: true, opacity: LIGHT_WIRE_OPACITY, depthWrite: false,
    }),
  );
  wire.visible = false;
  scene.add(wire);

  const dispose = () => {
    wire.geometry.dispose();
    (wire.material as THREE.Material).dispose();
    if (markers) {
      markers.geometry.dispose();
      markers.material.dispose();
    }
  };

  return {
    scene, base, worldCenter, focus: [0, 0, 0], ambient, hemisphere, sun, fill, rim, points, spots, fog, markers, wire,
    dispose,
  };
}

function setDirectionalSlot(light: THREE.DirectionalLight, plan: DirectionalLightPlan | null): void {
  light.visible = plan !== null;
  if (!plan) {
    light.intensity = 0;
    return;
  }
  light.color.copy(srgbColour(plan.color));
  light.intensity = plan.intensity;
  light.position.set(...plan.position);
}

function setHemisphereSlot(light: THREE.HemisphereLight, plan: HemisphereLightPlan | null): void {
  light.visible = plan !== null;
  if (!plan) {
    light.intensity = 0;
    return;
  }
  light.color.copy(srgbColour(plan.sky));
  light.groundColor.copy(srgbColour(plan.ground));
  light.intensity = plan.intensity;
}

export function configureCellRenderer(renderer: THREE.WebGLRenderer, settings: LightingSettings): void {
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.NeutralToneMapping;
  setCellExposure(renderer, settings);
}

export function setCellExposure(renderer: THREE.WebGLRenderer, settings: LightingSettings): void {
  renderer.toneMappingExposure = clampExposure(settings.exposure);
}

function setPointSlot(light: THREE.PointLight | THREE.SpotLight, plan: PointLightPlan | undefined): void {
  if (!plan) {
    light.intensity = 0;
    return;
  }
  light.color.copy(linearColour(plan.color));
  light.intensity = plan.intensity;
  light.distance = plan.distance;
  light.decay = plan.decay;
  light.position.set(...plan.position);
}

function setSpotSlot(light: THREE.SpotLight, plan: PointLightPlan | undefined): void {
  setPointSlot(light, plan);
  if (plan?.angle !== undefined) light.angle = plan.angle;
  if (plan?.penumbra !== undefined) light.penumbra = plan.penumbra;
}

export function applyCellLights(rig: CellLightRig, plan: CellLightPlan, settings: LightingSettings): void {
  rig.ambient.color.copy(srgbColour(plan.ambient.color));
  rig.ambient.intensity = plan.ambient.intensity;
  setHemisphereSlot(rig.hemisphere, plan.hemisphere);
  setDirectionalSlot(rig.sun, plan.directional);
  setDirectionalSlot(rig.fill, plan.fill);
  setDirectionalSlot(rig.rim, plan.rim);
  setCellEffectsVisible(rig.scene, settings.layers.effect);
  const pointPlans = plan.lights.filter(light => light.kind === 'point');
  const spotPlans = plan.lights.filter(light => light.kind === 'spot');
  rig.points.forEach((light, i) => setPointSlot(light, pointPlans[i]));
  rig.spots.forEach((light, i) => setSpotSlot(light, spotPlans[i]));
  rig.scene.fog = plan.fog && rig.fog ? rig.fog : null;
  if (rig.markers) rig.markers.visible = settings.markers;
}

export function refreshCellLights(rig: CellLightRig, settings: LightingSettings): void {
  applyCellLights(rig, resolveLightPlan(rig.base, settings, rig.focus), settings);
}

export function setLightWire(rig: CellLightRig, ref: CellPlacedReference | null): void {
  const radius = ref?.light?.radius ?? 0;
  if (!ref || !(radius > 0)) {
    rig.wire.visible = false;
    return;
  }
  const wc = rig.worldCenter;
  rig.wire.position.set(ref.position.x - wc.x, ref.position.y - wc.y, ref.position.z - wc.z);
  rig.wire.scale.setScalar(radius);
  rig.wire.visible = true;
}

export function bindCellLightOrbit(
  controls: OrbitControls,
  rig: CellLightRig,
  settings: () => LightingSettings,
): () => void {
  const orbitFocus = (): Vec3 => [controls.target.x, controls.target.y, controls.target.z];
  const reselect = createLightReselector(() => {
    const next = orbitFocus();
    if (!lightFocusMoved(rig.focus, next)) return;
    rig.focus = next;
    refreshCellLights(rig, settings());
  }, LIGHT_RESELECT_DELAY_MS);
  rig.focus = orbitFocus();
  refreshCellLights(rig, settings());
  controls.addEventListener('change', reselect.schedule);
  return () => {
    controls.removeEventListener('change', reselect.schedule);
    reselect.cancel();
  };
}

export function createLightReselector(run: () => void, delayMs: number = LIGHT_RESELECT_DELAY_MS) {
  let timer: ReturnType<typeof setTimeout> | undefined;
  return {
    schedule: () => {
      if (timer !== undefined) return;
      timer = setTimeout(() => {
        timer = undefined;
        run();
      }, delayMs);
    },
    cancel: () => {
      if (timer !== undefined) clearTimeout(timer);
      timer = undefined;
    },
  };
}
