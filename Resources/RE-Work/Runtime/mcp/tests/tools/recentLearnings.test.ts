import Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import { fixedClock } from "../../src/clock.js";
import { loadConfig } from "../../src/config.js";
import { recordFinding } from "../../src/learning/findings.js";
import type { AuditDeps } from "../../src/tools/auditHardcodedAddresses.js";
import { recentLearnings } from "../../src/tools/recentLearnings.js";
import { tempLearningDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

const T10 = fixedClock("2026-08-13T10:00:00.000Z");
const T11 = fixedClock("2026-08-13T11:00:00.000Z");

function setup(): AuditDeps {
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
    INSERT INTO symbols VALUES ('1.10.163', 0x1000, 'Combat::Start', 'sig', 8);
  `);
  return { staticDb: sdb, learningDb: learning.db, clock: T10, config: loadConfig({}) };
}

const BASE = {
  version: "1.10.163",
  rva: 0x1000,
  rawDecompileHash: "h1",
  model: "m",
  promptVersion: "1",
  extractorVersion: "1",
  projectFingerprint: null,
  proposedName: "Combat::Start",
  confidence: "high" as const,
  note: "n",
  origin: "interactive" as const,
  ghidraVersion: null,
};

describe("recent_learnings", () => {
  it("defaults to current-only, all origins, limit 50", async () => {
    const deps = setup();
    recordFinding(deps.learningDb, T10, BASE);
    recordFinding(deps.learningDb, T10, { ...BASE, rva: 0x2000, origin: "daemon" });

    const result = await recentLearnings({}, deps);
    expect(result.currentOnly).toBe(true);
    expect(result.origin).toBe("all");
    expect(result.count).toBe(2);
  });

  it("origin=daemon excludes interactive findings", async () => {
    const deps = setup();
    recordFinding(deps.learningDb, T10, BASE);
    recordFinding(deps.learningDb, T10, { ...BASE, rva: 0x2000, origin: "daemon" });

    const result = await recentLearnings({ origin: "daemon" }, deps);
    expect(result.count).toBe(1);
    expect(result.learnings[0].origin).toBe("daemon");
    expect(result.learnings[0].rva).toBe("0x2000");
  });

  it("current_only=false exposes superseded history", async () => {
    const deps = setup();
    recordFinding(deps.learningDb, T10, BASE);
    recordFinding(deps.learningDb, T11, { ...BASE, rawDecompileHash: "h2" });

    expect((await recentLearnings({}, deps)).count).toBe(1);
    const withHistory = await recentLearnings({ current_only: false }, deps);
    expect(withHistory.count).toBe(2);
    expect(withHistory.learnings.some((l) => !l.isCurrent)).toBe(true);
    expect(withHistory.learnings[0].supersedesId).not.toBeNull();
  });

  it("renders RVA and VA and attaches the static name", async () => {
    const deps = setup();
    recordFinding(deps.learningDb, T10, BASE);
    const learning = (await recentLearnings({}, deps)).learnings[0];
    expect(learning.rva).toBe("0x1000");
    expect(learning.va).toBe("0x140001000");
    expect(learning.staticName).toBe("Combat::Start");
    expect(learning.contradiction).toBeNull();
  });

  it("flags a contradiction against the static index", async () => {
    const deps = setup();
    recordFinding(deps.learningDb, T10, { ...BASE, proposedName: "Combat::Begin" });
    const learning = (await recentLearnings({}, deps)).learnings[0];
    expect(learning.contradiction).toMatchObject({
      staticName: "Combat::Start",
      proposedName: "Combat::Begin",
    });
  });

  it("honours since and limit", async () => {
    const deps = setup();
    recordFinding(deps.learningDb, T10, BASE);
    recordFinding(deps.learningDb, T11, { ...BASE, rva: 0x2000 });

    expect((await recentLearnings({ since: "2026-08-13T10:30:00.000Z" }, deps)).count).toBe(1);
    expect((await recentLearnings({ limit: 1 }, deps)).count).toBe(1);
  });

  it("caps an absurd limit instead of dumping the table", async () => {
    const deps = setup();
    recordFinding(deps.learningDb, T10, BASE);
    const result = await recentLearnings({ limit: 10_000 }, deps);
    expect(result.count).toBe(1);
  });

  it("returns an empty list when nothing has been learned yet", async () => {
    const result = await recentLearnings({}, setup());
    expect(result.count).toBe(0);
    expect(result.learnings).toEqual([]);
  });
});
