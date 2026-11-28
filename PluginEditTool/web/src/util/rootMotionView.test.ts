import { describe, expect, it } from 'vitest';
import {
  arrowSegments, loopOffsetStep, motionArm, motionTravel, parseMotionAt, parseMotionShape, pathLine, tickSegment, yawAboutUp,
  type MotionPose,
} from './rootMotionView';

const reply = (extra: object = {}) => JSON.stringify({
  kind: 'default', previewable: true, forward: [0, 1, 0], origin: [0, 0, 0, 0], at: [0, 2, 0, 0.5],
  summary: { net_translation: [0, 4, 0] }, path: [[0, 0, 0, 0], [0, 4, 0, 1]], marks: [[0, 0, 0, 0], [0, 2, 0, 0.5]], ...extra,
});

describe('parseMotionShape', () => {
  it('reads the path, marks, origin and net travel the host reports', () => {
    // Constraint: detects a shape that drops the net travel the loop offset needs or the marks the ticks come from.
    const shape = parseMotionShape(reply());
    expect(shape).toEqual({ origin: [0, 0, 0], net: [0, 4, 0], forward: [0, 1, 0],
      path: [[0, 0, 0, 0], [0, 4, 0, 1]], marks: [[0, 0, 0, 0], [0, 2, 0, 0.5]] });
  });

  it('gives no shape for a motion the host will not preview, never a zero path', () => {
    // Constraint: detects an unsupported or non-previewable motion drawn as a path at the origin.
    expect(parseMotionShape(reply({ previewable: false }))).toBeNull();
    expect(parseMotionShape(JSON.stringify({ kind: 'none', previewable: false }))).toBeNull();
    expect(parseMotionShape(reply({ path: [[0, 0, 'x', 0]] }))).toBeNull();
    expect(parseMotionShape(reply({ path: undefined }))).toBeNull();
  });

  it('turns a host error into a thrown message', () => {
    // Constraint: detects an error reply shown as a clip with no motion.
    expect(() => parseMotionShape(JSON.stringify({ error: 'Could not open x' }))).toThrow('Could not open x');
  });
});

describe('parseMotionAt', () => {
  it('reads the pose at the asked time and refuses a reply with none', () => {
    expect(parseMotionAt(reply())).toEqual([0, 2, 0, 0.5]);
    expect(parseMotionAt(reply({ previewable: false }))).toBeNull();
    expect(parseMotionAt(reply({ at: [0, 'bad', 0, 0] }))).toBeNull();
  });
});

describe('overlay geometry', () => {
  it('lifts the path to the overlay height and keeps x and y', () => {
    expect(pathLine([[1, 2, 9, 0], [3, 4, 9, 0]])).toEqual([[1, 2, 0.5], [3, 4, 0.5]]);
  });

  it('draws each tick across the path, turned by the mark yaw', () => {
    // Constraint: detects ticks that ignore the yaw, which would lie along the path of a turning clip.
    const [from, to] = tickSegment([10, 0, 0, Math.PI / 2], 2);
    expect(from[0]).toBeCloseTo(10);
    expect(from[1]).toBeCloseTo(-2);
    expect(to[1]).toBeCloseTo(2);
    expect(from[2]).toBe(0.5);
  });

  it('draws the end arrow along the turned forward axis with a head behind the tip', () => {
    const segments = arrowSegments([0, 0, 0, 0], [0, 1, 0], 2);
    expect(segments).toHaveLength(3);
    expect(segments[0][0]).toEqual([0, 0, 0.5]);
    expect(segments[0][1][1]).toBeCloseTo(6);
    expect(segments[1][1][1]).toBeLessThan(6);
    expect(segments[2][1][1]).toBeLessThan(6);
    expect(segments[1][1][0]).toBeCloseTo(-segments[2][1][0]);
    const turned = arrowSegments([0, 0, 0, Math.PI / 2], [0, 1, 0], 2);
    expect(turned[0][1][0]).toBeCloseTo(-6);
  });

  it('falls back to the +Y axis for a zero forward vector', () => {
    expect(arrowSegments([0, 0, 0, 0], [0, 0, 0], 2)[0][1][1]).toBeCloseTo(6);
  });

  it('keeps the tick arm at 2 or more and scales it with the scene', () => {
    expect(motionArm(10)).toBe(2);
    expect(motionArm(100)).toBeCloseTo(8);
  });

  it('rotates a vector about the up axis only', () => {
    const turned = yawAboutUp([1, 0, 5], Math.PI / 2);
    expect(turned[0]).toBeCloseTo(0);
    expect(turned[1]).toBeCloseTo(1);
    expect(turned[2]).toBe(5);
  });
});

describe('motionTravel', () => {
  it('is the pose minus the origin', () => {
    const at: MotionPose = [3, 5, 1, 0];
    expect(motionTravel(at, [1, 1, 1])).toEqual([2, 4, 0]);
  });
});

describe('loopOffsetStep', () => {
  const net: [number, number, number] = [0, 4, 0];
  it('moves the fixed camera back one net travel when playback wraps without following', () => {
    // Constraint: detects a looping walk that leaves the frame after the first cycle.
    expect(loopOffsetStep([0, 0, 0], 2, 50, 60, true, false, net)).toEqual([0, -4, 0]);
    expect(loopOffsetStep([0, -4, 0], 1, 59, 60, true, false, net)).toEqual([0, -8, 0]);
  });

  it('leaves the camera alone when the pose itself wrapped back by one net travel', () => {
    // Constraint: detects the camera shifted away from a looping pose that already returned to its origin on the wrap.
    expect(loopOffsetStep([0, 0, 0], 2, 50, 60, true, false, net, [0, 0.2, 0], [0, 3.8, 0])).toEqual([0, 0, 0]);
    expect(loopOffsetStep([0, 0, 0], 2, 50, 60, true, false, net, [0, 4.2, 0], [0, 3.8, 0])).toEqual([0, -4, 0]);
  });

  it('changes nothing while following, paused or advancing', () => {
    // Constraint: detects an offset added twice when the camera already follows, or on a scrub back while paused.
    expect(loopOffsetStep([0, 0, 0], 2, 50, 60, true, true, net)).toEqual([0, 0, 0]);
    expect(loopOffsetStep([0, 0, 0], 2, 50, 60, false, false, net)).toEqual([0, 0, 0]);
    expect(loopOffsetStep([0, 0, 0], 51, 50, 60, true, false, net)).toEqual([0, 0, 0]);
    expect(loopOffsetStep([0, 0, 0], 50, 50, 60, true, false, net)).toEqual([0, 0, 0]);
  });

  it('does not count a scrub backwards while playing as a wrap', () => {
    // Constraint: detects a drag back a few frames shifting the camera by a whole net travel.
    expect(loopOffsetStep([0, 0, 0], 40, 50, 60, true, false, net)).toEqual([0, 0, 0]);
    expect(loopOffsetStep([0, 0, 0], 20, 50, 60, true, false, net)).toEqual([0, 0, 0]);
    expect(loopOffsetStep([0, 0, 0], 19, 50, 60, true, false, net)).toEqual([0, -4, 0]);
  });
});
