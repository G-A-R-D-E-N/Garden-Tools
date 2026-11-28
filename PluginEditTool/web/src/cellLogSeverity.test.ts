import { describe, expect, it } from 'vitest';
import { logSeverity, raiseSeverity } from './cellLogSeverity';

describe('logSeverity', () => {
  it('marks real failures as errors', () => {
    expect(logSeverity('[10:00:00] ✗ save: boom')).toBe('error');
    expect(logSeverity('✗ edit failed')).toBe('error');
  });

  it('marks missing textures and materials as warnings, not errors', () => {
    expect(logSeverity('[10:00:00] ⚠ Textures: 5 loaded, 1 not found, 0 failed to decode, 0 shapes with no texture path')).toBe('warn');
    expect(logSeverity('    2 materials not found (bgsm path set, no material resolved)')).toBe('warn');
  });

  it('marks blocked actions and partial geometry as warnings', () => {
    expect(logSeverity('[10:00:00] x create cancelled -- no target plugin chosen.')).toBe('warn');
    expect(logSeverity('• geometry: 8/10 mesh(es) resolved, 2 failed')).toBe('warn');
  });

  it('keeps informational and clean lines green', () => {
    expect(logSeverity('ℹ Textures: 5 loaded, 0 not found, 0 failed to decode, 3 shapes with no texture path')).toBe('ok');
    expect(logSeverity('• geometry: 10/10 mesh(es) resolved, 0 failed')).toBe('ok');
    expect(logSeverity('• loaded CELL -- 12 reference(s), 12 with a model')).toBe('ok');
  });
});

describe('raiseSeverity', () => {
  it('keeps the most severe unseen level', () => {
    expect(raiseSeverity(null, 'warn')).toBe('warn');
    expect(raiseSeverity('warn', 'error')).toBe('error');
    expect(raiseSeverity('error', 'warn')).toBe('error');
  });

  it('ignores ok lines', () => {
    expect(raiseSeverity(null, 'ok')).toBeNull();
    expect(raiseSeverity('warn', 'ok')).toBe('warn');
  });
});
