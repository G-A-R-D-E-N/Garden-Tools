import Database from "better-sqlite3";
import { afterEach, describe, expect, it, vi } from "vitest";
import { fixedClock } from "../../src/clock.js";
import { loadConfig } from "../../src/config.js";
import type { AnalyzeDeps } from "../../src/learning/analyze.js";
import { learnFromTarget } from "../../src/tools/learnFromTarget.js";
import { tempLearningDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function deps(overrides: Partial<AnalyzeDeps> = {}): AnalyzeDeps {
  const learning = tempLearningDb();
  cleanups.push(learning.cleanup);
  const sdb = new Database(":memory:");
  cleanups.push(() => sdb.close());
  sdb.exec(`
    CREATE TABLE symbols (version TEXT, rva INTEGER, name TEXT, signature TEXT, sizeof INTEGER);
    CREATE TABLE address_library (version TEXT, id INTEGER, rva INTEGER);
    CREATE TABLE name_disagreements (address TEXT, rva INTEGER, existing_name TEXT, decoded_name TEXT, note TEXT, verdict_155 TEXT);
    CREATE TABLE misname_corrections (address TEXT, rva INTEGER, old_name TEXT, new_name TEXT, method TEXT, note TEXT);
    CREATE TABLE misname_reverts (address TEXT, rva INTEGER, wrongly_renamed_to TEXT, revert_to TEXT);
    INSERT INTO symbols VALUES ('1.10.163', 0x128b0, 'Actor::SetFlag', 'void SetFlag(Actor *)', 15);
  `);
  return {
    staticDb: sdb,
    learningDb: learning.db,
    clock: fixedClock("2026-08-13T10:00:00.000Z"),
    config: loadConfig({}),
    decompile: vi.fn(async () => ({
      version: "1.10.163",
      rva: 0x128b0,
      entryRva: 0x128b0,
      functionName: "Actor::SetFlag",
      body: "void SetFlag(Actor *this) { this->flags |= 2; }",
      normalizedHash: "hash-a",
      truncated: false,
    })),
    extract: vi.fn(async () => ({
      proposedName: "Actor::SetFlag",
      confidence: "high" as const,
      note: "sets bit 1",
    })),
    projectFingerprint: () => "proj-1",
    ...overrides,
  };
}

describe("learn_from_target", () => {
  it("returns the finding with both RVA and VA rendered", async () => {
    const result = await learnFromTarget({ target: "0x128b0" }, deps());
    expect(result.version).toBe("1.10.163");
    expect(result.rva).toBe("0x128b0");
    expect(result.va).toBe("0x1400128b0");
    expect(result.analysisRun).toBe(true);
    expect(result.finding.proposedName).toBe("Actor::SetFlag");
    expect(result.contradiction).toBeNull();
    expect(result.warning).toBeNull();
  });

  it("reports analysisRun=false on a deduplicated repeat", async () => {
    const d = deps();
    await learnFromTarget({ target: "0x128b0" }, d);
    const second = await learnFromTarget({ target: "0x128b0" }, d);
    expect(second.analysisRun).toBe(false);
  });

  it("includes the static-index context", async () => {
    const result = await learnFromTarget({ target: "0x128b0" }, deps());
    expect(result.staticIndex.name).toBe("Actor::SetFlag");
    expect(result.staticIndex.confidenceVerdict).toBe("no_audit_trail");
  });

  it("emits a loud warning when the finding contradicts an audited static name", async () => {
    const d = deps({
      extract: vi.fn(async () => ({
        proposedName: "Actor::ClearFlag",
        confidence: "high" as const,
        note: "looks like a clear",
      })),
    });
    d.staticDb
      .prepare(
        "INSERT INTO misname_corrections VALUES ('0x1400128b0', 0x128b0, 'Old', 'Actor::SetFlag', 'decompile', 'verified')"
      )
      .run();

    const result = await learnFromTarget({ target: "0x128b0" }, d);
    expect(result.contradiction).toMatchObject({ staticIsAudited: true });
    expect(result.warning).toMatch(/CONTRADICTION/i);
    expect(result.warning).toContain("Actor::SetFlag");
    expect(result.warning).toContain("Actor::ClearFlag");
  });

  it("warns more softly when the static name was never audited", async () => {
    const d = deps({
      extract: vi.fn(async () => ({
        proposedName: "Actor::ClearFlag",
        confidence: "low" as const,
        note: "guess",
      })),
    });
    const result = await learnFromTarget({ target: "0x128b0" }, d);
    expect(result.contradiction).toMatchObject({ staticIsAudited: false });
    expect(result.warning).toMatch(/disagrees/i);
  });

  it("propagates a resolution failure as a readable error", async () => {
    await expect(learnFromTarget({ target: "Nope::Missing" }, deps())).rejects.toThrow(
      /No symbol matches/
    );
  });

  it("passes force through to the pipeline", async () => {
    const d = deps();
    await learnFromTarget({ target: "0x128b0" }, d);
    const forced = await learnFromTarget({ target: "0x128b0", force: true }, d);
    expect(forced.analysisRun).toBe(true);
  });
});
