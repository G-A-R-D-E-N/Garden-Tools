import Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import { mapOgAe, nameSearchStmt } from "../../src/tools/mapOgAe.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

// Measured 2026-08-14: og_ae_map.ae_rva holds 1.11.191 addresses (3000/3000 sampled resolve in the
// 191 Address Library), coinciding with 1.11.221 only ~59% of the time. ae_rel_id is exact.
// NavMesh::CreateGroupData is the worked case: 191 puts it at 0x77f7a0, 221 at 0x77f7d0.
const OG = "1.10.163";
const AE = "1.11.221";

function seeded(): Database.Database {
  const db = new Database(":memory:");
  cleanups.push(() => db.close());
  db.exec(`
    CREATE TABLE address_library (version TEXT, id INTEGER, rva INTEGER);
    CREATE TABLE og_ae_map (name TEXT, og_rva INTEGER, og_rel_id INTEGER, ae_rva INTEGER, ae_rel_id INTEGER);

    INSERT INTO address_library VALUES
      ('${OG}', 847742,  ${0x749ea0}),
      ('${AE}', 2212327, ${0x77f7d0}),
      -- A second function whose real AE address happens to equal the stale 191 address above.
      ('${AE}', 999001,  ${0x77f7a0});

    INSERT INTO og_ae_map VALUES
      ('NavMesh::CreateGroupData(FORM *,FORM_GROUP *)', ${0x749ea0}, 847742, ${0x77f7a0}, 2212327),
      ('SomeOther::Function(void)',                     ${0x600000}, 600001, ${0x610000}, 999001);
  `);
  return db;
}

/** Adds the trigram FTS mirror that a freshly built index carries. */
function withFts(db: Database.Database): Database.Database {
  db.exec(`
    CREATE VIRTUAL TABLE og_ae_map_fts USING fts5(
      name, content='og_ae_map', content_rowid='rowid', tokenize='trigram');
    INSERT INTO og_ae_map_fts(rowid, name) SELECT rowid, name FROM og_ae_map;
  `);
  return db;
}

describe("og_ae_map name search", () => {
  // A leading-wildcard LIKE cannot use a plain b-tree index, so this was a full scan of 267k rows
  // (~40 ms measured). The trigram FTS mirror turns it into an index lookup.
  it("goes through the trigram index rather than scanning og_ae_map", () => {
    const db = withFts(seeded());
    const plan = (
      db
        .prepare(`EXPLAIN QUERY PLAN ${nameSearchStmt(db).source}`)
        .all("%x%", 25) as { detail: string }[]
    )
      .map((r) => r.detail)
      .join(" | ");

    // "VIRTUAL TABLE INDEX 0:L0" is SQLite reporting that it pushed the LIKE into the trigram
    // index on column 0, which is the whole point; og_ae_map itself is then reached by rowid.
    expect(plan).toContain("VIRTUAL TABLE INDEX 0:L0");
    expect(plan).toContain("SEARCH m USING INTEGER PRIMARY KEY");
    expect(plan).not.toContain("SCAN og_ae_map");
  });

  it("falls back to a plain scan on an index built before the FTS mirror existed", () => {
    const db = seeded();
    expect(nameSearchStmt(db).source).not.toContain("og_ae_map_fts");
    expect(mapOgAe("CreateGroupData", db)).toHaveLength(1);
  });

  it("returns identical results with and without the FTS mirror", () => {
    const withIndex = mapOgAe("CreateGroupData", withFts(seeded()));
    const withoutIndex = mapOgAe("CreateGroupData", seeded());
    expect(withIndex).toEqual(withoutIndex);
  });
});

describe("mapOgAe", () => {
  it("finds an AE address through the Address Library id, not the ae_rva column", () => {
    const results = mapOgAe("0x77f7d0", seeded());
    expect(results).toHaveLength(1);
    expect(results[0].name).toContain("NavMesh::CreateGroupData");
  });

  it("reports the AE address the Address Library gives, not the raw 191 column", () => {
    const results = mapOgAe("0x749ea0", seeded());
    expect(results[0].aeRva).toBe("0x77f7d0");
    expect(results[0].aeRelId).toBe(2212327);
  });

  it("does not return the wrong function when a stale ae_rva collides with a real AE address", () => {
    // 0x77f7a0 is NavMesh::CreateGroupData's *191* address and SomeOther::Function's *221*
    // address. Matching on ae_rva hands back NavMesh — a confident, plausible, wrong answer.
    const results = mapOgAe("0x77f7a0", seeded());
    expect(results.map((r) => r.name)).toEqual(["SomeOther::Function(void)"]);
  });

  it("still finds an OG address by its og_rva", () => {
    const results = mapOgAe("0x749ea0", seeded());
    expect(results).toHaveLength(1);
    expect(results[0].ogRva).toBe("0x749ea0");
    expect(results[0].ogRelId).toBe(847742);
  });

  it("still searches by name", () => {
    const results = mapOgAe("CreateGroupData", seeded());
    expect(results).toHaveLength(1);
    expect(results[0].ogRva).toBe("0x749ea0");
  });

  it("finds a name by an infix substring, case-insensitively", () => {
    const db = withFts(seeded());
    expect(mapOgAe("creategroupdata", db)).toHaveLength(1);
    expect(mapOgAe("FORM_GROUP", db)).toHaveLength(1);
  });

  it("leaves the AE side null when the row has no ae_rel_id", () => {
    const db = seeded();
    db.exec(`INSERT INTO og_ae_map VALUES ('Unbridged::Func', ${0x700000}, 700001, NULL, NULL)`);
    const results = mapOgAe("0x700000", db);
    expect(results[0].aeRva).toBeNull();
    expect(results[0].aeVa).toBeNull();
    expect(results[0].aeRelId).toBeNull();
  });
});
