import { describe, expect, it } from 'vitest';
import {
  DEFAULT_ROUGHNESS, GLOW_WITHOUT_EMIT_INTENSITY, shapeMapPaths, shapeMaterialPlan, type ShapeMaterialData,
} from './cellMaterials';

function bgsm(overrides: Partial<ShapeMaterialData> = {}): ShapeMaterialData {
  return {
    diffuse: 'textures\\a_d.dds',
    normal: 'textures\\a_n.dds',
    smoothSpec: 'textures\\a_s.dds',
    glow: '',
    alphaBlend: false,
    alphaTest: false,
    alphaTestRef: 0,
    alpha: 1,
    twoSided: false,
    emitEnabled: false,
    emittanceColor: [0, 0, 0],
    emittanceMult: 0,
    smoothness: 0.6,
    ...overrides,
  };
}

describe('shapeMapPaths', () => {
  it('reads every map from a BGSM material', () => {
    expect(shapeMapPaths({ material: bgsm({ glow: 'textures\\a_l.dds' }) })).toEqual({
      diffuse: 'textures\\a_d.dds',
      normal: 'textures\\a_n.dds',
      smoothSpec: 'textures\\a_s.dds',
      glow: 'textures\\a_l.dds',
    });
  });

  it('falls back to NIF slots 0 and 1, then to bgsmPath for diffuse', () => {
    expect(shapeMapPaths({ textures: [{ slot: 0, path: 'd.dds' }, { slot: 1, path: 'n.dds' }] })).toEqual({
      diffuse: 'd.dds',
      normal: 'n.dds',
    });
    expect(shapeMapPaths({ bgsmPath: 'materials\\x.bgsm' })).toEqual({ diffuse: 'materials\\x.bgsm' });
  });

  it('drops empty paths', () => {
    expect(shapeMapPaths({ material: bgsm({ normal: '', smoothSpec: '  ' }) })).toEqual({
      diffuse: 'textures\\a_d.dds',
    });
  });
});

describe('shapeMaterialPlan', () => {
  it('keeps two-sided only when the BGSM says so, and stays double-sided for NIF shapes', () => {
    expect(shapeMaterialPlan({ material: bgsm({ twoSided: true }) }, false).side).toBe('double');
    expect(shapeMaterialPlan({ material: bgsm({ twoSided: false }) }, false).side).toBe('front');
    expect(shapeMaterialPlan({ textures: [] }, false).side).toBe('double');
  });

  it('turns alpha test into a threshold and blend into transparency', () => {
    const tested = shapeMaterialPlan({ material: bgsm({ alphaTest: true, alphaTestRef: 128, alphaBlend: true }) }, false);
    expect(tested.alphaTest).toBeCloseTo(128 / 255, 6);
    expect(tested.transparent).toBe(false);
    const blended = shapeMaterialPlan({ material: bgsm({ alphaBlend: true, alpha: 0.5 }) }, false);
    expect(blended.transparent).toBe(true);
    expect(blended.opacity).toBe(0.5);
  });

  it('takes roughness from smoothness, or from the map when one is loaded', () => {
    expect(shapeMaterialPlan({ material: bgsm({ smoothness: 0.6 }) }, false).roughness).toBeCloseTo(0.4, 6);
    expect(shapeMaterialPlan({ material: bgsm({ smoothness: 0 }) }, false).roughness).toBe(DEFAULT_ROUGHNESS);
    const mapped = shapeMaterialPlan({ material: bgsm() }, true);
    expect(mapped.roughness).toBe(1);
    expect(mapped.roughnessFromMap).toBe(true);
  });

  it('emits the emittance colour when enabled and a dim white glow otherwise', () => {
    const emit = shapeMaterialPlan({
      material: bgsm({ emitEnabled: true, emittanceColor: [1, 0.5, 0], emittanceMult: 2 }),
    }, false);
    expect(emit.emissive).toEqual([1, 0.5, 0]);
    expect(emit.emissiveIntensity).toBe(2);
    const glow = shapeMaterialPlan({ material: bgsm({ glow: 'textures\\a_l.dds' }) }, false);
    expect(glow.emissive).toEqual([1, 1, 1]);
    expect(glow.emissiveIntensity).toBe(GLOW_WITHOUT_EMIT_INTENSITY);
    expect(shapeMaterialPlan({ material: bgsm() }, false).emissive).toBeUndefined();
  });
});
