import { describe, expect, it } from 'vitest';
import {
  countMaterialNotFound,
  countMissing,
  countNoPath,
  createTextureReport,
  formatTextureReport,
  snapshotTextureReport,
  topMissing,
} from './cellTextureReport';

describe('cellTextureReport', () => {
  it('counts shapes with no diffuse path per shader type', () => {
    const report = createTextureReport();
    countNoPath(report, 'BSLightingShaderProperty');
    countNoPath(report, 'BSLightingShaderProperty');
    countNoPath(report, 'BSEffectShaderProperty');
    expect(report.noPath.get('BSLightingShaderProperty')).toBe(2);
    expect(report.noPath.get('BSEffectShaderProperty')).toBe(1);
  });

  it('keeps the ten most frequent distinct missing paths', () => {
    const report = createTextureReport();
    for (let i = 0; i < 12; i++) {
      for (let k = 0; k <= i; k++) countMissing(report, `textures\\p${i}.dds`);
    }
    const top = topMissing(report);
    expect(top).toHaveLength(10);
    expect(top[0]).toEqual(['textures\\p11.dds', 12]);
    expect(top[9]).toEqual(['textures\\p2.dds', 3]);
    expect(report.missing.size).toBe(12);
  });

  it('snapshots the report so later counts do not change it', () => {
    const report = createTextureReport();
    countMissing(report, 'a.dds');
    countNoPath(report, 'BSLightingShaderProperty');
    const snap = snapshotTextureReport(report);
    countMissing(report, 'a.dds');
    countMaterialNotFound(report);
    expect(snap.missing.get('a.dds')).toBe(1);
    expect(snap.noPath.get('BSLightingShaderProperty')).toBe(1);
    expect(snap.materialNotFound).toBe(0);
  });

  it('formats material, shader and missing-path detail lines', () => {
    const report = createTextureReport();
    countMaterialNotFound(report);
    countMaterialNotFound(report);
    countNoPath(report, 'BSLightingShaderProperty');
    countMissing(report, 'textures\\x.dds');
    const lines = formatTextureReport(report);
    expect(lines[0]).toBe('2 materials not found (bgsm path set, no material resolved)');
    expect(lines).toContain('1 shape with no diffuse path, shader BSLightingShaderProperty');
    expect(lines).toContain('Missing textures, top 1 of 1:');
    expect(lines).toContain('  textures\\x.dds (x1)');
  });

  it('formats an empty report as no lines', () => {
    expect(formatTextureReport(createTextureReport())).toEqual([]);
  });
});
