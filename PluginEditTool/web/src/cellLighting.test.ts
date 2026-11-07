import { describe, expect, it } from 'vitest';
import {
  cellLightPlan, decodeRecordChannel, directionalPosition, fo4LightToThree, neutralLightPlan,
  selectNearestLights, MAX_CELL_LIGHTS, POINT_LIGHT_GAIN, AMBIENT_ONLY_GAIN, type CellLightingData,
} from './cellLighting';

const white = { r: 1, g: 1, b: 1 };

describe('decodeRecordChannel', () => {
  it('maps 0 and 1 to black and white and rounds to record steps first', () => {
    expect(decodeRecordChannel(0)).toBe(0);
    expect(decodeRecordChannel(1)).toBe(1);
    expect(decodeRecordChannel(0.5)).toBeCloseTo(Math.pow(128 / 255, 2.2), 6);
    expect(decodeRecordChannel(Number.NaN)).toBe(0);
  });
});

describe('fo4LightToThree', () => {
  it('uses the radius as the distance with inverse-square decay', () => {
    const plan = fo4LightToThree({ radius: 300, color: white });
    expect(plan).not.toBeNull();
    expect(plan!.kind).toBe('point');
    expect(plan!.distance).toBe(300);
    expect(plan!.decay).toBe(2);
    expect(plan!.intensity).toBeCloseTo(POINT_LIGHT_GAIN * 300 * 300, 6);
  });

  it('returns null for a zero radius or a black colour', () => {
    expect(fo4LightToThree({ radius: 0, color: white })).toBeNull();
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

describe('cellLightPlan', () => {
  it('falls back to the neutral rig when the cell has no lighting', () => {
    const plan = cellLightPlan(null, [], [0, 0, 0]);
    expect(plan).toEqual(neutralLightPlan());
    expect(plan.ambient.intensity).toBe(0.7);
    expect(plan.fog).toBeNull();
  });

  it('derives ambient in sRGB at intensity 1 from the XCLL colour', () => {
    const plan = cellLightPlan(lighting, [], [0, 0, 0]);
    expect(plan.ambient.color[0]).toBeCloseTo(51 / 255, 6);
    expect(plan.ambient.color[2]).toBeCloseTo(153 / 255, 6);
    expect(plan.ambient.intensity).toBe(1);
  });

  it('turns fog on only when the far distance is past the near distance', () => {
    expect(cellLightPlan(lighting, [], [0, 0, 0]).fog).toEqual({
      color: [0, 0, 0], near: 100, far: 2000,
    });
    expect(cellLightPlan({ ...lighting, fogFar: 0 }, [], [0, 0, 0]).fog).toBeNull();
    expect(cellLightPlan({ ...lighting, fogFar: 50, fogNear: 100 }, [], [0, 0, 0]).fog).toBeNull();
  });

  it('drops the key directional when its colour is black', () => {
    expect(cellLightPlan({ ...lighting, directional: [0, 0, 0] }, [], [0, 0, 0]).directional).toBeNull();
    expect(cellLightPlan(lighting, [], [0, 0, 0]).directional).not.toBeNull();
  });

  it('caps the point lights to the nearest set', () => {
    const many = Array.from({ length: 30 }, (_, i) => fo4LightToThree({ radius: 100, color: white }, [i * 5, 0, 0])!);
    const plan = cellLightPlan(lighting, many, [0, 0, 0]);
    expect(plan.lights).toHaveLength(MAX_CELL_LIGHTS);
    expect(plan.lights[0].position[0]).toBe(0);
  });
});
