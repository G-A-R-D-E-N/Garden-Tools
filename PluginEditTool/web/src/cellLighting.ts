export const FO4_LIGHT_GAMMA = 2.2;
export const FO4_RECORD_CHANNEL_STEPS = 255;

export const PHYSICAL_LIGHT_SCALE = Math.PI;
export const POINT_LIGHT_GAIN = 0.8;
export const POINT_LIGHT_RANGE_SCALE = 2;
export const POINT_LIGHT_DECAY = 1;
export const AMBIENT_ONLY_GAIN = 0.5;
export const AMBIENT_GAIN_ALONE = 1;
export const AMBIENT_GAIN_WITH_LIGHTS = 0.5;
export const AMBIENT_COLOUR_FLOOR = 0.12;
export const NEUTRAL_KEY_GAIN = 1;
export const NEUTRAL_FILL_GAIN = 0.35;
export const NEUTRAL_RIM_GAIN = 0.25;
export const NEUTRAL_HEMISPHERE_GAIN = 0.25;
export const NEUTRAL_KEY_POSITION: [number, number, number] = [1, 1, 2];
export const NEUTRAL_FILL_POSITION: [number, number, number] = [-1.5, -1, 0.5];
export const NEUTRAL_RIM_POSITION: [number, number, number] = [-1.2, 1.6, 1];
export const NEUTRAL_SKY_COLOUR: [number, number, number] = [1, 1, 1];
export const NEUTRAL_GROUND_COLOUR: [number, number, number] = [0.35, 0.35, 0.38];
export const EXPOSURE_MIN = 0.25;
export const EXPOSURE_MAX = 2;
export const EXPOSURE_DEFAULT = 0.9;
export const SPOT_PENUMBRA = 0.25;
export const MIN_SPOT_HALF_ANGLE_DEG = 1;
export const MAX_SPOT_HALF_ANGLE_DEG = 89.5;
export const DEFAULT_FOV_DEG = 90;
export const MAX_CELL_LIGHTS = 16;
export const MAX_CELL_SPOT_LIGHTS = 4;
export const DIRECTIONAL_DISTANCE = 1000;
export const FOG_MIN_FAR_SCENE_RADII = 4;
export const FOG_POWER_MIN = 0.25;
export const FOG_POWER_MAX = 4;
export const LIGHT_GAIN_MIN = 0.25;
export const LIGHT_GAIN_MAX = 8;
export const LIGHT_GAIN_DEFAULT = 0.35;
export const LIGHT_RESELECT_DELAY_MS = 150;
export const LIGHT_RESELECT_MIN_MOVE = 1;
export const LIGHTING_SETTINGS_STORAGE_KEY = 'cell.viewerLighting';
export const LIGHTING_SETTINGS_SCHEMA_VERSION = 2;

export type Vec3 = [number, number, number];

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
  color: Vec3;
  intensity: number;
  distance: number;
  decay: number;
  position: Vec3;
  visible: boolean;
  angle?: number;
  penumbra?: number;
  ambientOnly: boolean;
}

export interface HemisphereLightPlan {
  sky: Vec3;
  ground: Vec3;
  intensity: number;
}

export interface DirectionalLightPlan {
  color: Vec3;
  intensity: number;
  position: Vec3;
}

export interface CellLightBase {
  ambient: { color: Vec3 };
  hemisphere: HemisphereLightPlan | null;
  directional: DirectionalLightPlan | null;
  fog: { color: Vec3; near: number; far: number } | null;
  fill: DirectionalLightPlan | null;
  rim: DirectionalLightPlan | null;
  lights: PointLightPlan[];
}

export interface CellLightPlan {
  ambient: { color: Vec3; intensity: number };
  hemisphere: HemisphereLightPlan | null;
  directional: DirectionalLightPlan | null;
  fog: { color: Vec3; near: number; far: number } | null;
  fill: DirectionalLightPlan | null;
  rim: DirectionalLightPlan | null;
  lights: PointLightPlan[];
}

