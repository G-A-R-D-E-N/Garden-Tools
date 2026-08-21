import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { WalUnsafeError, assertWalSafe, openLearningDb } from "../src/learning/db.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

describe("graceful degradation", () => {
  it("the static read-only tools do not import the learning database", async () => {
    // If any legacy tool pulled in the learning layer, importing it would create the file.
    const before = existsSync(new URL("../re-learning.sqlite", import.meta.url));
    await import("../src/tools/lookupByName.js");
    await import("../src/tools/checkNameConfidence.js");
    await import("../src/tools/getVtableSlots.js");
    const after = existsSync(new URL("../re-learning.sqlite", import.meta.url));
    expect(after).toBe(before);
  });

  it("the learning tool modules do not open a database at import time", async () => {
    const dir = mkdtempSync(join(tmpdir(), "re-degrade-"));
    cleanups.push(() => rmSync(dir, { recursive: true, force: true }));
    const path = join(dir, "never-created.sqlite");

    const previous = process.env.RE_MCP_LEARNING_DB_PATH;
    process.env.RE_MCP_LEARNING_DB_PATH = path;
    cleanups.push(() => {
      if (previous === undefined) delete process.env.RE_MCP_LEARNING_DB_PATH;
      else process.env.RE_MCP_LEARNING_DB_PATH = previous;
    });

    await import("../src/tools/learnFromTarget.js");
    await import("../src/tools/recentLearnings.js");

    expect(existsSync(path)).toBe(false);
  });

  it("a WAL-unsafe runtime is refused with a message that names the floor", () => {
    try {
      assertWalSafe("3.40.0");
      expect.unreachable("should have thrown");
    } catch (err) {
      expect(err).toBeInstanceOf(WalUnsafeError);
      expect((err as Error).message).toContain("3.51.3");
      expect((err as Error).message).toMatch(/read-only static index tools are unaffected/i);
    }
  });

  it("opening the learning DB in an unwritable directory throws rather than corrupting state", () => {
    expect(() => openLearningDb({ path: "/proc/definitely-not-writable/x.sqlite" })).toThrow();
  });
});
