import type Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import { classifyIds, measureIdSpace } from "../../src/learning/provenance.js";
import { seedIdSpaces, tempStaticDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function db(): Database.Database {
  const d = tempStaticDb();
  seedIdSpaces(d, { ogMax: 2000, aeStride: 20 });
  cleanups.push(() => d.close());
  return d;
}

describe("provenance", () => {
  it("derives the id space from the index rather than assuming it", () => {
    const space = measureIdSpace(db(), "1.10.163", "1.11.221");
    expect(space.ogMaxId).toBe(2000);
    // 101 AE ids at stride 20 across 2001 slots.
    expect(space.baseline).toBeCloseTo(101 / 2001, 4);
    expect(space.hasAe).toBe(true);
  });

  it("calls a file AE-native when its low ids all appear in the sparse AE library", () => {
    const d = db();
    const space = measureIdSpace(d, "1.10.163", "1.11.221");
    const aeAuthored = Array.from({ length: 60 }, (_, i) => i * 20);
    const p = classifyIds(d, aeAuthored, space, "1.11.221");
    expect(p.verdict).toBe("ae_native");
    expect(p.aeHitRate).toBe(1);
  });

  it("calls a file OG-native when its low ids hit the AE library only at the chance rate", () => {
    const d = db();
    const space = measureIdSpace(d, "1.10.163", "1.11.221");
    // Ids that are never multiples of the AE stride, so they exist only in the dense OG library.
    const ogAuthored = Array.from({ length: 60 }, (_, i) => i * 20 + 7);
    const p = classifyIds(d, ogAuthored, space, "1.11.221");
    expect(p.verdict).toBe("og_native");
    expect(p.aeHitRate).toBe(0);
  });

  it("refuses to guess from too small a sample", () => {
    const d = db();
    const space = measureIdSpace(d, "1.10.163", "1.11.221");
    expect(classifyIds(d, [20, 40, 60], space, "1.11.221").verdict).toBe("insufficient");
  });

  it("cannot run the density test on ids above the OG maximum", () => {
    const d = db();
    const space = measureIdSpace(d, "1.10.163", "1.11.221");
    const high = Array.from({ length: 100 }, (_, i) => 100000 + i);
    const p = classifyIds(d, high, space, "1.11.221");
    expect(p.lowIdCount).toBe(0);
    expect(p.verdict).toBe("insufficient");
  });

  it("falls back to resolution asymmetry when every id is above the OG maximum", () => {
    // A generated table of high ids: no OG library can contain them, the AE library does. That
    // asymmetry is the answer even though the density test has nothing to measure.
    const d = db();
    const space = measureIdSpace(d, "1.10.163", "1.11.221");
    const high = Array.from({ length: 100 }, (_, i) => 100000 + i);
    const p = classifyIds(d, high, space, "1.11.221", "1.10.163");
    expect(p.lowIdCount).toBe(0);
    expect(p.verdict).toBe("ae_native");
    expect(p.aeHitRate).toBe(1);
  });

  it("does not use the fallback while the density test still has a sample", () => {
    const d = db();
    const space = measureIdSpace(d, "1.10.163", "1.11.221");
    const ogAuthored = Array.from({ length: 60 }, (_, i) => i * 20 + 7);
    expect(classifyIds(d, ogAuthored, space, "1.11.221", "1.10.163").verdict).toBe("og_native");
  });

  it("reports mixed rather than picking a side when the population is split", () => {
    const d = db();
    const space = measureIdSpace(d, "1.10.163", "1.11.221");
    const mixed = [
      ...Array.from({ length: 20 }, (_, i) => i * 20),
      ...Array.from({ length: 40 }, (_, i) => i * 20 + 7),
    ];
    expect(classifyIds(d, mixed, space, "1.11.221").verdict).toBe("mixed");
  });
});
