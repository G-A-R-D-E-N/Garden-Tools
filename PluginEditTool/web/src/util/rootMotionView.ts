import type { Vec3 } from './hkxViewOptions';

export type MotionPose = [number, number, number, number];

export interface MotionShape {
  origin: Vec3;
  net: Vec3;
  forward: Vec3;
  path: MotionPose[];
  marks: MotionPose[];
}

export interface MotionState {
  shape: MotionShape;
  at: MotionPose;
  frame: number;
  frameCount: number;
  playing: boolean;
}

interface MotionReply {
  previewable?: boolean;
  forward?: Vec3;
  origin?: MotionPose;
  at?: MotionPose;
  summary?: { net_translation?: Vec3 };
  path?: MotionPose[];
  marks?: MotionPose[];
  error?: string;
}

// Constraint: the overlay sits 0.5 above the ground plane like the BGS root-motion line, so it does not fight the grid.
export const OVERLAY_HEIGHT = 0.5;

const parseReply = (raw: unknown): MotionReply => {
  const reply = (typeof raw === 'string' ? JSON.parse(raw) : raw) as MotionReply;
  if (reply.error) throw new Error(reply.error);
  return reply;
};

const isPose = (value: unknown): value is MotionPose =>
  Array.isArray(value) && value.length === 4 && value.every(n => typeof n === 'number' && Number.isFinite(n));

// Requires the reply of ExtractedMotion with include_path; a motion Radicle will not preview yields null, never a zero path.
export function parseMotionShape(raw: unknown): MotionShape | null {
  const reply = parseReply(raw);
  if (!reply.previewable || !isPose(reply.origin) || !Array.isArray(reply.path) || !reply.path.every(isPose)) return null;
  const net = reply.summary?.net_translation;
  const forward = reply.forward;
  return {
    origin: [reply.origin[0], reply.origin[1], reply.origin[2]],
    net: net && net.length === 3 ? net : [0, 0, 0],
    forward: forward && forward.length === 3 ? forward : [0, 1, 0],
    path: reply.path,
    marks: Array.isArray(reply.marks) ? reply.marks.filter(isPose) : [],
  };
}

export function parseMotionAt(raw: unknown): MotionPose | null {
  const reply = parseReply(raw);
  return reply.previewable && isPose(reply.at) ? reply.at : null;
}

export function yawAboutUp(v: Vec3, turn: number): Vec3 {
  const c = Math.cos(turn);
  const s = Math.sin(turn);
  return [v[0] * c - v[1] * s, v[0] * s + v[1] * c, v[2]];
}

const lift = (pose: MotionPose | Vec3): Vec3 => [pose[0], pose[1], OVERLAY_HEIGHT];

export const pathLine = (path: MotionPose[]): Vec3[] => path.map(lift);

export const motionArm = (extent: number): number => Math.max(2, extent * 0.08);

export function tickSegment(mark: MotionPose, arm: number): [Vec3, Vec3] {
  const side = yawAboutUp([arm, 0, 0], mark[3]);
  const at = lift(mark);
  return [[at[0] - side[0], at[1] - side[1], at[2]], [at[0] + side[0], at[1] + side[1], at[2]]];
}

export function arrowSegments(end: MotionPose, forward: Vec3, arm: number): [Vec3, Vec3][] {
  const length = Math.hypot(...forward);
  const facing: Vec3 = length * length > 1e-8 ? [forward[0] / length, forward[1] / length, forward[2] / length] : [0, 1, 0];
  const turned = yawAboutUp(facing, end[3]);
  const ahead: Vec3 = [turned[0] * arm * 3, turned[1] * arm * 3, turned[2] * arm * 3];
  const right: Vec3 = [ahead[1] * 0.3, -ahead[0] * 0.3, 0];
  const start = lift(end);
  const tip: Vec3 = [start[0] + ahead[0], start[1] + ahead[1], start[2] + ahead[2]];
  const back: Vec3 = [tip[0] - ahead[0] * 0.3, tip[1] - ahead[1] * 0.3, tip[2] - ahead[2] * 0.3];
  return [
    [start, tip],
    [tip, [back[0] + right[0], back[1] + right[1], back[2]]],
    [tip, [back[0] - right[0], back[1] - right[1], back[2]]],
  ];
}

export const motionTravel = (at: MotionPose, origin: Vec3): Vec3 => [at[0] - origin[0], at[1] - origin[1], at[2] - origin[2]];

// Invariant: a wrap (a drop of more than half the clip) while playing and not following moves the fixed camera back by one net travel, but only when the pose keeps accumulating; a pose that itself dropped back by about one net travel already wrapped, so the camera stays. A small step back or a scrub never moves it.
export function loopOffsetStep(offset: Vec3, frame: number, lastFrame: number, frameCount: number, playing: boolean, follow: boolean, net: Vec3,
                               travel: Vec3 = [0, 0, 0], lastTravel: Vec3 = [0, 0, 0]): Vec3 {
  if (!playing || follow || !(lastFrame - frame > frameCount / 2)) return offset;
  const length = net[0] * net[0] + net[1] * net[1] + net[2] * net[2];
  const along = (travel[0] - lastTravel[0]) * net[0] + (travel[1] - lastTravel[1]) * net[1] + (travel[2] - lastTravel[2]) * net[2];
  if (length > 0 && along / length < -0.5) return offset;
  return [offset[0] - net[0], offset[1] - net[1], offset[2] - net[2]];
}
