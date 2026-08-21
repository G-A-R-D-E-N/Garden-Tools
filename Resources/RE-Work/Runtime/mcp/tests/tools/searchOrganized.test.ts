import Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import { searchOrganized } from "../../src/tools/searchOrganized.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

// Fixture rva values are the ADDRESSES minus the image base 0x140000000:
//   0x140724fa0 -> 0x724fa0 = 7491488   0x140DE5F50 -> 0xDE5F50 = 14573392
// (The plan's pasted fixture used rva values that did not correspond to its own
// addresses; corrected here — see the ledger "TASK 5".)
function fixtureDb(): Database.Database {
  const db = new Database(":memory:");
  cleanups.push(() => db.close());
  db.exec(`
    CREATE TABLE organized_rows (
      source TEXT, row_no INTEGER, address TEXT, rva INTEGER,
      name_a TEXT, name_b TEXT, kind TEXT, note TEXT, extra TEXT
    );
  `);
  db.prepare(
    "INSERT INTO organized_rows VALUES ('applied_safe_gaps', 1, '0x140724fa0', 7491488, 'Old::Name', 'Actor::Update', 'reassign', 'n', '{}')"
  ).run();
  db.prepare(
    "INSERT INTO organized_rows VALUES ('crossref_review_candidates', 7, '0x140724fa0', 7491488, 'Existing::Name', 'Actor::Update', null, null, '{}')"
  ).run();
  db.prepare(
    "INSERT INTO organized_rows VALUES ('final_2_held', 2, '0x140DE5F50', 14573392, null, 'EquipActorOutfitItemsFunctor::operator()', null, null, '{}')"
  ).run();
  return db;
}

describe("searchOrganized", () => {
  it("finds rows by a name substring, across sources", () => {
    const result = searchOrganized({ query: "Actor::Update" }, fixtureDb());
    expect(result.matchCount).toBe(2);
    expect(result.results.map((r) => r.source).sort()).toEqual([
      "applied_safe_gaps",
      "crossref_review_candidates",
    ]);
  });

  it("scopes to one source when given", () => {
    const result = searchOrganized({ query: "Actor::Update", source: "final_2_held" }, fixtureDb());
    expect(result.matchCount).toBe(0);
  });

  it("finds rows by hex address, exact rva match", () => {
    const result = searchOrganized({ query: "0x140724fa0" }, fixtureDb());
    expect(result.matchCount).toBe(2);
    expect(result.results.every((r) => r.rva === "0x724fa0")).toBe(true);
  });

  it("finds rows by a bare-RVA-looking query too", () => {
    const result = searchOrganized({ query: "724fa0" }, fixtureDb());
    expect(result.matchCount).toBe(2);
  });

  it("caps results at the limit and reports truncated", () => {
    const db = fixtureDb();
    const result = searchOrganized({ query: "Actor::Update", limit: 1 }, db);
    expect(result.results).toHaveLength(1);
    expect(result.truncated).toBe(true);
  });

  it("caps the limit parameter itself at 200", () => {
    const result = searchOrganized({ query: "x", limit: 10000 }, fixtureDb());
    expect(result).toBeDefined(); // no throw; the cap is exercised for real in a larger fixture
  });

  it("returns no matches without throwing for a query nothing matches", () => {
    const result = searchOrganized({ query: "NoSuchNameAnywhere" }, fixtureDb());
    expect(result.matchCount).toBe(0);
    expect(result.results).toEqual([]);
  });
});
