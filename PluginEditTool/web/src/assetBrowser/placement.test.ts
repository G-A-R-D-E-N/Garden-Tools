import { describe, expect, it } from 'vitest';
import { parseSnapSetting, placementPoints, snapPoint, snapValue, spiralOffsets } from './placement';

describe('snap grid', () => {
  it('rounds a coordinate to the nearest grid line', () => {
    expect(snapValue(39, 16)).toBe(32);
    expect(snapValue(-70, 64)).toBe(-64);
    expect(snapValue(70, 64)).toBe(64);
  });

  it('leaves the value alone when the grid is not positive', () => {
    expect(snapValue(39, 0)).toBe(39);
  });

  it('snaps x and y but keeps the height exactly', () => {
    expect(snapPoint({ x: 70, y: -70, z: 12.5 }, 64)).toEqual({ x: 64, y: -64, z: 12.5 });
  });

  it('returns the same point when snapping is off', () => {
    const point = { x: 70.4, y: -70.6, z: 1 };
    expect(snapPoint(point, null)).toBe(point);
  });
});

describe('spiral offsets for multi-drop', () => {
  it('starts at the drop point itself', () => {
    expect(spiralOffsets(1)).toEqual([{ x: 0, y: 0 }]);
    expect(spiralOffsets(0)).toEqual([]);
  });

  it('winds outward ring by ring in unit steps', () => {
    expect(spiralOffsets(5, 1)).toEqual([
      { x: 0, y: 0 },
      { x: 1, y: 0 },
      { x: 1, y: 1 },
      { x: 0, y: 1 },
      { x: -1, y: 1 },
    ]);
  });

  it('never repeats a position within a large batch', () => {
    const offsets = spiralOffsets(200, 64);
    const keys = new Set(offsets.map(o => `${o.x},${o.y}`));
    expect(keys.size).toBe(200);
  });

  it('scales the offsets by the spacing', () => {
    expect(spiralOffsets(2, 64)).toEqual([{ x: 0, y: 0 }, { x: 64, y: 0 }]);
  });
});

describe('placement points', () => {
  it('snaps the origin then fans the batch out from it', () => {
    expect(placementPoints({ x: 100, y: 100, z: 5 }, 2, 64)).toEqual([
      { x: 128, y: 128, z: 5 },
      { x: 192, y: 128, z: 5 },
    ]);
  });

  it('uses the raw origin when snapping is off', () => {
    expect(placementPoints({ x: 100, y: 100, z: 5 }, 1, null)).toEqual([{ x: 100, y: 100, z: 5 }]);
  });
});

describe('snap setting parsing', () => {
  it('reads off as no snapping and each listed grid as itself', () => {
    expect(parseSnapSetting('off')).toBeNull();
    expect(parseSnapSetting('1')).toBe(1);
    expect(parseSnapSetting('8')).toBe(8);
    expect(parseSnapSetting('16')).toBe(16);
    expect(parseSnapSetting('64')).toBe(64);
  });

  it('falls back to the default grid for missing or unknown values', () => {
    expect(parseSnapSetting(null)).toBe(64);
    expect(parseSnapSetting('3')).toBe(64);
    expect(parseSnapSetting('abc')).toBe(64);
  });
});
