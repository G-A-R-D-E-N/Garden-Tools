import { describe, expect, it } from 'vitest';
import {
  AMBIENT_COLOUR_FLOOR, AMBIENT_ONLY_GAIN, DEFAULT_LAYER_VISIBILITY, DEFAULT_LIGHTING_SETTINGS, EXPOSURE_DEFAULT,
  LIGHT_GAIN_DEFAULT, LIGHTING_SETTINGS_SCHEMA_VERSION, MAX_CELL_LIGHTS, MAX_CELL_SPOT_LIGHTS, PHYSICAL_LIGHT_SCALE,
  POINT_LIGHT_DECAY, POINT_LIGHT_GAIN, POINT_LIGHT_RANGE_SCALE, ambientColourFloor, ambientGain, ambientIntensity,
  cellLightBase, clampExposure, clampLightGain, decodeRecordChannel, directionalPosition, fogPowerFactor, fogRange,
  fo4LightToThree, lightFocusMoved, neutralLightBase, parseLayerVisibility, parseLightingSettings,
  parseStoredLightingSettings, resolveLightPlan, selectLightPools, selectNearestLights, serializeLightingSettings,
  withLayerFlag, withLightingFlag, type CellLightingData,
} from './cellLighting';

const white = { r: 1, g: 1, b: 1 };

const settingsWith = (patch: Partial<typeof DEFAULT_LIGHTING_SETTINGS>) => ({ ...DEFAULT_LIGHTING_SETTINGS, ...patch });

describe('decodeRecordChannel', () => {
  it('maps 0 and 1 to black and white and rounds to record steps first', () => {
    expect(decodeRecordChannel(0)).toBe(0);
    expect(decodeRecordChannel(1)).toBe(1);
    expect(decodeRecordChannel(0.5)).toBeCloseTo(Math.pow(128 / 255, 2.2), 6);
    expect(decodeRecordChannel(Number.NaN)).toBe(0);
  });
});

describe('fo4LightToThree', () => {
  it('uses twice the radius as the cutoff distance with a linear decay', () => {
    const plan = fo4LightToThree({ radius: 300, color: white });
    expect(plan).not.toBeNull();
    expect(plan!.kind).toBe('point');
    expect(plan!.distance).toBe(300 * POINT_LIGHT_RANGE_SCALE);
    expect(plan!.decay).toBe(POINT_LIGHT_DECAY);
    expect(POINT_LIGHT_DECAY).toBe(1);
  });

  it('scales intensity by the gain, the PI factor and the radius once', () => {
    expect(PHYSICAL_LIGHT_SCALE).toBeCloseTo(Math.PI, 12);
    const plan = fo4LightToThree({ radius: 300, color: white });
    expect(plan!.intensity).toBeCloseTo(POINT_LIGHT_GAIN * PHYSICAL_LIGHT_SCALE * 300, 4);
  });

  it('bounds the near-field boost to the quarter-radius ratio of the range edge', () => {
    const plan = fo4LightToThree({ radius: 300, color: white });
    const edge = plan!.intensity / Math.pow(300, plan!.decay);
    const quarter = plan!.intensity / Math.pow(75, plan!.decay);
    expect(quarter / edge).toBeCloseTo(4, 6);
    expect((quarter * LIGHT_GAIN_DEFAULT) / PHYSICAL_LIGHT_SCALE).toBeLessThan(1.5);
  });

  it('returns null for a zero, negative or non-finite radius, or a black colour', () => {
    expect(fo4LightToThree({ radius: 0, color: white })).toBeNull();
    expect(fo4LightToThree({ radius: -5, color: white })).toBeNull();
    expect(fo4LightToThree({ radius: 100, color: { r: 0, g: 0, b: 0 } })).toBeNull();
    expect(fo4LightToThree({ radius: Number.POSITIVE_INFINITY, color: white })).toBeNull();
  });

  it('normalises colour by its peak so intensity carries the brightness', () => {
    const plan = fo4LightToThree({ radius: 100, color: { r: 1, g: 0.5, b: 0 } });
    expect(plan!.color[0]).toBeCloseTo(1, 6);
    expect(plan!.color[1]).toBeCloseTo(decodeRecordChannel(0.5), 6);
    expect(plan!.color[2]).toBe(0);
  });

  it('scales intensity by the absolute fade and flips sign for negative fade', () => {
    const half = fo4LightToThree({ radius: 100, color: white, fade: 0.5 });
    const full = fo4LightToThree({ radius: 100, color: white, fade: 1 });
    expect(half!.intensity).toBeCloseTo(full!.intensity * 0.5, 6);
    const negative = fo4LightToThree({ radius: 100, color: white, fade: -1 });
    expect(negative!.intensity).toBeCloseTo(-full!.intensity, 6);
  });

  it('makes spots with a clamped half-angle from the FOV', () => {
    const narrow = fo4LightToThree({ radius: 100, color: white, isSpot: true, fov: 60 });
    expect(narrow!.kind).toBe('spot');
    expect(narrow!.angle).toBeCloseTo((30 * Math.PI) / 180, 6);
    const wide = fo4LightToThree({ radius: 100, color: white, isSpot: true, fov: 400 });
    expect(wide!.angle).toBeCloseTo((89.5 * Math.PI) / 180, 6);
    const missing = fo4LightToThree({ radius: 100, color: white, isSpot: true });
    expect(missing!.angle).toBeCloseTo((45 * Math.PI) / 180, 6);
  });

  it('hides off-by-default lights and dims ambient-only lights', () => {
    const lit = fo4LightToThree({ radius: 100, color: white, offByDefault: true });
    expect(lit!.visible).toBe(false);
    expect(fo4LightToThree({ radius: 100, color: white })!.visible).toBe(true);
    const fill = fo4LightToThree({ radius: 100, color: white, ambientOnly: true });
    const normal = fo4LightToThree({ radius: 100, color: white });
    expect(fill!.intensity).toBeCloseTo(normal!.intensity * AMBIENT_ONLY_GAIN, 6);
    expect(fill!.ambientOnly).toBe(true);
  });

  it('places the light at the given position', () => {
    const plan = fo4LightToThree({ radius: 100, color: white }, [5, -6, 7]);
    expect(plan!.position).toEqual([5, -6, 7]);
  });
});

