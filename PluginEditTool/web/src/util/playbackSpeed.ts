export const MIN_FRAME_INTERVAL_SECONDS = 1 / 120;
export const MAX_FRAME_INTERVAL_SECONDS = 4;

export const PLAYBACK_SPEEDS = [0.25, 0.5, 1, 2, 4] as const;

// Constraint: a speed that is not finite and above zero plays at 1.
export const normalizeSpeed = (speed: number): number => (Number.isFinite(speed) && speed > 0 ? speed : 1);

// Constraint: one frame never plays faster than 1/120 s or slower than 4 s, whatever the speed.
export function frameInterval(frameDuration: number, speed: number): number {
  const base = Number.isFinite(frameDuration) && frameDuration > 0 ? frameDuration : 0;
  return Math.min(Math.max(base / normalizeSpeed(speed), MIN_FRAME_INTERVAL_SECONDS), MAX_FRAME_INTERVAL_SECONDS);
}

// Constraint: playback time advances by this many clip seconds per real second; a clip without a frame step plays at the plain speed.
export function playbackRate(frameDuration: number, speed: number): number {
  const wanted = normalizeSpeed(speed);
  if (!(Number.isFinite(frameDuration) && frameDuration > 0)) return wanted;
  return frameDuration / frameInterval(frameDuration, wanted);
}
