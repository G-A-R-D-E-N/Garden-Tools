import Database from "better-sqlite3";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { fixedClock } from "../../src/clock.js";
import { loadConfig } from "../../src/config.js";
import { recordFinding } from "../../src/learning/findings.js";
import type { AuditDeps } from "../../src/tools/auditHardcodedAddresses.js";
import { exportLearnings } from "../../src/tools/exportLearnings.js";
import { tempLearningDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

const CLOCK = fixedClock("2026-08-13T10:00:00.000Z");

function deps(): AuditDeps {
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
  return { staticDb: sdb, learningDb: learning.db, clock: CLOCK, config: loadConfig({}) };
}

const BASE = {
  version: "1.10.163",
  rva: 0x1000,
  rawDecompileHash: "h",
  model: "m",
  promptVersion: "1",
  extractorVersion: "1",
  projectFingerprint: null,
  proposedName: "Combat::Start",
  confidence: "high" as const,
  note: "matches",
  origin: "interactive" as const,
  ghidraVersion: null,
};

describe("exportLearnings", () => {
  function tempOutDir(): string {
    const dir = mkdtempSync(join(tmpdir(), "re-export-"));
    cleanups.push(() => rmSync(dir, { recursive: true, force: true }));
    return dir;
  }

  it("counts only current findings, excluding superseded history", async () => {
    const d = deps();
    recordFinding(d.learningDb, CLOCK, BASE);
    recordFinding(
      d.learningDb,
      fixedClock("2026-08-13T11:00:00.000Z"),
      { ...BASE, rawDecompileHash: "h2" }
    );

    const result = await exportLearnings({ write: false }, d, tempOutDir());
    expect(result.count).toBe(1);
    expect(result.wrote).toBeNull();
  });

  it("writes a CSV with static name and contradiction columns when write is true", async () => {
    const d = deps();
    recordFinding(d.learningDb, CLOCK, { ...BASE, proposedName: "Combat::Different" });

    const result = await exportLearnings({ write: true }, d, tempOutDir());

    expect(result.count).toBe(1);
    expect(result.wrote).not.toBeNull();
    expect(existsSync(result.wrote!)).toBe(true);

    const content = readFileSync(result.wrote!, "utf8");
    expect(content).toContain("Combat::Different");
    expect(content).toContain("Combat::Start"); // static_name column
    expect(content).toContain("interactive");
  });

  it("returns count 0 and wrote null for an empty learning DB", async () => {
    const result = await exportLearnings({ write: false }, deps(), tempOutDir());
    expect(result.count).toBe(0);
    expect(result.wrote).toBeNull();
  });

  it("does not write when write is false even with findings present", async () => {
    const d = deps();
    recordFinding(d.learningDb, CLOCK, BASE);
    const result = await exportLearnings({}, d, tempOutDir());
    expect(result.wrote).toBeNull();
  });
});