describe('fogPowerFactor', () => {
  it('uses 1 for a missing, zero, negative or non-finite power', () => {
    expect(fogPowerFactor(undefined)).toBe(1);
    expect(fogPowerFactor(0)).toBe(1);
    expect(fogPowerFactor(-2)).toBe(1);
    expect(fogPowerFactor(Number.NaN)).toBe(1);
  });

  it('clamps the power to 0.25..4', () => {
    expect(fogPowerFactor(0.1)).toBe(0.25);
    expect(fogPowerFactor(9)).toBe(4);
    expect(fogPowerFactor(2)).toBe(2);
  });
});

describe('fogRange', () => {
  const base = { fogNear: 100, fogFar: 2000 };

  it('keeps the game far distance when it is beyond four scene radii', () => {
    expect(fogRange(base, 50)).toEqual({ near: 100, far: 2000 });
  });

  it('raises far to four scene radii when the game far is closer', () => {
    expect(fogRange({ fogNear: 100, fogFar: 300 }, 1000)).toEqual({ near: 100, far: 4000 });
  });

  it('stretches the band with fogPower', () => {
    expect(fogRange({ ...base, fogPower: 0.5 }, 50)!.far).toBe(1050);
    expect(fogRange({ ...base, fogPower: 2 }, 50)!.far).toBe(3900);
  });

  it('clamps the fogPower stretch to 4 times the game band', () => {
    expect(fogRange({ ...base, fogPower: 10 }, 50)!.far).toBe(100 + 1900 * 4);
  });

  it('returns null when far does not exceed near or is not a positive number', () => {
    expect(fogRange({ fogNear: 100, fogFar: 100 }, 50)).toBeNull();
    expect(fogRange({ fogNear: 0, fogFar: 0 }, 50)).toBeNull();
    expect(fogRange({ fogNear: 100, fogFar: Number.NaN }, 50)).toBeNull();
  });
});

