import { describe, expect, it } from "vitest";
import { fixedClock, systemClock } from "../src/clock.js";

describe("clock", () => {
  it("systemClock returns a UTC ISO-8601 string", () => {
    const now = systemClock.now();
    expect(now).toMatch(/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/);
  });

  it("fixedClock returns exactly what it was given, every time", () => {
    const clock = fixedClock("2026-08-13T10:00:00.000Z");
    expect(clock.now()).toBe("2026-08-13T10:00:00.000Z");
    expect(clock.now()).toBe("2026-08-13T10:00:00.000Z");
  });

  it("fixedClock rejects a non-UTC-ISO input so tests cannot drift", () => {
    expect(() => fixedClock("2026-08-13")).toThrow(/UTC ISO-8601/);
  });
});
