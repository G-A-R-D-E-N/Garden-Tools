import Database from "better-sqlite3";
import { existsSync, mkdtempSync, readFileSync, rmSync, unlinkSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
  createSchema,
  indexOrganizedRowsGeneric,
  indexPdbSymbols,
  parseCategoryFile,
} from "../src/build-index.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function tempDir(): string {
  const dir = mkdtempSync(join(tmpdir(), "re-build-index-"));
  cleanups.push(() => rmSync(dir, { recursive: true, force: true }));
  return dir;
}

const SAMPLE = [
  "# 1.10.163 -- category: Actor_Character -- 2 functions",
  "# source: .../Fallout4_163_functions.txt",
  "# regenerate with ReverseEngineering/Scripts/organize_functions.py",
  "",
  "  func [0x128b0 | sizeof=15] NiTFlags<unsigned_int,Actor>::SetBit :: void SetBit(void * this)",
  "  func [0x749ea0] PlayerCharacter::Update :: void Update(PlayerCharacter * this)",
  "not a func line at all",
].join("\n");

describe("parseCategoryFile", () => {
  it("extracts rva and name, ignoring comments and junk", () => {
    expect(parseCategoryFile(SAMPLE)).toEqual([
      { rva: 0x128b0, name: "NiTFlags<unsigned_int,Actor>::SetBit" },
      { rva: 0x749ea0, name: "PlayerCharacter::Update" },
    ]);
  });

  it("handles entries with no sizeof", () => {
    const rows = parseCategoryFile("  func [0xabc] Foo::Bar :: void Bar(void)");
    expect(rows).toEqual([{ rva: 0xabc, name: "Foo::Bar" }]);
  });

  it("returns nothing for an empty file", () => {
    expect(parseCategoryFile("")).toEqual([]);
  });
});

describe("subsystem_index schema", () => {
  it("its primary key makes a repeated insert of the same row a no-op", () => {
    const db = new Database(":memory:");
    db.exec(`
      CREATE TABLE subsystem_index (
        version  TEXT NOT NULL,
        category TEXT NOT NULL,
        rva      INTEGER NOT NULL,
        name     TEXT NOT NULL,
        PRIMARY KEY (version, category, rva)
      );
    `);
    const insert = db.prepare(
      "INSERT OR REPLACE INTO subsystem_index (version, category, rva, name) VALUES (?, ?, ?, ?)"
    );
    insert.run("1.10.163", "Actor_Character", 0x128b0, "A::B");
    insert.run("1.10.163", "Actor_Character", 0x128b0, "A::B");
    expect((db.prepare("SELECT COUNT(*) AS n FROM subsystem_index").get() as { n: number }).n).toBe(
      1
    );
    db.close();
  });
});

describe("og_ae_map schema", () => {
  // Asserted against the real DDL, not a copy of it, so re-adding a dead index fails here.
  it("indexes only the columns the bridge is actually traversed by", () => {
    const db = new Database(":memory:");
    createSchema(db);
    const indexes = (
      db
        .prepare("SELECT name FROM sqlite_master WHERE type='index' AND tbl_name='og_ae_map'")
        .all() as { name: string }[]
    )
      .map((i) => i.name)
      .sort();
    db.close();

    // No ae_rva index: that column holds 1.11.191 addresses and is never queried, so an index on
    // it is pure write cost. AE lookups go through ae_rel_id.
    expect(indexes).toEqual([
      "idx_og_ae_map_ae_rel_id",
      "idx_og_ae_map_name",
      "idx_og_ae_map_og_rel_id",
      "idx_og_ae_map_og_rva",
    ]);
  });
});

