import Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import {
  WalUnsafeError,
  assertWalSafe,
  compareSqliteVersions,
  openLearningDb,
  runMigrations,
} from "../../src/learning/db.js";
import { tempLearningDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

describe("sqlite WAL safety gate", () => {
  it("orders versions numerically, not lexically", () => {
    expect(compareSqliteVersions("3.51.3", "3.51.3")).toBe(0);
    expect(compareSqliteVersions("3.9.0", "3.51.3")).toBeLessThan(0);
    expect(compareSqliteVersions("3.53.3", "3.51.3")).toBeGreaterThan(0);
  });

  it("rejects versions affected by the WAL-reset bug", () => {
    expect(() => assertWalSafe("3.51.2")).toThrow(WalUnsafeError);
    expect(() => assertWalSafe("3.7.0")).toThrow(/3\.51\.3/);
  });

  it("accepts the fixed floor and anything above it", () => {
    expect(() => assertWalSafe("3.51.3")).not.toThrow();
    expect(() => assertWalSafe("3.53.3")).not.toThrow();
  });

  it("the bundled better-sqlite3 runtime passes the gate", () => {
    const probe = new Database(":memory:");
    const version = (probe.prepare("SELECT sqlite_version() AS v").get() as { v: string }).v;
    probe.close();
    expect(() => assertWalSafe(version)).not.toThrow();
  });
});

describe("openLearningDb", () => {
  it("creates the file, enables WAL, and stamps user_version", () => {
    const fixture = tempLearningDb();
    cleanups.push(fixture.cleanup);
    const mode = fixture.db.pragma("journal_mode", { simple: true });
    expect(mode).toBe("wal");
    expect(fixture.db.pragma("user_version", { simple: true })).toBeGreaterThan(0);
  });

  it("creates every learning table", () => {
    const fixture = tempLearningDb();
    cleanups.push(fixture.cleanup);
    const names = (
      fixture.db.prepare("SELECT name FROM sqlite_master WHERE type = 'table'").all() as {
        name: string;
      }[]
    ).map((r) => r.name);
    expect(names).toEqual(
      expect.arrayContaining(["learned_findings", "discovered_repos", "consumer_references"])
    );
  });

  it("migrations are idempotent — running twice changes nothing and does not throw", () => {
    const fixture = tempLearningDb();
    cleanups.push(fixture.cleanup);
    const first = fixture.db.pragma("user_version", { simple: true });
    const second = runMigrations(fixture.db);
    expect(second).toBe(first);
    expect(runMigrations(fixture.db)).toBe(first);
  });

  it("reopening an existing database preserves rows", () => {
    const fixture = tempLearningDb();
    cleanups.push(fixture.cleanup);
    fixture.db
      .prepare(
        `INSERT INTO learned_findings
           (version, rva, proposed_name, confidence, origin, raw_decompile_hash,
            model, prompt_version, extractor_version, created_at)
         VALUES ('1.10.163', 100, 'Foo::Bar', 'high', 'interactive', 'h', 'm', '1', '1', '2026-08-13T10:00:00.000Z')`
      )
      .run();
    fixture.db.close();

    const reopened = openLearningDb({ path: fixture.path });
    const count = (
      reopened.prepare("SELECT COUNT(*) AS n FROM learned_findings").get() as { n: number }
    ).n;
    reopened.close();
    expect(count).toBe(1);
  });
});
