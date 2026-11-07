export function advanceTime(time: number, seconds: number, duration: number): number {
  if (duration <= 0) return 0;
  const t = time + seconds;
  return ((t % duration) + duration) % duration;
}
