// Every timestamp written to re-learning.sqlite comes through here so tests can pin time.
export interface Clock {
  now(): string;
}

const UTC_ISO = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/;

export const systemClock: Clock = {
  now: () => new Date().toISOString(),
};

export function fixedClock(iso: string): Clock {
  if (!UTC_ISO.test(iso)) {
    throw new Error(`fixedClock needs a UTC ISO-8601 timestamp, got "${iso}"`);
  }
  return { now: () => iso };
}