describe('ambient', () => {
  it('uses gain 0 when off, 1 alone and 0.7 with other lights', () => {
    expect(ambientGain(false, false)).toBe(0);
    expect(ambientGain(true, false)).toBe(0);
    expect(ambientGain(false, true)).toBe(1);
    expect(ambientGain(true, true)).toBe(0.5);
  });

  it('multiplies the gain by the PI factor that cancels the Lambert 1/PI', () => {
    expect(ambientIntensity(false, true)).toBeCloseTo(PHYSICAL_LIGHT_SCALE, 12);
    expect(ambientIntensity(true, true)).toBeCloseTo(0.5 * PHYSICAL_LIGHT_SCALE, 12);
    expect(ambientIntensity(true, false)).toBe(0);
  });

  it('floors each colour channel to the ambient minimum', () => {
    expect(ambientColourFloor([0, 0.5, 0.1])).toEqual([AMBIENT_COLOUR_FLOOR, 0.5, AMBIENT_COLOUR_FLOOR]);
  });
});

describe('clampLightGain', () => {
  it('falls back to the default gain for a missing or non-numeric gain', () => {
    expect(clampLightGain(undefined)).toBe(LIGHT_GAIN_DEFAULT);
    expect(clampLightGain('big')).toBe(LIGHT_GAIN_DEFAULT);
    expect(clampLightGain(Number.NaN)).toBe(LIGHT_GAIN_DEFAULT);
  });

  it('clamps to 0.25..8 and keeps values in range', () => {
    expect(clampLightGain(0.01)).toBe(0.25);
    expect(clampLightGain(100)).toBe(8);
    expect(clampLightGain(2)).toBe(2);
  });
});

describe('selectNearestLights', () => {
  const at = (x: number) => ({ position: [x, 0, 0] as [number, number, number], id: x });

  it('keeps the nearest lights first and drops the rest beyond the cap', () => {
    const lights = Array.from({ length: 20 }, (_, i) => at(i * 10));
    const kept = selectNearestLights(lights, [0, 0, 0]);
    expect(kept).toHaveLength(MAX_CELL_LIGHTS);
    expect(kept.map(l => l.id)).toEqual(lights.slice(0, MAX_CELL_LIGHTS).map(l => l.id));
  });

  it('orders by distance to the focus point, not by input order', () => {
    const lights = [at(500), at(10), at(200)];
    expect(selectNearestLights(lights, [0, 0, 0], 2).map(l => l.id)).toEqual([10, 200]);
  });

  it('fills the cap with positive lights before any negative light', () => {
    const positive = [300, 310, 320].map(x => ({ position: [x, 0, 0] as [number, number, number], intensity: 1, id: x }));
    const negative = Array.from({ length: 20 }, (_, i) => ({
      position: [i, 0, 0] as [number, number, number], intensity: -1, id: i,
    }));
    const kept = selectNearestLights([...negative, ...positive], [0, 0, 0]);
    expect(kept).toHaveLength(MAX_CELL_LIGHTS);
    expect(kept.slice(0, 3).map(l => l.id)).toEqual([300, 310, 320]);
    expect(kept.slice(3).every(l => l.intensity === -1)).toBe(true);
  });

  it('returns nothing for a zero cap', () => {
    expect(selectNearestLights([at(1)], [0, 0, 0], 0)).toEqual([]);
  });
});

describe('selectLightPools', () => {
  const point = (x: number) => fo4LightToThree({ radius: 100, color: white }, [x, 0, 0])!;
  const spot = (x: number) => fo4LightToThree({ radius: 100, color: white, isSpot: true }, [x, 0, 0])!;

  it('keeps at most 16 point lights and 4 spot lights, nearest first', () => {
    const points = Array.from({ length: 20 }, (_, i) => point(i * 10));
    const spots = Array.from({ length: 6 }, (_, i) => spot(i * 10));
    const pools = selectLightPools([...points, ...spots], [0, 0, 0]);
    expect(pools.points).toHaveLength(MAX_CELL_LIGHTS);
    expect(pools.points.map(p => p.position[0])).toEqual(Array.from({ length: MAX_CELL_LIGHTS }, (_, i) => i * 10));
    expect(pools.points.every(p => p.kind === 'point')).toBe(true);
    expect(pools.spots).toHaveLength(MAX_CELL_SPOT_LIGHTS);
    expect(pools.spots.every(p => p.kind === 'spot')).toBe(true);
  });

  it('selects around the focus point rather than the origin', () => {
    const pools = selectLightPools([point(0), point(1000), point(1010)], [1010, 0, 0]);
    expect(pools.points.map(p => p.position[0])).toEqual([1010, 1000, 0]);
  });
});

