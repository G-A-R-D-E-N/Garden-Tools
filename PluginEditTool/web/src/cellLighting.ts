
export const FO4_LIGHT_GAMMA = 2.2;
export const FO4_RECORD_CHANNEL_STEPS = 255;

export const POINT_LIGHT_GAIN = 0.45;
export const AMBIENT_ONLY_GAIN = 0.5;
export const SPOT_PENUMBRA = 0.25;
export const MIN_SPOT_HALF_ANGLE_DEG = 1;
export const MAX_SPOT_HALF_ANGLE_DEG = 89.5;
export const DEFAULT_FOV_DEG = 90;
export const MAX_CELL_LIGHTS = 16;
export const DIRECTIONAL_DISTANCE = 1000;

export interface Rgb01 { r: number; g: number; b: number }

export interface FoLightData {
  radius: number;
  color: Rgb01;
  fade?: number | null;
  fov?: number;
  isSpot?: boolean;
  ambientOnly?: boolean;
  offByDefault?: boolean;
}

export interface CellLightingData {
  ambient: [number, number, number];
  directional: [number, number, number];
  directionalRotationXY: number;
  directionalRotationZ: number;
  directionalFade: number;
  fogNearColor: [number, number, number];
  fogFarColor?: [number, number, number] | null;
  fogNear: number;
  fogFar: number;
  fogClipDistance?: number;
  fogPower?: number;
}

export interface PointLightPlan {
  kind: 'point' | 'spot';
  color: [number, number, number];
  intensity: number;
  distance: number;
  decay: number;
  position: [number, number, number];
  visible: boolean;
  angle?: number;
  penumbra?: number;
  ambientOnly: boolean;
}

export interface CellLightPlan {
  ambient: { color: [number, number, number]; intensity: number };
  directional: { color: [number, number, number]; intensity: number; position: [number, number, number] } | null;
  fog: { color: [number, number, number]; near: number; far: number } | null;
  lights: PointLightPlan[];
}

export function decodeRecordChannel(value01: number): number {
  if (!Number.isFinite(value01)) return 0;
  const clamped = Math.min(1, Math.max(0, value01));
  const step = Math.round(clamped * FO4_RECORD_CHANNEL_STEPS) / FO4_RECORD_CHANNEL_STEPS;
  return Math.pow(step, FO4_LIGHT_GAMMA);
}

export function byteColourToUnit(rgb: [number, number, number]): [number, number, number] {
  return [rgb[0] / 255, rgb[1] / 255, rgb[2] / 255];
}

function halfAngleDegrees(fovDeg: number | undefined): number {
  const fov = typeof fovDeg === 'number' && fovDeg > 0 && Number.isFinite(fovDeg) ? fovDeg : DEFAULT_FOV_DEG;
  return Math.min(MAX_SPOT_HALF_ANGLE_DEG, Math.max(MIN_SPOT_HALF_ANGLE_DEG, fov * 0.5));
}

export function fo4LightToThree(light: FoLightData, position: [number, number, number] = [0, 0, 0]): PointLightPlan | null {
  const radius = light.radius;
  if (!(radius > 0) || !Number.isFinite(radius)) return null;

  const fade = typeof light.fade === 'number' && Number.isFinite(light.fade) ? light.fade : 1;
  const magnitude = Math.abs(fade);
  const r = decodeRecordChannel(light.color.r) * magnitude;
  const g = decodeRecordChannel(light.color.g) * magnitude;
  const b = decodeRecordChannel(light.color.b) * magnitude;
  const peak = Math.max(r, g, b);
  if (!(peak > 0)) return null;

  const sign = fade < 0 ? -1 : 1;
  const ambientOnly = light.ambientOnly === true;
  const intensity = sign * peak * POINT_LIGHT_GAIN * radius * radius * (ambientOnly ? AMBIENT_ONLY_GAIN : 1);

  const plan: PointLightPlan = {
    kind: light.isSpot ? 'spot' : 'point',
    color: [r / peak, g / peak, b / peak],
    intensity,
    distance: radius,
    decay: 2,
    position,
    visible: light.offByDefault !== true,
    ambientOnly,
  };
  if (plan.kind === 'spot') {
    plan.angle = (halfAngleDegrees(light.fov) * Math.PI) / 180;
    plan.penumbra = SPOT_PENUMBRA;
  }
  return plan;
}

export function selectNearestLights<T extends { position: [number, number, number]; intensity?: number }>(
  lights: readonly T[],
  focus: [number, number, number],
  limit: number = MAX_CELL_LIGHTS,
): T[] {
  const cap = Math.max(0, Math.floor(limit));
  const dist2 = (p: [number, number, number]) => {
    const dx = p[0] - focus[0];
    const dy = p[1] - focus[1];
    const dz = p[2] - focus[2];
    return dx * dx + dy * dy + dz * dz;
  };
  const byDistance = (negative: boolean) => lights
    .map((light, index) => ({ light, index, d: dist2(light.position) }))
    .filter(entry => ((entry.light.intensity ?? 0) < 0) === negative)
    .sort((a, b) => a.d - b.d || a.index - b.index)
    .map(entry => entry.light);
  const positives = byDistance(false).slice(0, cap);
  const negatives = byDistance(true).slice(0, Math.max(0, cap - positives.length));
  return [...positives, ...negatives];
}

export function directionalPosition(rotationXYDeg: number, rotationZDeg: number): [number, number, number] {
  const yaw = (rotationXYDeg * Math.PI) / 180;
  const pitch = (rotationZDeg * Math.PI) / 180;
  const c = Math.cos(pitch);
  return [
    DIRECTIONAL_DISTANCE * c * Math.cos(yaw),
    DIRECTIONAL_DISTANCE * c * Math.sin(yaw),
    DIRECTIONAL_DISTANCE * Math.sin(pitch),
  ];
}

export function neutralLightPlan(): CellLightPlan {
  return {
    ambient: { color: [1, 1, 1], intensity: 0.7 },
    directional: { color: [1, 1, 1], intensity: 1.0, position: [1, 1, 2] },
    fog: null,
    lights: [],
  };
}

export function cellLightPlan(
  lighting: CellLightingData | null | undefined,
  lights: readonly PointLightPlan[],
  focus: [number, number, number],
): CellLightPlan {
  if (!lighting) return { ...neutralLightPlan(), lights: selectNearestLights(lights, focus) };

  const ambient = byteColourToUnit(lighting.ambient);
  const directionalColour = byteColourToUnit(lighting.directional);
  const directionalAllowed = directionalColour.some(channel => channel > 0);
  const fade = Number.isFinite(lighting.directionalFade) && lighting.directionalFade > 0
    ? lighting.directionalFade : 1;

  const fogFar = lighting.fogFar;
  const fogNear = Math.max(0, lighting.fogNear || 0);
  const fog = Number.isFinite(fogFar) && fogFar > fogNear && fogFar > 0
    ? { color: byteColourToUnit(lighting.fogNearColor), near: fogNear, far: fogFar }
    : null;

  return {
    ambient: { color: ambient, intensity: 1 },
    directional: directionalAllowed
      ? {
        color: directionalColour,
        intensity: fade,
        position: directionalPosition(lighting.directionalRotationXY, lighting.directionalRotationZ),
      }
      : null,
    fog,
    lights: selectNearestLights(lights, focus),
  };
}