export const TOGGLEABLE_LAYERS = ['room', 'trigger', 'marker', 'effect'] as const;
export type ToggleableLayer = typeof TOGGLEABLE_LAYERS[number];
export type LayerVisibility = Record<ToggleableLayer, boolean>;

export interface LightingSettings {
  fog: boolean;
  lights: boolean;
  ambient: boolean;
  markers: boolean;
  neutral: boolean;
  gain: number;
  layers: LayerVisibility;
  exposure: number;
}

export type LightingFlag = 'fog' | 'lights' | 'ambient' | 'markers' | 'neutral';

export const DEFAULT_LAYER_VISIBILITY: LayerVisibility = { room: true, trigger: true, marker: true, effect: true };

export const DEFAULT_LIGHTING_SETTINGS: LightingSettings = {
  fog: false,
  lights: true,
  ambient: true,
  markers: true,
  neutral: false,
  gain: LIGHT_GAIN_DEFAULT,
  layers: DEFAULT_LAYER_VISIBILITY,
  exposure: EXPOSURE_DEFAULT,
};

export function decodeRecordChannel(value01: number): number {
  if (!Number.isFinite(value01)) return 0;
  const clamped = Math.min(1, Math.max(0, value01));
  const step = Math.round(clamped * FO4_RECORD_CHANNEL_STEPS) / FO4_RECORD_CHANNEL_STEPS;
  return Math.pow(step, FO4_LIGHT_GAMMA);
}

export function byteColourToUnit(rgb: [number, number, number]): Vec3 {
  return [rgb[0] / 255, rgb[1] / 255, rgb[2] / 255];
}

export function clampLightGain(value: unknown): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) return LIGHT_GAIN_DEFAULT;
  return Math.min(LIGHT_GAIN_MAX, Math.max(LIGHT_GAIN_MIN, value));
}

export function clampExposure(value: unknown): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) return EXPOSURE_DEFAULT;
  return Math.min(EXPOSURE_MAX, Math.max(EXPOSURE_MIN, value));
}

export function fogPowerFactor(power: number | undefined): number {
  if (typeof power !== 'number' || !Number.isFinite(power) || power <= 0) return 1;
  return Math.min(FOG_POWER_MAX, Math.max(FOG_POWER_MIN, power));
}

export function ambientColourFloor(colour: Vec3): Vec3 {
  return [
    Math.max(colour[0], AMBIENT_COLOUR_FLOOR),
    Math.max(colour[1], AMBIENT_COLOUR_FLOOR),
    Math.max(colour[2], AMBIENT_COLOUR_FLOOR),
  ];
}

export function ambientGain(hasOtherLights: boolean, ambientOn: boolean): number {
  if (!ambientOn) return 0;
  return hasOtherLights ? AMBIENT_GAIN_WITH_LIGHTS : AMBIENT_GAIN_ALONE;
}

export function ambientIntensity(hasOtherLights: boolean, ambientOn: boolean): number {
  return ambientGain(hasOtherLights, ambientOn) * PHYSICAL_LIGHT_SCALE;
}

export function fogRange(
  lighting: Pick<CellLightingData, 'fogNear' | 'fogFar' | 'fogPower'>,
  sceneRadius: number,
): { near: number; far: number } | null {
  const near = Math.max(0, lighting.fogNear || 0);
  const gameFar = lighting.fogFar;
  if (!Number.isFinite(gameFar) || gameFar <= near || gameFar <= 0) return null;
  const stretched = near + (gameFar - near) * fogPowerFactor(lighting.fogPower);
  const minimumFar = FOG_MIN_FAR_SCENE_RADII * Math.max(0, sceneRadius);
  return { near, far: Math.max(stretched, minimumFar) };
}

function halfAngleDegrees(fovDeg: number | undefined): number {
  const fov = typeof fovDeg === 'number' && fovDeg > 0 && Number.isFinite(fovDeg) ? fovDeg : DEFAULT_FOV_DEG;
  return Math.min(MAX_SPOT_HALF_ANGLE_DEG, Math.max(MIN_SPOT_HALF_ANGLE_DEG, fov * 0.5));
}