describe('lightFocusMoved', () => {
  it('reports movement only beyond the minimum distance', () => {
    expect(lightFocusMoved([0, 0, 0], [0.5, 0, 0])).toBe(false);
    expect(lightFocusMoved([0, 0, 0], [1, 0, 0])).toBe(false);
    expect(lightFocusMoved([0, 0, 0], [3, 4, 0])).toBe(true);
    expect(lightFocusMoved([0, 0, 0], [3, 4, 0], 10)).toBe(false);
  });
});

describe('directionalPosition', () => {
  it('points along +X at zero heading and zero tilt', () => {
    const [x, y, z] = directionalPosition(0, 0);
    expect(x).toBeGreaterThan(0);
    expect(Math.abs(y)).toBeLessThan(1e-9);
    expect(Math.abs(z)).toBeLessThan(1e-9);
  });

  it('lifts the light with a positive tilt', () => {
    const [, , z] = directionalPosition(0, 45);
    expect(z).toBeGreaterThan(0);
  });
});

describe('lighting settings', () => {
  it('defaults to fog off, lights, ambient and markers on, neutral off, gain 0.35, every layer visible, exposure 0.9', () => {
    expect(DEFAULT_LIGHTING_SETTINGS).toEqual({
      fog: false, lights: true, ambient: true, markers: true, neutral: false,
      gain: 0.35, layers: { room: true, trigger: true, marker: true, effect: true }, exposure: 0.9,
    });
    expect(LIGHT_GAIN_DEFAULT).toBe(0.35);
    expect(EXPOSURE_DEFAULT).toBe(0.9);
    expect(parseLightingSettings(null)).toEqual(DEFAULT_LIGHTING_SETTINGS);
  });

  it('clamps exposure to 0.25..2 and falls back to the default', () => {
    expect(clampExposure(undefined)).toBe(0.9);
    expect(clampExposure(Number.NaN)).toBe(0.9);
    expect(clampExposure(0.01)).toBe(0.25);
    expect(clampExposure(9)).toBe(2);
    expect(parseLightingSettings({ exposure: 1.4 }).exposure).toBe(1.4);
  });

  it('reads the neutral flag and drops the retired effects flag', () => {
    expect(parseLightingSettings({ neutral: true })).toMatchObject({ neutral: true });
    expect(parseLightingSettings({ effects: true })).not.toHaveProperty('effects');
  });

  it('keeps valid booleans and clamps the gain', () => {
    expect(parseLightingSettings({ fog: true, gain: 3 })).toEqual(settingsWith({ fog: true, gain: 3 }));
    expect(parseLightingSettings({ gain: 100 }).gain).toBe(8);
  });

  it('falls back to the default for a non-boolean flag or a non-numeric gain', () => {
    expect(parseLightingSettings({ fog: 'yes', lights: 0, gain: 'big' })).toEqual(DEFAULT_LIGHTING_SETTINGS);
  });

  it('reads stored JSON and falls back to the defaults on junk', () => {
    expect(parseStoredLightingSettings(null)).toEqual(DEFAULT_LIGHTING_SETTINGS);
    expect(parseStoredLightingSettings('not json')).toEqual(DEFAULT_LIGHTING_SETTINGS);
    expect(parseStoredLightingSettings('null')).toEqual(DEFAULT_LIGHTING_SETTINGS);
    const current = { schemaVersion: LIGHTING_SETTINGS_SCHEMA_VERSION };
    expect(parseStoredLightingSettings(JSON.stringify({
      ...current, lights: false, ambient: false, markers: false, gain: 0.1, layers: { room: false, effect: false },
    }))).toEqual({
      fog: false, lights: false, ambient: false, markers: false, neutral: false,
      gain: 0.25, layers: { room: false, trigger: true, marker: true, effect: false }, exposure: 0.9,
    });
  });

  it('parses layer visibility per key and ignores junk, unknown keys and non-objects', () => {
    expect(parseLayerVisibility(null)).toEqual(DEFAULT_LAYER_VISIBILITY);
    expect(parseLayerVisibility('room')).toEqual(DEFAULT_LAYER_VISIBILITY);
    expect(parseLayerVisibility({ room: 0, trigger: 'no', marker: false, light: false }))
      .toEqual({ room: true, trigger: true, marker: false, effect: true });
    expect(parseLightingSettings({ layers: { effect: false } }).layers)
      .toEqual({ room: true, trigger: true, marker: true, effect: false });
  });

  it('sets one layer flag without changing the other layers or the source settings', () => {
    const next = withLayerFlag(DEFAULT_LIGHTING_SETTINGS, 'trigger', false);
    expect(next.layers).toEqual({ room: true, trigger: false, marker: true, effect: true });
    expect(next.lights).toBe(true);
    expect(DEFAULT_LIGHTING_SETTINGS.layers.trigger).toBe(true);
    expect(withLayerFlag(next, 'trigger', true).layers).toEqual(DEFAULT_LAYER_VISIBILITY);
  });

  it('round-trips layer visibility through stored JSON', () => {
    const stored = JSON.stringify({ layers: { room: false, trigger: false, marker: true, effect: false } });
    expect(parseStoredLightingSettings(stored).layers)
      .toEqual({ room: false, trigger: false, marker: true, effect: false });
  });

  it('resets a stored gain and exposure from an older schema to the new defaults', () => {
    const old = JSON.stringify({ lights: true, gain: 1.05, exposure: 1.6, fog: true });
    expect(parseStoredLightingSettings(old)).toEqual(settingsWith({ fog: true }));
    expect(parseStoredLightingSettings(old).gain).toBe(LIGHT_GAIN_DEFAULT);
    expect(parseStoredLightingSettings(JSON.stringify({ gain: 1.05 })).gain).toBe(LIGHT_GAIN_DEFAULT);
  });

  it('keeps gain and exposure when the stored schema matches the current one', () => {
    const stored = JSON.stringify({ schemaVersion: LIGHTING_SETTINGS_SCHEMA_VERSION, gain: 1.05, exposure: 1.6 });
    expect(parseStoredLightingSettings(stored)).toMatchObject({ gain: 1.05, exposure: 1.6 });
  });

  it('writes the schema version and reads back what it wrote', () => {
    const written = serializeLightingSettings(settingsWith({ gain: 2, exposure: 1.2 }));
    expect(JSON.parse(written).schemaVersion).toBe(LIGHTING_SETTINGS_SCHEMA_VERSION);
    expect(parseStoredLightingSettings(written)).toEqual(settingsWith({ gain: 2, exposure: 1.2 }));
  });

  it('sets one flag without changing the source settings', () => {
    expect(withLightingFlag(DEFAULT_LIGHTING_SETTINGS, 'fog', true)).toEqual(settingsWith({ fog: true }));
    expect(DEFAULT_LIGHTING_SETTINGS.fog).toBe(false);
  });
});