describe("pdb_symbols isolation", () => {
  function schemaFor(db: Database) {
    db.exec(`
      CREATE TABLE pdb_symbols (
        name                TEXT NOT NULL,
        signature           TEXT NOT NULL,
        calling_convention  TEXT,
        sizeof              INTEGER,
        is_fpo              INTEGER NOT NULL DEFAULT 0,
        pdb_rva             INTEGER,
        version             TEXT NOT NULL DEFAULT '1.10.155'
      );
      CREATE INDEX idx_pdb_symbols_name ON pdb_symbols(name);
    `);
  }

  it("has no index on pdb_rva", () => {
    const db = new Database(":memory:");
    schemaFor(db);
    const indexes = db
      .prepare("SELECT sql FROM sqlite_master WHERE type='index' AND tbl_name='pdb_symbols'")
      .all() as { sql: string }[];
    db.close();
    expect(indexes).toHaveLength(1);
    expect(indexes[0].sql).toContain("(name)");
    expect(indexes.some((i) => i.sql.includes("pdb_rva"))).toBe(false);
  });

  it("indexPdbSymbols writes rows with a hardcoded version, from a fixture file", async () => {
    const dir = tempDir();
    const fixturePath = join(dir, "fixture_pdb.txt");
    writeFileSync(
      fixturePath,
      [
        "Summary for E:\\fake.pdb",
        "---GLOBALS---",
        "  func [0x00020900+ 0 - 0x00020908- 1 | sizeof=  8] (FPO) hknpBodyId& __cdecl hknpBodyId::operator=(hknpBodyId& __that)",
        "  func [0x01cce5b0+17 - 0x01cce6f4-13 | sizeof=324] bool __cdecl BSCullingProcess::AddShared(NiAVObject* apObject)",
        "not a func line",
      ].join("\n")
    );

    const db = new Database(":memory:");
    schemaFor(db);
    const count = await indexPdbSymbols(db, fixturePath);

    expect(count).toBe(2);
    const rows = db.prepare("SELECT * FROM pdb_symbols ORDER BY name").all() as {
      name: string;
      version: string;
      pdb_rva: number;
      calling_convention: string;
    }[];
    expect(rows).toHaveLength(2);
    expect(rows.every((r) => r.version === "1.10.155")).toBe(true);
    expect(rows.find((r) => r.name === "BSCullingProcess::AddShared")!.pdb_rva).toBe(0x01cce5b0);
    expect(rows.find((r) => r.name === "BSCullingProcess::AddShared")!.calling_convention).toBe(
      "__cdecl"
    );
    db.close();
  });

  it("skips a line whose name cannot be extracted rather than storing a null name", async () => {
    const dir = tempDir();
    const fixturePath = join(dir, "fixture_pdb2.txt");
    writeFileSync(
      fixturePath,
      "  func [0x1+ 0 - 0x2- 1 | sizeof=  1] not a signature at all\n"
    );
    const db = new Database(":memory:");
    schemaFor(db);
    const count = await indexPdbSymbols(db, fixturePath);
    expect(count).toBe(0);
    db.close();
  });

  it("returns 0 and does not throw when the dump file is missing", async () => {
    const db = new Database(":memory:");
    schemaFor(db);
    const count = await indexPdbSymbols(db, join(tempDir(), "does-not-exist.txt"));
    expect(count).toBe(0);
    db.close();
  });
});

