import { describe, expect, it } from 'vitest';
import { formatCellTimings } from './cellTimings';

describe('formatCellTimings', () => {
  it('reports index and mesh timings in seconds', () => {
    const line = formatCellTimings({
      indexMs: 1500,
      wallMs: 2250,
      resolveMs: 500,
      extractMs: 1000,
      convertMs: 250,
    });
    expect(line).toBe('Timing: index 1.50s, meshes 2.25s (resolve 0.50s/extract 1.00s/convert 0.25s), textures -');
  });

  it('treats missing batch fields as zero', () => {
    expect(formatCellTimings(null)).toBe(
      'Timing: index 0.00s, meshes 0.00s (resolve 0.00s/extract 0.00s/convert 0.00s), textures -',
    );
    expect(formatCellTimings(undefined, null)).toContain('textures -');
  });

  it('sums texture decode, png write and base64 time into the texture segment', () => {
    const line = formatCellTimings({ wallMs: 1000 }, {
      textureCalls: 3,
      decodeMs: 400,
      pngWriteMs: 100,
      base64Ms: 500,
    });
    expect(line).toMatch(/textures 3 calls 1\.00s$/);
  });

  it('counts zero texture calls when the texture block omits them', () => {
    const line = formatCellTimings({}, { decodeMs: 10 });
    expect(line).toMatch(/textures 0 calls 0\.01s$/);
  });
});
