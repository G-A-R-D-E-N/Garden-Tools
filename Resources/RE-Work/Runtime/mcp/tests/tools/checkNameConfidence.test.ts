import Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import { computePdbCorroboration } from "../../src/tools/checkNameConfidence.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function dbWithPdbTable(): Database.Database {
  const db = new Database(":memory:");
  cleanups.push(() => db.close());
  db.exec(`
    CREATE TABLE pdb_symbols (
      name TEXT, signature TEXT, calling_convention TEXT, sizeof INTEGER, is_fpo INTEGER, pdb_rva INTEGER, version TEXT
    );
  `);
  db.prepare("INSERT INTO pdb_symbols VALUES ('Actor::SetFlag', 'sig', '__cdecl', 1, 0, 999, '1.10.155')").run();
  return db;
}

describe("computePdbCorroboration", () => {
  it("reports unavailable when pdb_symbols does not exist", () => {
    const db = new Database(":memory:");
    cleanups.push(() => db.close());
    expect(computePdbCorroboration(db, "Actor::SetFlag")).toEqual({ status: "unavailable", matches: [] });
  });

  it("reports confirmed with the matched names when the PDB has the name", () => {
    const result = computePdbCorroboration(dbWithPdbTable(), "Actor::SetFlag");
    expect(result.status).toBe("confirmed");
    expect(result.matches).toContain("Actor::SetFlag");
  });

  it("reports absent when the PDB is indexed but has no match", () => {
    const result = computePdbCorroboration(dbWithPdbTable(), "NoSuchName");
    expect(result).toEqual({ status: "absent", matches: [] });
  });

  it("substring-matches, consistent with this tool's existing name convention", () => {
    const result = computePdbCorroboration(dbWithPdbTable(), "SetFlag");
    expect(result.status).toBe("confirmed");
  });
});