export function fo4LightToThree(light: FoLightData, position: Vec3 = [0, 0, 0]): PointLightPlan | null {
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
  const intensity = sign * peak * POINT_LIGHT_GAIN * PHYSICAL_LIGHT_SCALE * radius
    * (ambientOnly ? AMBIENT_ONLY_GAIN : 1);

  const plan: PointLightPlan = {
    kind: light.isSpot ? 'spot' : 'point',
    color: [r / peak, g / peak, b / peak],
    intensity,
    distance: radius * POINT_LIGHT_RANGE_SCALE,
    decay: POINT_LIGHT_DECAY,
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

export function selectNearestLights<T extends { position: Vec3; intensity?: number }>(
  lights: readonly T[],
  focus: Vec3,
  limit: number = MAX_CELL_LIGHTS,
): T[] {
  const cap = Math.max(0, Math.floor(limit));
  const dist2 = (p: Vec3) => {
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

export function selectLightPools(
  lights: readonly PointLightPlan[],
  focus: Vec3,
): { points: PointLightPlan[]; spots: PointLightPlan[] } {
  return {
    points: selectNearestLights(lights.filter(light => light.kind === 'point'), focus, MAX_CELL_LIGHTS),
    spots: selectNearestLights(lights.filter(light => light.kind === 'spot'), focus, MAX_CELL_SPOT_LIGHTS),
  };
}

export function lightFocusMoved(previous: Vec3, next: Vec3, minMove: number = LIGHT_RESELECT_MIN_MOVE): boolean {
  return Math.hypot(next[0] - previous[0], next[1] - previous[1], next[2] - previous[2]) > minMove;
}

export function directionalPosition(rotationXYDeg: number, rotationZDeg: number): Vec3 {
  const yaw = (rotationXYDeg * Math.PI) / 180;
  const pitch = (rotationZDeg * Math.PI) / 180;
  const c = Math.cos(pitch);
  return [
    DIRECTIONAL_DISTANCE * c * Math.cos(yaw),
    DIRECTIONAL_DISTANCE * c * Math.sin(yaw),
    DIRECTIONAL_DISTANCE * Math.sin(pitch),
  ];
}

export function neutralLightBase(lights: readonly PointLightPlan[] = []): CellLightBase {
  return {
    ambient: { color: [0, 0, 0] },
    hemisphere: {
      sky: [...NEUTRAL_SKY_COLOUR],
      ground: [...NEUTRAL_GROUND_COLOUR],
      intensity: NEUTRAL_HEMISPHERE_GAIN * PHYSICAL_LIGHT_SCALE,
    },
    directional: {
      color: [1, 1, 1],
      intensity: NEUTRAL_KEY_GAIN * PHYSICAL_LIGHT_SCALE,
      position: [...NEUTRAL_KEY_POSITION],
    },
    fog: null,
    fill: {
      color: [1, 1, 1],
      intensity: NEUTRAL_FILL_GAIN * PHYSICAL_LIGHT_SCALE,
      position: [...NEUTRAL_FILL_POSITION],
    },
    rim: {
      color: [1, 1, 1],
      intensity: NEUTRAL_RIM_GAIN * PHYSICAL_LIGHT_SCALE,
      position: [...NEUTRAL_RIM_POSITION],
    },
    lights: [...lights],
  };
}

export function cellLightBase(
  lighting: CellLightingData | null | undefined,
  lights: readonly PointLightPlan[],
  sceneRadius: number,
): CellLightBase {
  if (!lighting) return neutralLightBase(lights);

  const directionalColour = byteColourToUnit(lighting.directional);
  const directionalAllowed = directionalColour.some(channel => channel > 0);
  const fade = Number.isFinite(lighting.directionalFade) && lighting.directionalFade > 0
    ? lighting.directionalFade : 1;
  const range = fogRange(lighting, sceneRadius);

  return {
    ambient: { color: ambientColourFloor(byteColourToUnit(lighting.ambient)) },
    directional: directionalAllowed
      ? {
        color: directionalColour,
        intensity: fade * PHYSICAL_LIGHT_SCALE,
        position: directionalPosition(lighting.directionalRotationXY, lighting.directionalRotationZ),
      }
      : null,
    fog: range ? { color: byteColourToUnit(lighting.fogNearColor), near: range.near, far: range.far } : null,
    hemisphere: null,
    fill: null,
    rim: null,
    lights: [...lights],
  };
}

export function resolveLightPlan(base: CellLightBase, settings: LightingSettings, focus: Vec3): CellLightPlan {
  const source = settings.neutral ? neutralLightBase() : base;
  const rigOn = settings.lights || settings.neutral;
  const directional = rigOn ? source.directional : null;
  const pools = settings.lights && !settings.neutral
    ? selectLightPools(base.lights, focus)
    : { points: [], spots: [] };
  const gain = clampLightGain(settings.gain);
  const lights = [...pools.points, ...pools.spots].map(light => ({ ...light, intensity: light.intensity * gain }));
  const hasOtherLights = directional !== null || lights.length > 0;
  return {
    ambient: { color: source.ambient.color, intensity: ambientIntensity(hasOtherLights, settings.ambient) },
    hemisphere: rigOn ? source.hemisphere : null,
    directional,
    fog: settings.fog && !settings.neutral ? base.fog : null,
    fill: rigOn ? source.fill : null,
    rim: rigOn ? source.rim : null,
    lights,
  };
}

export function parseLayerVisibility(raw: unknown): LayerVisibility {
  const source: Record<string, unknown> = typeof raw === 'object' && raw !== null ? raw as Record<string, unknown> : {};
  const layers: LayerVisibility = { ...DEFAULT_LAYER_VISIBILITY };
  for (const key of TOGGLEABLE_LAYERS) {
    if (typeof source[key] === 'boolean') layers[key] = source[key] as boolean;
  }
  return layers;
}

export function parseLightingSettings(raw: unknown): LightingSettings {
  const source: Record<string, unknown> = typeof raw === 'object' && raw !== null ? raw as Record<string, unknown> : {};
  const flag = (key: LightingFlag): boolean => typeof source[key] === 'boolean'
    ? source[key] as boolean
    : DEFAULT_LIGHTING_SETTINGS[key];
  return {
    fog: flag('fog'),
    lights: flag('lights'),
    ambient: flag('ambient'),
    markers: flag('markers'),
    neutral: flag('neutral'),
    gain: clampLightGain(source.gain),
    layers: parseLayerVisibility(source.layers),
    exposure: clampExposure(source.exposure),
  };
}

export function serializeLightingSettings(settings: LightingSettings): string {
  return JSON.stringify({ ...settings, schemaVersion: LIGHTING_SETTINGS_SCHEMA_VERSION });
}

export function parseStoredLightingSettings(stored: string | null): LightingSettings {
  if (stored === null) return { ...DEFAULT_LIGHTING_SETTINGS };
  try {
    const raw: unknown = JSON.parse(stored);
    const settings = parseLightingSettings(raw);
    const version = typeof raw === 'object' && raw !== null
      ? (raw as Record<string, unknown>).schemaVersion
      : undefined;
    if (version !== LIGHTING_SETTINGS_SCHEMA_VERSION) {
      return { ...settings, gain: LIGHT_GAIN_DEFAULT, exposure: EXPOSURE_DEFAULT };
    }
    return settings;
  } catch {
    return { ...DEFAULT_LIGHTING_SETTINGS };
  }
}

export function withLightingFlag(settings: LightingSettings, key: LightingFlag, value: boolean): LightingSettings {
  const next = { ...settings };
  next[key] = value;
  return next;
}

export function withLayerFlag(settings: LightingSettings, key: ToggleableLayer, value: boolean): LightingSettings {
  return { ...settings, layers: { ...settings.layers, [key]: value } };
}
