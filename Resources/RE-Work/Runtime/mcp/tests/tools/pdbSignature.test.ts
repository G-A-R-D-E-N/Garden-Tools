import Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import { pdbSignature } from "../../src/tools/pdbSignature.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function fixtureDb(): Database.Database {
  const db = new Database(":memory:");
  cleanups.push(() => db.close());
  db.exec(`
    CREATE TABLE pdb_symbols (
      name TEXT, signature TEXT, calling_convention TEXT, sizeof INTEGER, is_fpo INTEGER, pdb_rva INTEGER, version TEXT
    );
  `);
  db.prepare(
    "INSERT INTO pdb_symbols VALUES ('BSCullingProcess::AddShared', 'bool __cdecl BSCullingProcess::AddShared(NiAVObject*)', '__cdecl', 324, 1, 30162864, '1.10.155')"
  ).run();
  db.prepare(
    "INSERT INTO pdb_symbols VALUES ('BSCullingProcess::RemoveShared', 'bool __cdecl BSCullingProcess::RemoveShared(NiAVObject*)', '__cdecl', 200, 0, 30162900, '1.10.155')"
  ).run();
  return db;
}

describe("pdbSignature", () => {
  it("finds signatures by substring", () => {
    const result = pdbSignature({ name: "BSCullingProcess" }, fixtureDb());
    expect(result.matchCount).toBe(2);
    expect(result.matches.map((m) => m.name).sort()).toEqual([
      "BSCullingProcess::AddShared",
      "BSCullingProcess::RemoveShared",
    ]);
  });

  it("returns calling convention, sizeof, and isFpo", () => {
    const result = pdbSignature({ name: "AddShared" }, fixtureDb());
    expect(result.matches[0]).toMatchObject({ callingConvention: "__cdecl", sizeof: 324, isFpo: true });
  });

  it("never includes a pdb_rva field", () => {
    const result = pdbSignature({ name: "AddShared" }, fixtureDb());
    expect(JSON.stringify(result)).not.toContain("pdb_rva");
    expect(JSON.stringify(result)).not.toContain("pdbRva");
  });

  it("always carries the address-space warning note", () => {
    const result = pdbSignature({ name: "AddShared" }, fixtureDb());
    expect(result.note).toMatch(/1\.10\.155/);
    expect(result.note.toLowerCase()).toMatch(/not.*hook|never.*hook/);
  });

  it("returns no matches without throwing", () => {
    const result = pdbSignature({ name: "NoSuchFunction" }, fixtureDb());
    expect(result.matchCount).toBe(0);
    expect(result.matches).toEqual([]);
  });
});