const lighting: CellLightingData = {
  ambient: [51, 102, 153],
  directional: [255, 255, 255],
  directionalRotationXY: 0,
  directionalRotationZ: 0,
  directionalFade: 1,
  fogNearColor: [0, 0, 0],
  fogNear: 100,
  fogFar: 2000,
};

describe('resolveLightPlan', () => {
  const lamp = fo4LightToThree({ radius: 100, color: white }, [10, 0, 0])!;

  it('uses the neutral key, fill, rim and hemisphere when the cell has no lighting', () => {
    const base = cellLightBase(null, [], 100);
    expect(base.directional).not.toBeNull();
    expect(base.fill).not.toBeNull();
    expect(base.rim).not.toBeNull();
    expect(base.hemisphere).not.toBeNull();
    expect(base.ambient.color).toEqual([0, 0, 0]);
    expect(base.fog).toBeNull();
    expect(neutralLightBase([lamp]).lights).toHaveLength(1);
  });

  it('ignores cell lights, ambient and fog while the neutral preset is on', () => {
    const lit = cellLightBase(lighting, [lamp], 100);
    const plan = resolveLightPlan(lit, settingsWith({ neutral: true, fog: true }), [0, 0, 0]);
    expect(plan.lights).toEqual([]);
    expect(plan.fog).toBeNull();
    expect(plan.ambient.color).toEqual([0, 0, 0]);
    expect(plan.directional).not.toBeNull();
    expect(plan.fill).not.toBeNull();
    expect(plan.rim).not.toBeNull();
    expect(plan.hemisphere).not.toBeNull();
  });

  it('keeps the neutral rig on with cell lights off, and drops it only when the preset is off', () => {
    const lit = cellLightBase(lighting, [lamp], 100);
    expect(resolveLightPlan(lit, settingsWith({ neutral: true, lights: false }), [0, 0, 0]).fill).not.toBeNull();
    expect(resolveLightPlan(lit, settingsWith({ lights: false }), [0, 0, 0]).hemisphere).toBeNull();
  });

  it('floors the cell ambient colour before it is used', () => {
    const base = cellLightBase(lighting, [], 100);
    expect(base.ambient.color[0]).toBeCloseTo(51 / 255, 6);
    expect(AMBIENT_COLOUR_FLOOR).toBe(0.12);
    expect(base.ambient.color[1]).toBeCloseTo(102 / 255, 6);
    expect(base.ambient.color[2]).toBeCloseTo(153 / 255, 6);
  });

  it('keeps fog off by default and applies the cell fog when it is turned on', () => {
    const base = cellLightBase(lighting, [], 100);
    expect(resolveLightPlan(base, DEFAULT_LIGHTING_SETTINGS, [0, 0, 0]).fog).toBeNull();
    expect(resolveLightPlan(base, settingsWith({ fog: true }), [0, 0, 0]).fog)
      .toEqual({ color: [0, 0, 0], near: 100, far: 2000 });
  });

  it('gives ambient full gain alone and 0.5 once a key or point light is present', () => {
    const alone = cellLightBase({ ...lighting, directional: [0, 0, 0] }, [], 100);
    expect(resolveLightPlan(alone, DEFAULT_LIGHTING_SETTINGS, [0, 0, 0]).directional).toBeNull();
    expect(resolveLightPlan(alone, DEFAULT_LIGHTING_SETTINGS, [0, 0, 0]).ambient.intensity)
      .toBeCloseTo(PHYSICAL_LIGHT_SCALE, 6);
    const lit = cellLightBase(lighting, [lamp], 100);
    expect(resolveLightPlan(lit, DEFAULT_LIGHTING_SETTINGS, [0, 0, 0]).ambient.intensity)
      .toBeCloseTo(0.5 * PHYSICAL_LIGHT_SCALE, 6);
  });

  it('turns ambient off to zero intensity', () => {
    const lit = cellLightBase(lighting, [lamp], 100);
    expect(resolveLightPlan(lit, settingsWith({ ambient: false }), [0, 0, 0]).ambient.intensity).toBe(0);
  });

  it('doubles point light intensity at gain 2', () => {
    const lit = cellLightBase(lighting, [lamp], 100);
    const one = resolveLightPlan(lit, DEFAULT_LIGHTING_SETTINGS, [0, 0, 0]).lights[0];
    const two = resolveLightPlan(lit, settingsWith({ gain: 2 }), [0, 0, 0]).lights[0];
    expect(two.intensity / one.intensity).toBeCloseTo(2 / LIGHT_GAIN_DEFAULT, 6);
  });

  it('drops the key, the fill and every light when lights are off', () => {
    const lit = cellLightBase(lighting, [lamp], 100);
    const plan = resolveLightPlan(lit, settingsWith({ lights: false }), [0, 0, 0]);
    expect(plan.directional).toBeNull();
    expect(plan.lights).toEqual([]);
    expect(plan.fill).toBeNull();
    expect(resolveLightPlan(cellLightBase(null, [], 100), settingsWith({ lights: false }), [0, 0, 0]).fill).toBeNull();
  });

  it('caps the point lights in the plan to the nearest set', () => {
    const many = Array.from({ length: 30 }, (_, i) => fo4LightToThree({ radius: 100, color: white }, [i * 5, 0, 0])!);
    const plan = resolveLightPlan(cellLightBase(lighting, many, 100), DEFAULT_LIGHTING_SETTINGS, [0, 0, 0]);
    const points = plan.lights.filter(light => light.kind === 'point');
    expect(points).toHaveLength(MAX_CELL_LIGHTS);
    expect(points[0].position[0]).toBe(0);
  });
});