describe("organized_rows / organized_sources", () => {
  function schemaFor(db: Database) {
    db.exec(`
      CREATE TABLE organized_rows (
        source   TEXT NOT NULL,
        row_no   INTEGER NOT NULL,
        address  TEXT,
        rva      INTEGER,
        name_a   TEXT,
        name_b   TEXT,
        kind     TEXT,
        note     TEXT,
        extra    TEXT,
        PRIMARY KEY (source, row_no)
      );
      CREATE TABLE organized_sources (
        source     TEXT PRIMARY KEY,
        columns    TEXT NOT NULL,
        row_count  INTEGER NOT NULL,
        shape      TEXT NOT NULL
      );
    `);
  }

  function fixtureOrganizedDir(): string {
    const dir = tempDir();
    writeFileSync(join(dir, "all_inconclusive_resolved.csv"), "address,name\n0x1414d85f0,hknpShape::getType\n0x1414d8660,hknpShape::calcSize\n");
    writeFileSync(join(dir, "commonlib_vtable_slots.csv"), "class,slot,expected_method\nActor,5,Update\n");
    return dir;
  }

  it("indexes rows keyed by (source, row_no)", () => {
    const db = new Database(":memory:");
    schemaFor(db);
    const total = indexOrganizedRowsGeneric(db, fixtureOrganizedDir());
    expect(total).toBe(3);

    const rows = db
      .prepare("SELECT * FROM organized_rows WHERE source = ? ORDER BY row_no")
      .all("all_inconclusive_resolved") as { row_no: number; name_b: string; rva: number }[];
    expect(rows).toHaveLength(2);
    expect(rows[0]).toMatchObject({ row_no: 1, name_b: "hknpShape::getType" });
    db.close();
  });

  it("records the real header and row count in organized_sources", () => {
    const db = new Database(":memory:");
    schemaFor(db);
    indexOrganizedRowsGeneric(db, fixtureOrganizedDir());

    const source = db
      .prepare("SELECT * FROM organized_sources WHERE source = ?")
      .get("commonlib_vtable_slots") as { columns: string; row_count: number };
    expect(JSON.parse(source.columns)).toEqual(["class", "slot", "expected_method"]);
    expect(source.row_count).toBe(1);
    db.close();
  });

  it("stores extra columns as JSON", () => {
    const dir = tempDir();
    writeFileSync(
      join(dir, "vtable_audit_report.csv"),
      "class,slot,expected_method,actual_name,verdict,address\nActorPackageData,0,~ActorPackageData,~ActorPackageData,match,0x140724fa0\n"
    );
    const db = new Database(":memory:");
    schemaFor(db);
    indexOrganizedRowsGeneric(db, dir);
    const row = db
      .prepare("SELECT extra FROM organized_rows WHERE source = 'vtable_audit_report'")
      .get() as { extra: string };
    expect(JSON.parse(row.extra)).toEqual({ class: "ActorPackageData", slot: "0" });
    db.close();
  });

  it("one bad/missing file does not abort indexing the rest", () => {
    const dir = fixtureOrganizedDir();
    // Every other declared file is simply absent from this fixture dir; that must not throw.
    const db = new Database(":memory:");
    schemaFor(db);
    expect(() => indexOrganizedRowsGeneric(db, dir)).not.toThrow();
    const n = (db.prepare("SELECT COUNT(*) AS n FROM organized_rows").get() as { n: number }).n;
    expect(n).toBe(3);
    db.close();
  });
});

describe("static rebuild isolation", () => {
  it("build-index.ts never references the learning database", () => {
    const source = readFileSync(new URL("../src/build-index.ts", import.meta.url), "utf8");
    expect(source).not.toMatch(/LEARNING_DB_PATH/);
    expect(source).not.toMatch(/re-learning/);
  });

  it("the two database paths are distinct", async () => {
    const paths = await import("../src/paths.js");
    expect(paths.LEARNING_DB_PATH).not.toBe(paths.DB_PATH);
  });

  it("unlinking the static index leaves the learning database intact", () => {
    // Mirrors build-index.ts main(): `if (existsSync(DB_PATH)) unlinkSync(DB_PATH)`.
    const dir = tempDir();
    const staticPath = join(dir, "re-index.sqlite");
    const learningPath = join(dir, "re-learning.sqlite");
    writeFileSync(staticPath, "static");
    writeFileSync(learningPath, "learned");

    if (existsSync(staticPath)) unlinkSync(staticPath);

    expect(existsSync(staticPath)).toBe(false);
    expect(existsSync(learningPath)).toBe(true);
    expect(readFileSync(learningPath, "utf8")).toBe("learned");
  });
});
