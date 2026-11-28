import { describe, expect, it } from 'vitest';
import { frameInterval, normalizeSpeed, playbackRate } from './playbackSpeed';

describe('playbackSpeed', () => {
  it('scales the frame interval by the speed and clamps it to 1/120 s and 4 s', () => {
    // Constraint: detects a speed that is not applied to the interval or an interval that leaves the exact safety clamps.
    expect(frameInterval(1 / 30, 2)).toBeCloseTo(1 / 60, 6);
    expect(frameInterval(1 / 30, Infinity)).toBeCloseTo(1 / 30, 6);
    expect(frameInterval(1 / 30, 1_000_000)).toBeCloseTo(1 / 120, 6);
    expect(frameInterval(100, 0.01)).toBe(4);
  });

  it.each([0, -1, NaN, -Infinity, Infinity])('falls back to 1 for the speed %s', speed => {
    // Constraint: detects a bad speed that stops, reverses or freezes playback instead of playing at normal speed.
    expect(normalizeSpeed(speed)).toBe(1);
    expect(frameInterval(0.5, speed)).toBeCloseTo(0.5, 6);
    expect(playbackRate(0.5, speed)).toBeCloseTo(1, 6);
  });

  it('plays a clip second per real second at a plain speed and holds the clamp at the extremes', () => {
    // Constraint: detects a rate that ignores the interval clamp, so a huge speed would outrun the 1/120 s frame floor.
    expect(playbackRate(1 / 30, 2)).toBeCloseTo(2, 6);
    expect(playbackRate(1 / 30, 0.5)).toBeCloseTo(0.5, 6);
    expect(playbackRate(1 / 30, 1_000_000)).toBeCloseTo(4, 5);
    expect(playbackRate(100, 0.01)).toBeCloseTo(25, 6);
    expect(playbackRate(0, 2)).toBe(2);
  });
});
